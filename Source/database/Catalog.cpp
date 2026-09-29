#include "Database.h"
#include "../library/TrackNaming.h"

// Catálogo de la biblioteca: artistas (con una clave que no distingue mayúsculas ni tildes),
// alias aprendidos de las correcciones, créditos de cada pista (intérprete principal,
// invitados, compositores), lotes de importación y el borrador de la revisión. Ver Database.h.

namespace {
    void bindText(sqlite3_stmt* stmt, int index, const std::string& text) {
        sqlite3_bind_text(stmt, index, text.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::string columnText(sqlite3_stmt* stmt, int column) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
        return text != nullptr ? text : "";
    }

    // Huella acústica: enteros de 32 bits, guardados en orden little-endian.
    std::vector<uint32_t> blobToFingerprint(sqlite3_stmt* stmt, int column) {
        const auto* bytes = static_cast<const uint8_t*>(sqlite3_column_blob(stmt, column));
        const int size = sqlite3_column_bytes(stmt, column);
        std::vector<uint32_t> fingerprint(bytes != nullptr ? (size_t) size / 4 : 0);
        for (size_t i = 0; i < fingerprint.size(); ++i)
            fingerprint[i] = (uint32_t) bytes[4 * i] | (uint32_t) bytes[4 * i + 1] << 8
                           | (uint32_t) bytes[4 * i + 2] << 16 | (uint32_t) bytes[4 * i + 3] << 24;
        return fingerprint;
    }

    std::string keyOf(const std::string& name) {
        return TrackNaming::key(juce::String::fromUTF8(name.c_str())).toStdString();
    }

    // Todo o nada: si no se llega a commit(), al salir se deshace.
    struct Transaction {
        explicit Transaction(sqlite3* database) : db(database) {
            began = sqlite3_exec(db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr) == SQLITE_OK;
        }
        ~Transaction() {
            if (began && !committed) sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
        bool commit(bool allOk) {
            if (!began || !allOk) return false;
            committed = sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
            return committed;
        }
        sqlite3* db;
        bool began = false, committed = false;
    };

    TrackInfo notFound() {
        TrackInfo t {};
        t.id = -1;
        t.normalizationGain = 1.0;
        return t;
    }

    // Música: pistas que no están en ninguna categoría del sistema.
    const char* const isMusic =
        "t.id NOT IN (SELECT tc.track_id FROM track_categories tc JOIN categories c ON c.id = tc.category_id "
        "WHERE c.system_role > 0)";
}

void Database::createCatalogTables() {
    execute("CREATE TABLE IF NOT EXISTS artists ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "name TEXT NOT NULL, "
            "name_key TEXT NOT NULL UNIQUE, "
            "created_at TEXT DEFAULT '');");
    execute("CREATE TABLE IF NOT EXISTS artist_aliases ("
            "alias_key TEXT PRIMARY KEY, "
            "artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE);");
    execute("CREATE TABLE IF NOT EXISTS track_credits ("
            "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
            "artist_id INTEGER NOT NULL REFERENCES artists(id) ON DELETE CASCADE, "
            "role INTEGER NOT NULL, "
            "position INTEGER NOT NULL DEFAULT 0, "
            "PRIMARY KEY (track_id, role, position));");
    execute("CREATE INDEX IF NOT EXISTS idx_credits_artist ON track_credits(artist_id, role);");
    execute("CREATE TABLE IF NOT EXISTS import_batches ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "created_at TEXT NOT NULL, "
            "source TEXT DEFAULT '', "
            "category_id INTEGER, "
            "files INTEGER DEFAULT 0);");

    // Lo que se encontró en línea de cada pista (AcoustID y MusicBrainz).
    execute("CREATE TABLE IF NOT EXISTS online_matches ("
            "track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE, "
            "status INTEGER NOT NULL, "
            "score REAL DEFAULT 0, "
            "recording_id TEXT DEFAULT '', "
            "artist TEXT DEFAULT '', "
            "featured TEXT DEFAULT '', "
            "title TEXT DEFAULT '', "
            "year INTEGER DEFAULT 0, "
            "composers TEXT DEFAULT '', "
            "isrc TEXT DEFAULT '', "
            "error TEXT DEFAULT '', "
            "checked_at TEXT DEFAULT '');");

    // Descargas de la búsqueda en internet (yt-dlp): un intento por fila.
    execute("CREATE TABLE IF NOT EXISTS downloads ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT, "
            "video_id TEXT NOT NULL, "
            "deezer_id INTEGER DEFAULT 0, "
            "artist TEXT DEFAULT '', "
            "title TEXT DEFAULT '', "
            "status INTEGER NOT NULL DEFAULT 0, "
            "track_id INTEGER DEFAULT -1, "
            "error TEXT DEFAULT '', "
            "started_at TEXT NOT NULL, "
            "finished_at TEXT DEFAULT '');");
    execute("CREATE INDEX IF NOT EXISTS idx_downloads_started ON downloads(started_at);");

    // Búsqueda sin mayúsculas ni tildes (FTS5). keys: las claves del artista y el título
    // ("dleon" para "D'León"), que el separador de palabras de FTS no produce.
    ftsAvailable = sqlite3_exec(db, "CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5("
                                    "artist, title, people, extra, keys, tokenize = 'unicode61 remove_diacritics 2');",
                                nullptr, nullptr, nullptr) == SQLITE_OK;
    if (!ftsAvailable) juce::Logger::writeToLog("Database: FTS5 not available, search uses LIKE");
}

int Database::migrateCatalog() {
    std::vector<std::pair<int, std::string>> keys;
    each("SELECT id, artist, title FROM tracks;", {}, [&keys](sqlite3_stmt* s) {
        const auto artist = juce::String::fromUTF8(columnText(s, 1).c_str()).upToFirstOccurrenceOf(" feat. ", false, false);
        const auto title = juce::String::fromUTF8(columnText(s, 2).c_str());
        keys.push_back({ sqlite3_column_int(s, 0), TrackNaming::matchKey(artist, title).toStdString() });
    });

    Transaction t(db);
    bool ok = t.began;
    for (const auto& [id, matchKey] : keys)
        ok = ok && run("UPDATE tracks SET match_key = ?1 WHERE id = ?2;", [&](sqlite3_stmt* s) {
            bindText(s, 1, matchKey);
            sqlite3_bind_int(s, 2, id);
        });
    ok = ok && execute("PRAGMA user_version = 3;");
    if (!t.commit(ok)) {
        juce::Logger::writeToLog("Database: catalog migration failed; it will be retried at the next start");
        return -1;
    }
    return (int) keys.size();
}

//==============================================================================
// Importación

int Database::createImportBatch(const std::string& source, int categoryId, int files) {
    const bool ok = run("INSERT INTO import_batches (created_at, source, category_id, files) "
                        "VALUES (datetime('now', 'localtime'), ?1, ?2, ?3);", [&](sqlite3_stmt* s) {
        bindText(s, 1, source);
        if (categoryId >= 0) sqlite3_bind_int(s, 2, categoryId);
        else sqlite3_bind_null(s, 2);
        sqlite3_bind_int(s, 3, files);
    });
    return ok ? (int) sqlite3_last_insert_rowid(db) : -1;
}

bool Database::setImportSource(int trackId, const ImportSource& source) {
    const bool ok = run("UPDATE tracks SET source_name = ?1, source_folder = ?2, tag_artist = ?3, tag_title = ?4, "
                        "import_batch = ?5 WHERE id = ?6;", [&](sqlite3_stmt* s) {
        bindText(s, 1, source.name);
        bindText(s, 2, source.folder);
        bindText(s, 3, source.tagArtist);
        bindText(s, 4, source.tagTitle);
        sqlite3_bind_int(s, 5, source.batch);
        sqlite3_bind_int(s, 6, trackId);
    });
    if (ok) indexTrack(trackId);
    return ok;
}

//==============================================================================
// Revisión

std::vector<Database::ReviewRecord> Database::getReviewRecords(bool pending) {
    std::vector<ReviewRecord> records;
    std::string sql = std::string("SELECT ") + "t.id, t.path, t.artist, t.title, t.play_count, t.duration, t.last_played, "
        "t.normalization_gain, t.created_at, t.updated_at, t.loudness_lufs, t.true_peak_db, t.cue_in, t.cue_out, "
        "t.analysis_version, t.file_hash, t.status, t.version, t.year, t.confirmed_at, "
        "t.source_name, t.source_folder, t.tag_artist, t.tag_title, t.import_batch, t.review_draft, t.fingerprint FROM tracks t WHERE ";
    sql += pending ? "t.status = 1 " : (std::string("t.status = 0 AND t.confirmed_at = '' AND ") + isMusic + " ");
    sql += "ORDER BY t.source_folder, t.id;";

    each(sql.c_str(), {}, [this, &records](sqlite3_stmt* s) {
        ReviewRecord r;
        r.track = readTrackRow(s);
        r.source.name = columnText(s, 20);
        r.source.folder = columnText(s, 21);
        r.source.tagArtist = columnText(s, 22);
        r.source.tagTitle = columnText(s, 23);
        r.source.batch = sqlite3_column_int(s, 24);
        r.draft = columnText(s, 25);
        r.fingerprint = blobToFingerprint(s, 26);
        records.push_back(std::move(r));
    });
    return records;
}

int Database::countPending() {
    int count = 0;
    each("SELECT COUNT(*) FROM tracks WHERE status = 1;", {}, [&count](sqlite3_stmt* s) { count = sqlite3_column_int(s, 0); });
    return count;
}

int Database::countUnreviewed() {
    int count = 0;
    const auto sql = std::string("SELECT COUNT(*) FROM tracks t WHERE t.status = 0 AND t.confirmed_at = '' AND ") + isMusic + ";";
    each(sql.c_str(), {}, [&count](sqlite3_stmt* s) { count = sqlite3_column_int(s, 0); });
    return count;
}

bool Database::saveReviewDraft(int trackId, const std::string& json) {
    return run("UPDATE tracks SET review_draft = ?1 WHERE id = ?2;", [&](sqlite3_stmt* s) {
        bindText(s, 1, json);
        sqlite3_bind_int(s, 2, trackId);
    });
}

//==============================================================================
// Artistas y créditos

std::vector<Database::ArtistRow> Database::getKnownArtists() {
    std::vector<ArtistRow> rows;
    each("SELECT a.name, a.name_key, SUM(CASE WHEN c.role = 1 THEN 1 ELSE 0 END) FROM artists a "
         "JOIN track_credits c ON c.artist_id = a.id GROUP BY a.id;", {}, [&rows](sqlite3_stmt* s) {
        rows.push_back({ columnText(s, 0), columnText(s, 1), sqlite3_column_int(s, 2) });
    });
    return rows;
}

std::vector<std::pair<std::string, std::string>> Database::getArtistAliases() {
    std::vector<std::pair<std::string, std::string>> rows;
    each("SELECT al.alias_key, a.name_key FROM artist_aliases al JOIN artists a ON a.id = al.artist_id;", {},
         [&rows](sqlite3_stmt* s) { rows.push_back({ columnText(s, 0), columnText(s, 1) }); });
    return rows;
}

std::vector<Database::Credit> Database::getTrackCredits(int trackId) {
    std::vector<Credit> credits;
    each("SELECT c.artist_id, a.name, c.role FROM track_credits c JOIN artists a ON a.id = c.artist_id "
         "WHERE c.track_id = ?1 ORDER BY c.role, c.position;",
         [trackId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, trackId); },
         [&credits](sqlite3_stmt* s) {
             credits.push_back({ sqlite3_column_int(s, 0), columnText(s, 1), (CreditRole) sqlite3_column_int(s, 2) });
         });
    return credits;
}

int Database::artistIdFor(const std::string& name) {
    const auto k = keyOf(name);
    if (k.empty()) return -1;

    const auto written = juce::String::fromUTF8(name.c_str()).trim().toStdString();
    int id = -1;
    std::string stored;
    each("SELECT id, name FROM artists WHERE name_key = ?1;", [&](sqlite3_stmt* s) { bindText(s, 1, k); },
         [&](sqlite3_stmt* s) { id = sqlite3_column_int(s, 0); stored = columnText(s, 1); });
    if (id != -1) {
        // La última forma confirmada es la que vale ("Rubby Perez" → "Rubby Pérez"): las
        // propuestas siguientes lo escriben así.
        if (stored != written)
            run("UPDATE artists SET name = ?1 WHERE id = ?2;", [&](sqlite3_stmt* s) {
                bindText(s, 1, written);
                sqlite3_bind_int(s, 2, id);
            });
        return id;
    }

    const bool ok = run("INSERT INTO artists (name, name_key, created_at) VALUES (?1, ?2, datetime('now', 'localtime'));",
                        [&](sqlite3_stmt* s) {
                            bindText(s, 1, written);
                            bindText(s, 2, k);
                        });
    return ok ? (int) sqlite3_last_insert_rowid(db) : -1;
}

bool Database::confirmTrack(int trackId, const CatalogEntry& entry) {
    Transaction t(db);
    bool ok = t.began && run("DELETE FROM track_credits WHERE track_id = ?1;",
                             [trackId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, trackId); });

    auto credit = [&](const std::string& name, CreditRole role, int position) {
        if (!ok || keyOf(name).empty()) return;
        const int artistId = artistIdFor(name);
        ok = artistId != -1 && run("INSERT OR IGNORE INTO track_credits (track_id, artist_id, role, position) "
                                   "VALUES (?1, ?2, ?3, ?4);", [&](sqlite3_stmt* s) {
            sqlite3_bind_int(s, 1, trackId);
            sqlite3_bind_int(s, 2, artistId);
            sqlite3_bind_int(s, 3, (int) role);
            sqlite3_bind_int(s, 4, position);
        });
    };
    credit(entry.artist, CreditRole::Main, 0);
    for (size_t i = 0; i < entry.featured.size(); ++i) credit(entry.featured[i], CreditRole::Featured, (int) i);
    for (size_t i = 0; i < entry.composers.size(); ++i) credit(entry.composers[i], CreditRole::Composer, (int) i);

    // Otra forma de escribir al artista (la que traía el archivo) queda como alias, salvo que
    // sea el nombre de otro artista.
    const auto mainKey = keyOf(entry.artist);
    if (ok && !entry.aliasKey.empty() && entry.aliasKey != mainKey && !mainKey.empty()) {
        ok = run("INSERT OR REPLACE INTO artist_aliases (alias_key, artist_id) "
                 "SELECT ?1, id FROM artists WHERE name_key = ?2 "
                 "AND NOT EXISTS (SELECT 1 FROM artists WHERE name_key = ?1);", [&](sqlite3_stmt* s) {
            bindText(s, 1, entry.aliasKey);
            bindText(s, 2, mainKey);
        });
    }

    const auto matchKey = TrackNaming::matchKey(juce::String::fromUTF8(entry.artist.c_str()),
                                                juce::String::fromUTF8(entry.title.c_str())).toStdString();
    const auto storedPath = toStoredPath(entry.path);
    ok = ok && run("UPDATE tracks SET artist = ?1, title = ?2, version = ?3, year = ?4, status = 0, "
                   "confirmed_at = datetime('now', 'localtime'), review_draft = '', match_key = ?5, path = ?6, "
                   "updated_at = datetime('now', 'localtime') WHERE id = ?7;", [&](sqlite3_stmt* s) {
        bindText(s, 1, entry.displayArtist);
        bindText(s, 2, entry.displayTitle);
        bindText(s, 3, entry.version);
        sqlite3_bind_int(s, 4, entry.year);
        bindText(s, 5, matchKey);
        bindText(s, 6, storedPath);
        sqlite3_bind_int(s, 7, trackId);
    });
    const bool committed = t.commit(ok && sqlite3_changes(db) > 0);
    if (committed) indexTrack(trackId);
    return committed;
}

bool Database::setTrackNames(int trackId, const std::string& artist, const std::string& title) {
    const auto matchKey = TrackNaming::matchKey(juce::String::fromUTF8(artist.c_str()),
                                                juce::String::fromUTF8(title.c_str())).toStdString();
    const bool ok = run("UPDATE tracks SET artist = ?1, title = ?2, match_key = ?3, confirmed_at = datetime('now', 'localtime'), "
                        "updated_at = datetime('now', 'localtime') WHERE id = ?4;", [&](sqlite3_stmt* s) {
        bindText(s, 1, artist);
        bindText(s, 2, title);
        bindText(s, 3, matchKey);
        sqlite3_bind_int(s, 4, trackId);
    }) && sqlite3_changes(db) > 0;
    if (ok) indexTrack(trackId);
    return ok;
}

bool Database::setTrackPath(int trackId, const std::string& path) {
    const auto storedPath = toStoredPath(path);
    return run("UPDATE tracks SET path = ?1 WHERE id = ?2;", [&](sqlite3_stmt* s) {
        bindText(s, 1, storedPath);
        sqlite3_bind_int(s, 2, trackId);
    }) && sqlite3_changes(db) > 0;
}

TrackInfo Database::findDuplicate(const std::string& matchKey, double duration, double toleranceSeconds, int excludeId) {
    if (matchKey.empty() || matchKey == "|") return notFound();
    const auto where = std::string("WHERE t.match_key = ?1 AND abs(t.duration - ?2) <= ?3 AND t.id <> ?4 AND ")
                     + isMusic + " ORDER BY t.id LIMIT 1";
    const auto found = queryTracks(where, [&](sqlite3_stmt* s) {
        bindText(s, 1, matchKey);
        sqlite3_bind_double(s, 2, duration);
        sqlite3_bind_double(s, 3, toleranceSeconds);
        sqlite3_bind_int(s, 4, excludeId);
    });
    return found.empty() ? notFound() : found.front();
}

std::vector<Database::Placement> Database::getConfirmedMusicPlacement() {
    std::vector<Placement> rows;
    const auto sql = std::string("SELECT t.id, t.path, a.name, t.artist, t.title FROM tracks t "
                                 "JOIN track_credits c ON c.track_id = t.id AND c.role = 1 AND c.position = 0 "
                                 "JOIN artists a ON a.id = c.artist_id "
                                 "WHERE t.status = 0 AND t.confirmed_at <> '' AND ") + isMusic + ";";
    each(sql.c_str(), {}, [this, &rows](sqlite3_stmt* s) {
        rows.push_back({ sqlite3_column_int(s, 0), toAbsolutePath(columnText(s, 1)), columnText(s, 2),
                         columnText(s, 3), columnText(s, 4) });
    });
    return rows;
}

std::vector<Database::KeyRow> Database::getMusicKeys() {
    std::vector<KeyRow> rows;
    const auto sql = std::string("SELECT t.id, t.match_key, t.artist, t.title, t.duration FROM tracks t "
                                 "WHERE t.match_key <> '' AND ") + isMusic + ";";
    each(sql.c_str(), {}, [&rows](sqlite3_stmt* s) {
        rows.push_back({ sqlite3_column_int(s, 0), columnText(s, 1), columnText(s, 2), columnText(s, 3),
                         sqlite3_column_double(s, 4) });
    });
    return rows;
}

//==============================================================================
// Búsqueda

namespace {
    // Lo que se indexa de una pista: su texto, los nombres de sus créditos, versión, año, el
    // nombre original y las etiquetas, y las claves.
    const char* const searchIndexSelect =
        "SELECT t.id, t.artist, t.title, "
        "COALESCE((SELECT group_concat(a.name, ' ') FROM track_credits c JOIN artists a ON a.id = c.artist_id "
        "WHERE c.track_id = t.id), ''), "
        "t.version || ' ' || CASE WHEN t.year > 0 THEN t.year ELSE '' END || ' ' || t.source_name || ' ' "
        "|| t.tag_artist || ' ' || t.tag_title, "
        "replace(t.match_key, '|', ' ') FROM tracks t";
}

void Database::indexTrack(int trackId) {
    if (!ftsAvailable) return;
    auto bindId = [trackId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, trackId); };
    run("DELETE FROM tracks_fts WHERE rowid = ?1;", bindId);
    const auto sql = std::string("INSERT INTO tracks_fts (rowid, artist, title, people, extra, keys) ")
                   + searchIndexSelect + " WHERE t.id = ?1;";
    run(sql.c_str(), bindId);
}

void Database::rebuildSearchIndex() {
    if (!ftsAvailable) return;
    Transaction t(db);
    const auto sql = std::string("INSERT INTO tracks_fts (rowid, artist, title, people, extra, keys) ") + searchIndexSelect + ";";
    t.commit(execute("DELETE FROM tracks_fts;") && execute(sql));
}

void Database::ensureSearchIndex() {
    if (!ftsAvailable) return;
    int indexed = -1, tracks = -1;
    each("SELECT (SELECT COUNT(*) FROM tracks_fts), (SELECT COUNT(*) FROM tracks);", {}, [&](sqlite3_stmt* s) {
        indexed = sqlite3_column_int(s, 0);
        tracks = sqlite3_column_int(s, 1);
    });
    if (indexed == tracks) return;
    juce::Logger::writeToLog("Database: rebuilding the search index (" + juce::String(tracks) + " tracks)");
    rebuildSearchIndex();
}

//==============================================================================
// Intérpretes y artistas

std::unordered_map<int, std::vector<std::string>> Database::getPerformerKeys() {
    std::unordered_map<int, std::vector<std::string>> performers;
    each("SELECT c.track_id, a.name_key FROM track_credits c JOIN artists a ON a.id = c.artist_id "
         "WHERE c.role IN (1, 2) ORDER BY c.track_id, c.role, c.position;", {},
         [&performers](sqlite3_stmt* s) { performers[sqlite3_column_int(s, 0)].push_back(columnText(s, 1)); });
    return performers;
}

std::vector<Database::ArtistOverview> Database::getArtistsOverview() {
    std::vector<ArtistOverview> rows;
    std::unordered_map<int, size_t> byId;
    each("SELECT a.id, a.name, a.name_key, SUM(CASE WHEN c.role = 1 THEN 1 ELSE 0 END), "
         "SUM(CASE WHEN c.role = 2 THEN 1 ELSE 0 END), SUM(CASE WHEN c.role = 3 THEN 1 ELSE 0 END) "
         "FROM artists a JOIN track_credits c ON c.artist_id = a.id GROUP BY a.id;", {},
         [&](sqlite3_stmt* s) {
             ArtistOverview o;
             o.id = sqlite3_column_int(s, 0);
             o.name = columnText(s, 1);
             o.key = columnText(s, 2);
             o.main = sqlite3_column_int(s, 3);
             o.featured = sqlite3_column_int(s, 4);
             o.composer = sqlite3_column_int(s, 5);
             byId[o.id] = rows.size();
             rows.push_back(std::move(o));
         });
    each("SELECT artist_id, alias_key FROM artist_aliases ORDER BY alias_key;", {}, [&](sqlite3_stmt* s) {
        const auto it = byId.find(sqlite3_column_int(s, 0));
        if (it != byId.end()) rows[it->second].aliases.push_back(columnText(s, 1));
    });
    return rows;
}

std::vector<int> Database::getTracksOfArtist(int artistId) {
    std::vector<int> ids;
    each("SELECT DISTINCT track_id FROM track_credits WHERE artist_id = ?1 ORDER BY track_id;",
         [artistId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, artistId); },
         [&ids](sqlite3_stmt* s) { ids.push_back(sqlite3_column_int(s, 0)); });
    return ids;
}

bool Database::renameArtist(int artistId, const std::string& newName, int& conflictId) {
    conflictId = -1;
    const auto written = juce::String::fromUTF8(newName.c_str()).trim().toStdString();
    const auto newKey = keyOf(written);
    if (newKey.empty()) return false;

    std::string oldKey;
    bool exists = false;
    each("SELECT name_key FROM artists WHERE id = ?1;", [artistId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, artistId); },
         [&](sqlite3_stmt* s) { oldKey = columnText(s, 0); exists = true; });
    if (!exists) return false;

    if (newKey != oldKey) {
        each("SELECT id FROM artists WHERE name_key = ?1 AND id <> ?2;", [&](sqlite3_stmt* s) {
                 bindText(s, 1, newKey);
                 sqlite3_bind_int(s, 2, artistId);
             },
             [&conflictId](sqlite3_stmt* s) { conflictId = sqlite3_column_int(s, 0); });
        if (conflictId != -1) return false;  // es otro artista: se decide si fusionarlos
    }

    Transaction t(db);
    bool ok = run("UPDATE artists SET name = ?1, name_key = ?2 WHERE id = ?3;", [&](sqlite3_stmt* s) {
        bindText(s, 1, written);
        bindText(s, 2, newKey);
        sqlite3_bind_int(s, 3, artistId);
    });
    if (newKey != oldKey) {
        // El nombre nuevo deja de ser alias de nadie; el anterior queda como alias de este.
        ok = ok && run("DELETE FROM artist_aliases WHERE alias_key = ?1;", [&](sqlite3_stmt* s) { bindText(s, 1, newKey); });
        ok = ok && run("INSERT OR REPLACE INTO artist_aliases (alias_key, artist_id) VALUES (?1, ?2);", [&](sqlite3_stmt* s) {
            bindText(s, 1, oldKey);
            sqlite3_bind_int(s, 2, artistId);
        });
    }
    return t.commit(ok);
}

bool Database::mergeArtists(int fromId, int intoId) {
    if (fromId == intoId) return false;
    std::string fromKey;
    int found = 0;
    each("SELECT id, name_key FROM artists WHERE id IN (?1, ?2);", [&](sqlite3_stmt* s) {
             sqlite3_bind_int(s, 1, fromId);
             sqlite3_bind_int(s, 2, intoId);
         },
         [&](sqlite3_stmt* s) {
             ++found;
             if (sqlite3_column_int(s, 0) == fromId) fromKey = columnText(s, 1);
         });
    if (found != 2) return false;

    auto bindBoth = [&](sqlite3_stmt* s) {
        sqlite3_bind_int(s, 1, fromId);
        sqlite3_bind_int(s, 2, intoId);
    };
    Transaction t(db);
    // Una pista que ya tenía a los dos en el mismo rol se queda con uno.
    bool ok = run("DELETE FROM track_credits WHERE artist_id = ?1 AND EXISTS (SELECT 1 FROM track_credits c2 "
                  "WHERE c2.track_id = track_credits.track_id AND c2.role = track_credits.role AND c2.artist_id = ?2);", bindBoth)
           && run("UPDATE track_credits SET artist_id = ?2 WHERE artist_id = ?1;", bindBoth)
           && run("UPDATE artist_aliases SET artist_id = ?2 WHERE artist_id = ?1;", bindBoth)
           && run("INSERT OR REPLACE INTO artist_aliases (alias_key, artist_id) VALUES (?1, ?2);", [&](sqlite3_stmt* s) {
                  bindText(s, 1, fromKey);
                  sqlite3_bind_int(s, 2, intoId);
              })
           && run("DELETE FROM artists WHERE id = ?1;", [fromId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, fromId); });
    return t.commit(ok);
}

bool Database::removeAlias(const std::string& aliasKey) {
    return run("DELETE FROM artist_aliases WHERE alias_key = ?1;", [&](sqlite3_stmt* s) { bindText(s, 1, aliasKey); })
        && sqlite3_changes(db) > 0;
}

bool Database::setTrackArtistLine(int trackId, const std::string& artist, const std::string& matchKey) {
    const bool ok = run("UPDATE tracks SET artist = ?1, match_key = ?2, updated_at = datetime('now', 'localtime') WHERE id = ?3;",
                        [&](sqlite3_stmt* s) {
                            bindText(s, 1, artist);
                            bindText(s, 2, matchKey);
                            sqlite3_bind_int(s, 3, trackId);
                        });
    if (ok) indexTrack(trackId);
    return ok;
}

//==============================================================================
// Lotes de importación

std::vector<Database::BatchInfo> Database::getImportBatches() {
    std::vector<BatchInfo> batches;
    each("SELECT b.id, b.created_at, b.source, b.files, "
         "(SELECT COUNT(*) FROM tracks t WHERE t.import_batch = b.id AND t.status = 1), "
         "(SELECT COUNT(*) FROM tracks t WHERE t.import_batch = b.id AND t.status = 0) "
         "FROM import_batches b ORDER BY b.id DESC;", {}, [&batches](sqlite3_stmt* s) {
        BatchInfo b;
        b.id = sqlite3_column_int(s, 0);
        b.createdAt = columnText(s, 1);
        b.source = columnText(s, 2);
        b.files = sqlite3_column_int(s, 3);
        b.pending = sqlite3_column_int(s, 4);
        b.confirmed = sqlite3_column_int(s, 5);
        batches.push_back(std::move(b));
    });
    return batches;
}

std::vector<int> Database::getPendingTracksOfBatch(int batchId) {
    std::vector<int> ids;
    each("SELECT id FROM tracks WHERE import_batch = ?1 AND status = 1 ORDER BY id;",
         [batchId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, batchId); },
         [&ids](sqlite3_stmt* s) { ids.push_back(sqlite3_column_int(s, 0)); });
    return ids;
}

//==============================================================================
// Huella acústica

bool Database::setFingerprint(int trackId, const std::vector<uint32_t>& fingerprint, int version) {
    std::vector<uint8_t> bytes(fingerprint.size() * 4);
    for (size_t i = 0; i < fingerprint.size(); ++i)
        for (int b = 0; b < 4; ++b)
            bytes[4 * i + (size_t) b] = (uint8_t) (fingerprint[i] >> (8 * b));
    return run("UPDATE tracks SET fingerprint = ?1, fingerprint_version = ?2 WHERE id = ?3;", [&](sqlite3_stmt* s) {
        sqlite3_bind_blob(s, 1, bytes.data(), (int) bytes.size(), SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 2, version);
        sqlite3_bind_int(s, 3, trackId);
    });
}

std::vector<Database::FingerprintRow> Database::getFingerprintsNear(double duration, double toleranceSeconds) {
    std::vector<FingerprintRow> rows;
    const auto sql = std::string("SELECT t.id, t.artist, t.title, t.duration, t.fingerprint FROM tracks t "
                                 "WHERE t.duration BETWEEN ?1 AND ?2 AND length(t.fingerprint) > 0 AND ") + isMusic + ";";
    each(sql.c_str(), [&](sqlite3_stmt* s) {
             sqlite3_bind_double(s, 1, duration - toleranceSeconds);
             sqlite3_bind_double(s, 2, duration + toleranceSeconds);
         },
         [&rows](sqlite3_stmt* s) {
             rows.push_back({ sqlite3_column_int(s, 0), columnText(s, 1), columnText(s, 2), sqlite3_column_double(s, 3),
                              blobToFingerprint(s, 4) });
         });
    return rows;
}

std::vector<int> Database::getTrackIdsNeedingFingerprint(int version) {
    std::vector<int> ids;
    // Lo que está por revisar primero: la revisión busca duplicados con la huella.
    const auto sql = std::string("SELECT t.id FROM tracks t WHERE t.fingerprint_version < ?1 AND t.analysis_version > 0 AND ")
                   + isMusic + " ORDER BY t.status DESC, t.id DESC;";
    each(sql.c_str(), [version](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, version); },
         [&ids](sqlite3_stmt* s) { ids.push_back(sqlite3_column_int(s, 0)); });
    return ids;
}

//==============================================================================
// Identificación en línea

namespace {
    // Listas de nombres en una columna: uno por línea.
    std::string joinLines(const std::vector<std::string>& names) {
        std::string text;
        for (const auto& n : names) text += (text.empty() ? "" : "\n") + n;
        return text;
    }

    std::vector<std::string> splitLines(const std::string& text) {
        std::vector<std::string> names;
        for (const auto& line : juce::StringArray::fromLines(juce::String::fromUTF8(text.c_str())))
            if (line.isNotEmpty()) names.push_back(line.toStdString());
        return names;
    }
}

bool Database::saveOnlineMatch(int trackId, const OnlineMatch& match) {
    return run("INSERT OR REPLACE INTO online_matches (track_id, status, score, recording_id, artist, featured, title, "
               "year, composers, isrc, error, checked_at) "
               "SELECT ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, datetime('now', 'localtime') "
               "WHERE EXISTS (SELECT 1 FROM tracks WHERE id = ?1);", [&](sqlite3_stmt* s) {
        sqlite3_bind_int(s, 1, trackId);
        sqlite3_bind_int(s, 2, (int) match.status);
        sqlite3_bind_double(s, 3, match.score);
        bindText(s, 4, match.recordingId);
        bindText(s, 5, match.artist);
        bindText(s, 6, joinLines(match.featured));
        bindText(s, 7, match.title);
        sqlite3_bind_int(s, 8, match.year);
        bindText(s, 9, joinLines(match.composers));
        bindText(s, 10, match.isrc);
        bindText(s, 11, match.error);
    });
}

std::unordered_map<int, Database::OnlineMatch> Database::getOnlineMatches() {
    std::unordered_map<int, OnlineMatch> matches;
    each("SELECT track_id, status, score, recording_id, artist, featured, title, year, composers, isrc, error "
         "FROM online_matches;", {}, [&matches](sqlite3_stmt* s) {
        OnlineMatch m;
        m.status = (OnlineStatus) sqlite3_column_int(s, 1);
        m.score = sqlite3_column_double(s, 2);
        m.recordingId = columnText(s, 3);
        m.artist = columnText(s, 4);
        m.featured = splitLines(columnText(s, 5));
        m.title = columnText(s, 6);
        m.year = sqlite3_column_int(s, 7);
        m.composers = splitLines(columnText(s, 8));
        m.isrc = columnText(s, 9);
        m.error = columnText(s, 10);
        matches[sqlite3_column_int(s, 0)] = std::move(m);
    });
    return matches;
}

std::vector<int> Database::getTracksNeedingOnlineLookup() {
    std::vector<int> ids;
    const auto sql = std::string("SELECT t.id FROM tracks t LEFT JOIN online_matches m ON m.track_id = t.id "
                                 "WHERE (m.track_id IS NULL OR m.status = 3) "
                                 "AND (t.status = 1 OR (t.confirmed_at = '' AND ") + isMusic + ")) "
                                 "ORDER BY t.status DESC, t.id;";
    each(sql.c_str(), {}, [&ids](sqlite3_stmt* s) { ids.push_back(sqlite3_column_int(s, 0)); });
    return ids;
}

bool Database::setOnlineIds(int trackId, const std::string& recordingId, const std::string& isrc) {
    return run("UPDATE tracks SET mb_recording_id = ?1, isrc = ?2 WHERE id = ?3;", [&](sqlite3_stmt* s) {
        bindText(s, 1, recordingId);
        bindText(s, 2, isrc);
        sqlite3_bind_int(s, 3, trackId);
    });
}

std::pair<std::string, std::string> Database::getOnlineIds(int trackId) {
    std::pair<std::string, std::string> ids;
    each("SELECT mb_recording_id, isrc FROM tracks WHERE id = ?1;", [trackId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, trackId); },
         [&ids](sqlite3_stmt* s) { ids = { columnText(s, 0), columnText(s, 1) }; });
    return ids;
}

//==============================================================================
// Descargas

int Database::startDownload(const std::string& videoId, long long deezerId, const std::string& artist, const std::string& title) {
    const bool ok = run("INSERT INTO downloads (video_id, deezer_id, artist, title, status, started_at) "
                        "VALUES (?1, ?2, ?3, ?4, 0, datetime('now', 'localtime'));", [&](sqlite3_stmt* s) {
        bindText(s, 1, videoId);
        sqlite3_bind_int64(s, 2, deezerId);
        bindText(s, 3, artist);
        bindText(s, 4, title);
    });
    return ok ? (int) sqlite3_last_insert_rowid(db) : -1;
}

bool Database::finishDownload(int id, DownloadStatus status, int trackId, const std::string& error) {
    return run("UPDATE downloads SET status = ?1, track_id = ?2, error = ?3, finished_at = datetime('now', 'localtime') "
               "WHERE id = ?4;", [&](sqlite3_stmt* s) {
        sqlite3_bind_int(s, 1, (int) status);
        sqlite3_bind_int(s, 2, trackId);
        bindText(s, 3, error);
        sqlite3_bind_int(s, 4, id);
    });
}

std::vector<Database::DownloadRecord> Database::getRecentDownloads(int limit) {
    std::vector<DownloadRecord> rows;
    each("SELECT id, video_id, deezer_id, artist, title, status, track_id, error, started_at, finished_at "
         "FROM downloads ORDER BY id DESC LIMIT ?1;", [limit](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, limit); },
         [&rows](sqlite3_stmt* s) {
        DownloadRecord r;
        r.id = sqlite3_column_int(s, 0);
        r.videoId = columnText(s, 1);
        r.deezerId = sqlite3_column_int64(s, 2);
        r.artist = columnText(s, 3);
        r.title = columnText(s, 4);
        r.status = (DownloadStatus) sqlite3_column_int(s, 5);
        r.trackId = sqlite3_column_int(s, 6);
        r.error = columnText(s, 7);
        r.startedAt = columnText(s, 8);
        r.finishedAt = columnText(s, 9);
        rows.push_back(std::move(r));
    });
    return rows;
}

std::vector<std::string> Database::getDownloadStartsSince(int minutes) {
    std::vector<std::string> starts;
    each("SELECT started_at FROM downloads WHERE started_at >= datetime('now', 'localtime', ?1) ORDER BY started_at;",
         [minutes](sqlite3_stmt* s) { bindText(s, 1, "-" + std::to_string(minutes) + " minutes"); },
         [&starts](sqlite3_stmt* s) { starts.push_back(columnText(s, 0)); });
    return starts;
}
