#include <JuceHeader.h>
#include "Database.h"
#include "../library/TrackNaming.h"

Database::Database() {}

Database::~Database() {
    close();
}

bool Database::open(const std::string& dbPath) {
    int rc = sqlite3_open(dbPath.c_str(), &db);
    if (rc) {
        std::cerr << "Can't open database: " << sqlite3_errmsg(db) << "\n";
        return false;
    }
    // Resistente a cortes de luz: diario clásico (no WAL, que falla en discos de red y en
    // algunos USB) y cada transacción escrita en disco antes de darla por hecha.
    sqlite3_exec(db, "PRAGMA journal_mode = DELETE; PRAGMA synchronous = FULL; PRAGMA busy_timeout = 5000;",
                 nullptr, nullptr, nullptr);
    return true;
}

void Database::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

bool Database::execute(const std::string& sql) {
    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL error: " << zErrMsg << " in SQL: " << sql << "\n";
        sqlite3_free(zErrMsg);
        return false;
    }
    return true;
}

bool Database::initSchema() {
    std::string sqlTracks =
        "CREATE TABLE IF NOT EXISTS tracks ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "path TEXT UNIQUE, "
        "artist TEXT, "
        "title TEXT, "
        "play_count INTEGER DEFAULT 0, "
        "duration REAL DEFAULT 0.0, "
        "last_played TEXT DEFAULT '',"
        "normalization_gain REAL DEFAULT 1.0,"
        "created_at TEXT DEFAULT '',"
        "updated_at TEXT DEFAULT '',"
        "loudness_lufs REAL DEFAULT -70.0,"
        "true_peak_db REAL DEFAULT -70.0,"
        "cue_in REAL DEFAULT 0.0,"
        "cue_out REAL DEFAULT 0.0,"
        "mix_point REAL DEFAULT 0.0,"
        "analysis_version INTEGER DEFAULT 0,"
        "file_hash TEXT DEFAULT '',"
        "status INTEGER DEFAULT 0,"
        "version TEXT DEFAULT '',"
        "year INTEGER DEFAULT 0,"
        "confirmed_at TEXT DEFAULT '',"
        "match_key TEXT DEFAULT '',"
        "source_name TEXT DEFAULT '',"
        "source_folder TEXT DEFAULT '',"
        "tag_artist TEXT DEFAULT '',"
        "tag_title TEXT DEFAULT '',"
        "import_batch INTEGER DEFAULT 0,"
        "review_draft TEXT DEFAULT '',"
        "fingerprint BLOB,"
        "fingerprint_version INTEGER DEFAULT 0,"
        "mb_recording_id TEXT DEFAULT '',"
        "isrc TEXT DEFAULT ''"
        ");";

    if (!execute(sqlTracks)) return false;
    execute("CREATE INDEX IF NOT EXISTS idx_artist_title ON tracks(artist, title);");

    // Columnas agregadas en versiones posteriores: en una base de datos vieja se crean
    // aquí (si ya existen, SQLite devuelve un error que se ignora).
    for (const char* column : { "last_played TEXT DEFAULT ''",
                                "normalization_gain REAL DEFAULT 1.0",
                                "created_at TEXT DEFAULT ''",
                                "updated_at TEXT DEFAULT ''",
                                "loudness_lufs REAL DEFAULT -70.0",
                                "true_peak_db REAL DEFAULT -70.0",
                                "cue_in REAL DEFAULT 0.0",
                                "cue_out REAL DEFAULT 0.0",
                                "analysis_version INTEGER DEFAULT 0",
                                "file_hash TEXT DEFAULT ''",
                                // Esquema 3: catálogo y revisión de lo importado.
                                "status INTEGER DEFAULT 0",
                                "version TEXT DEFAULT ''",
                                "year INTEGER DEFAULT 0",
                                "confirmed_at TEXT DEFAULT ''",
                                "match_key TEXT DEFAULT ''",
                                "source_name TEXT DEFAULT ''",
                                "source_folder TEXT DEFAULT ''",
                                "tag_artist TEXT DEFAULT ''",
                                "tag_title TEXT DEFAULT ''",
                                "import_batch INTEGER DEFAULT 0",
                                "review_draft TEXT DEFAULT ''",
                                "fingerprint BLOB",
                                "fingerprint_version INTEGER DEFAULT 0",
                                "mb_recording_id TEXT DEFAULT ''",
                                "isrc TEXT DEFAULT ''",
                                "mix_point REAL DEFAULT 0.0" }) {
        const std::string sql = std::string("ALTER TABLE tracks ADD COLUMN ") + column + ";";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    }
    execute("CREATE INDEX IF NOT EXISTS idx_file_hash ON tracks(file_hash);");
    execute("CREATE INDEX IF NOT EXISTS idx_match_key ON tracks(match_key);");
    execute("CREATE INDEX IF NOT EXISTS idx_track_status ON tracks(status);");
    execute("CREATE INDEX IF NOT EXISTS idx_track_duration ON tracks(duration);");
    execute("CREATE INDEX IF NOT EXISTS idx_track_batch ON tracks(import_batch);");

    std::string sqlCats =
        "CREATE TABLE IF NOT EXISTS categories ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "name TEXT UNIQUE, "
        "sort_order INTEGER DEFAULT 0,"
        "folder_format INTEGER DEFAULT 1,"
        "system_role INTEGER DEFAULT 0"  // CategoryRole; 0 = categoría del usuario
        ");";
    if (!execute(sqlCats)) return false;

    for (const char* column : { "folder_format INTEGER DEFAULT 1", "system_role INTEGER DEFAULT 0" }) {
        const std::string sql = std::string("ALTER TABLE categories ADD COLUMN ") + column + ";";
        sqlite3_exec(db, sql.c_str(), nullptr, nullptr, nullptr);
    }
    execute("CREATE UNIQUE INDEX IF NOT EXISTS idx_category_role ON categories(system_role) WHERE system_role > 0;");

    std::string sqlTrackCats =
        "CREATE TABLE IF NOT EXISTS track_categories ("
        "track_id INTEGER, "
        "category_id INTEGER, "
        "PRIMARY KEY (track_id, category_id), "
        "FOREIGN KEY(track_id) REFERENCES tracks(id) ON DELETE CASCADE, "
        "FOREIGN KEY(category_id) REFERENCES categories(id) ON DELETE CASCADE"
        ");";
    if (!execute(sqlTrackCats)) return false;

    // Versión del esquema (PRAGMA user_version) para las migraciones que se hacen una vez.
    int schemaVersion = 0;
    sqlite3_stmt* versionStmt;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &versionStmt, nullptr) == SQLITE_OK) {
        if (sqlite3_step(versionStmt) == SQLITE_ROW) schemaVersion = sqlite3_column_int(versionStmt, 0);
        sqlite3_finalize(versionStmt);
    }

    ensureSystemCategories();
    if (schemaVersion < 1) {
        removeLegacyDefaultCategories();
        execute("PRAGMA user_version = 1;");
    }

    // Registro de emisión: una fila por pista que salió al aire.
    std::string sqlPlayLog =
        "CREATE TABLE IF NOT EXISTS play_log ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "played_at TEXT NOT NULL, "
        "track_id INTEGER, "
        "artist TEXT, "
        "title TEXT, "
        "duration REAL DEFAULT 0.0, "
        "deck TEXT"
        ");";
    if (!execute(sqlPlayLog)) return false;
    execute("CREATE INDEX IF NOT EXISTS idx_play_log_played_at ON play_log(played_at);");

    createStationTables();
    createCatalogTables();

    // Claves foráneas: la pauta, la cola y la botonera no pueden apuntar a una pista borrada.
    // Antes se limpian las filas huérfanas que dejaran versiones anteriores.
    execute("DELETE FROM track_categories WHERE track_id NOT IN (SELECT id FROM tracks) "
            "OR category_id NOT IN (SELECT id FROM categories);");
    return execute("PRAGMA foreign_keys = ON;");
}

//==============================================================================
// Rutas portables

void Database::setPathBase(const juce::File& labRoot) {
    pathBase = labRoot;
}

std::string Database::toStoredPath(const std::string& path) const {
    if (pathBase == juce::File() || path.empty()) return path;

    const auto p = juce::String::fromUTF8(path.data(), (int) path.size());
    if (!juce::File::isAbsolutePath(p)) return path;

    const juce::File file(p);
    if (!file.isAChildOf(pathBase)) return path;

    // Barras normales para que la misma base de datos sirva en Windows, macOS y Linux.
    return file.getRelativePathFrom(pathBase).replaceCharacter('\\', '/').toStdString();
}

std::string Database::toAbsolutePath(const std::string& storedPath) const {
    if (pathBase == juce::File() || storedPath.empty()) return storedPath;

    const auto p = juce::String::fromUTF8(storedPath.data(), (int) storedPath.size());
    if (juce::File::isAbsolutePath(p)) return storedPath;

    return pathBase.getChildFile(p).getFullPathName().toStdString();
}

int Database::migrateToRelativePaths(const std::function<void()>& beforeWriting) {
    if (pathBase == juce::File()) return 0;

    std::vector<std::pair<int, std::string>> updates;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id, path FROM tracks;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const int id = sqlite3_column_int(stmt, 0);
            auto text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (text == nullptr) continue;

            const auto stored = juce::String::fromUTF8(text);
            if (!juce::File::isAbsolutePath(stored)) continue;

            const juce::File file(stored);
            juce::String relative;

            if (file.isAChildOf(pathBase)) {
                relative = file.getRelativePathFrom(pathBase);
            } else if (!file.existsAsFile()) {
                // El laboratorio se movió (otra letra de unidad, otro equipo): se toma el
                // tramo desde la carpeta Library y se usa si existe en el laboratorio actual.
                const auto normalized = stored.replaceCharacter('\\', '/');
                const int index = normalized.lastIndexOfIgnoreCase("/Library/");
                if (index >= 0) {
                    const auto candidate = normalized.substring(index + 1);
                    if (pathBase.getChildFile(candidate).existsAsFile())
                        relative = candidate;
                }
            }

            if (relative.isNotEmpty())
                updates.push_back({ id, relative.replaceCharacter('\\', '/').toStdString() });
        }
        sqlite3_finalize(stmt);
    }

    if (updates.empty()) return 0;
    if (beforeWriting) beforeWriting();

    int changed = 0;
    execute("BEGIN TRANSACTION;");
    sqlite3_stmt* update;
    if (sqlite3_prepare_v2(db, "UPDATE OR IGNORE tracks SET path = ? WHERE id = ?;", -1, &update, nullptr) == SQLITE_OK) {
        for (const auto& [id, path] : updates) {
            sqlite3_bind_text(update, 1, path.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(update, 2, id);
            if (sqlite3_step(update) == SQLITE_DONE && sqlite3_changes(db) > 0)
                ++changed;
            sqlite3_reset(update);
        }
        sqlite3_finalize(update);
    }
    execute("COMMIT;");
    return changed;
}

//==============================================================================
// Registro de emisión

bool Database::addPlayLogEntry(const PlayLogEntry& entry) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO play_log (played_at, track_id, artist, title, duration, deck) VALUES (?, ?, ?, ?, ?, ?);";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, entry.playedAt.c_str(), -1, SQLITE_TRANSIENT);
    if (entry.trackId != -1) sqlite3_bind_int(stmt, 2, entry.trackId);
    else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, entry.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, entry.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, entry.duration);
    sqlite3_bind_text(stmt, 6, entry.deck.c_str(), -1, SQLITE_TRANSIENT);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<PlayLogEntry> Database::getRecentPlayLog(int limit) {
    std::vector<PlayLogEntry> entries;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT played_at, track_id, artist, title, duration, deck FROM play_log ORDER BY id DESC LIMIT ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return entries;

    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto text = [stmt](int col) {
            auto t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
            return std::string(t ? t : "");
        };
        PlayLogEntry e;
        e.playedAt = text(0);
        e.trackId = sqlite3_column_type(stmt, 1) == SQLITE_NULL ? -1 : sqlite3_column_int(stmt, 1);
        e.artist = text(2);
        e.title = text(3);
        e.duration = sqlite3_column_double(stmt, 4);
        e.deck = text(5);
        entries.push_back(e);
    }
    sqlite3_finalize(stmt);
    return entries;
}

//==============================================================================

// Columnas de una pista, en el orden que espera readTrackRow().
static const char* const trackColumns =
    "t.id, t.path, t.artist, t.title, t.play_count, t.duration, t.last_played, t.normalization_gain, "
    "t.created_at, t.updated_at, t.loudness_lufs, t.true_peak_db, t.cue_in, t.cue_out, "
    "t.analysis_version, t.file_hash, t.status, t.version, t.year, t.confirmed_at, t.mix_point, "
    "(SELECT MAX(c.system_role) FROM track_categories tc JOIN categories c ON c.id = tc.category_id "
    "WHERE tc.track_id = t.id)";

TrackInfo Database::readTrackRow(sqlite3_stmt* stmt) const {
    auto text = [stmt](int col) {
        auto t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return std::string(t ? t : "");
    };

    TrackInfo t {};
    t.id = sqlite3_column_int(stmt, 0);
    t.path = toAbsolutePath(text(1));
    t.artist = text(2);
    t.title = text(3);
    t.playCount = sqlite3_column_int(stmt, 4);
    t.duration = sqlite3_column_double(stmt, 5);
    t.lastPlayed = text(6);
    const double gain = sqlite3_column_double(stmt, 7);
    t.normalizationGain = gain > 0.0 ? gain : 1.0;
    t.createdAt = text(8);
    t.updatedAt = text(9);
    t.loudnessLufs = sqlite3_column_double(stmt, 10);
    t.truePeakDb = sqlite3_column_double(stmt, 11);
    t.cueIn = sqlite3_column_double(stmt, 12);
    t.cueOut = sqlite3_column_double(stmt, 13);
    t.analysisVersion = sqlite3_column_int(stmt, 14);
    t.fileHash = text(15);
    t.status = sqlite3_column_int(stmt, 16);
    t.version = text(17);
    t.year = sqlite3_column_int(stmt, 18);
    t.confirmedAt = text(19);
    t.mixPoint = sqlite3_column_double(stmt, 20);
    t.systemRole = (CategoryRole) sqlite3_column_int(stmt, 21);  // NULL (sin categoría) = 0
    return t;
}

// Clave de la canción guardada: intérprete principal (sin invitados) y título (sin versión).
static std::string storedMatchKey(const std::string& artist, const std::string& title, const std::string& version) {
    const auto a = juce::String::fromUTF8(artist.c_str()).upToFirstOccurrenceOf(" feat. ", false, false);
    auto t = juce::String::fromUTF8(title.c_str());
    const auto v = juce::String::fromUTF8(version.c_str()).trim();
    if (v.isNotEmpty() && t.endsWith(" (" + v + ")")) t = t.dropLastCharacters(v.length() + 3);
    return TrackNaming::matchKey(a, t).toStdString();
}

std::vector<TrackInfo> Database::queryTracks(const std::string& whereClause,
                                             const std::function<void(sqlite3_stmt*)>& bind) {
    std::vector<TrackInfo> results;
    const std::string sql = std::string("SELECT ") + trackColumns + " FROM tracks t " + whereClause + ";";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "SQL error: " << sqlite3_errmsg(db) << " in SQL: " << sql << "\n";
        return results;
    }
    if (bind) bind(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        results.push_back(readTrackRow(stmt));
    sqlite3_finalize(stmt);
    return results;
}

static TrackInfo firstOrNotFound(const std::vector<TrackInfo>& tracks) {
    if (!tracks.empty()) return tracks.front();
    TrackInfo t {};
    t.id = -1;
    t.normalizationGain = 1.0;
    return t;
}

bool Database::addTrack(const TrackInfo& track) {
    const char* sql =
        "INSERT OR IGNORE INTO tracks (path, artist, title, duration, last_played, normalization_gain, "
        "created_at, updated_at, loudness_lufs, true_peak_db, cue_in, cue_out, analysis_version, file_hash, "
        "status, version, year, confirmed_at, match_key, mix_point) "
        "VALUES (?, ?, ?, ?, '', ?, datetime('now', 'localtime'), datetime('now', 'localtime'), ?, ?, ?, ?, ?, ?, "
        "?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    const std::string storedPath = toStoredPath(track.path);
    sqlite3_bind_text(stmt, 1, storedPath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, track.artist.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, track.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 4, track.duration);
    sqlite3_bind_double(stmt, 5, track.normalizationGain);
    sqlite3_bind_double(stmt, 6, track.loudnessLufs);
    sqlite3_bind_double(stmt, 7, track.truePeakDb);
    sqlite3_bind_double(stmt, 8, track.cueIn);
    sqlite3_bind_double(stmt, 9, track.cueOut);
    sqlite3_bind_int(stmt, 10, track.analysisVersion);
    sqlite3_bind_text(stmt, 11, track.fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 12, track.status);
    sqlite3_bind_text(stmt, 13, track.version.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 14, track.year);
    sqlite3_bind_text(stmt, 15, track.confirmedAt.c_str(), -1, SQLITE_TRANSIENT);
    const auto matchKey = storedMatchKey(track.artist, track.title, track.version);
    sqlite3_bind_text(stmt, 16, matchKey.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 17, track.mixPoint);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    const bool inserted = ok && sqlite3_changes(db) > 0;
    const auto id = (int) sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    if (inserted) indexTrack(id);
    return ok;
}

// Búsqueda FTS5: cada palabra (sin tildes ni signos) como prefijo, todas a la vez. "vives
// gota" encuentra "Carlos Vives - La Gota Fría"; "wilf" encuentra a Wilfrido Vargas.
static std::string ftsQuery(const std::string& query) {
    std::string expression;
    for (const auto& word : juce::StringArray::fromTokens(TrackNaming::key(juce::String::fromUTF8(query.c_str())), " ", ""))
        if (word.isNotEmpty()) expression += (expression.empty() ? "\"" : " \"") + word.toStdString() + "\"*";
    return expression;
}

std::vector<TrackInfo> Database::searchTracks(const std::string& query, const std::string& categoryFilter) {
    const bool filterUncategorized = (categoryFilter == "Uncategorized");
    const bool filterAll = (categoryFilter == "All Categories" || categoryFilter.empty());
    const std::string match = ftsAvailable && !query.empty() ? ftsQuery(query) : std::string();

    std::string joins, conditions = "1=1", order;
    if (filterUncategorized) {
        conditions += " AND t.id NOT IN (SELECT track_id FROM track_categories)";
    } else if (!filterAll) {
        joins += "JOIN track_categories tc ON t.id = tc.track_id JOIN categories c ON tc.category_id = c.id ";
        conditions += " AND c.name = ?1";
    }
    if (!match.empty()) {
        joins += "JOIN tracks_fts ON tracks_fts.rowid = t.id ";
        conditions += " AND tracks_fts MATCH ?4";
        order = " ORDER BY tracks_fts.rank";
    } else if (!query.empty()) {
        // Sin índice: también por la clave, que no distingue mayúsculas ni tildes.
        conditions += " AND (t.artist LIKE ?2 OR t.title LIKE ?2 OR t.match_key LIKE ?3)";
    }

    const std::string pattern = "%" + query + "%";
    const std::string keyPattern = "%" + TrackNaming::key(juce::String::fromUTF8(query.c_str())).toStdString() + "%";
    return queryTracks(joins + "WHERE " + conditions + order, [&](sqlite3_stmt* stmt) {
        if (!filterUncategorized && !filterAll)
            sqlite3_bind_text(stmt, 1, categoryFilter.c_str(), -1, SQLITE_TRANSIENT);
        if (!match.empty()) {
            sqlite3_bind_text(stmt, 4, match.c_str(), -1, SQLITE_TRANSIENT);
        } else if (!query.empty()) {
            sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, keyPattern.c_str(), -1, SQLITE_TRANSIENT);
        }
    });
}

TrackInfo Database::getTrackByPath(const std::string& path) {
    // También se prueba la ruta tal cual, por si alguna fila quedó sin migrar.
    const std::string storedPath = toStoredPath(path);
    return firstOrNotFound(queryTracks("WHERE t.path = ?1 OR t.path = ?2 LIMIT 1", [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, storedPath.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, path.c_str(), -1, SQLITE_TRANSIENT);
    }));
}

TrackInfo Database::getTrackById(int id) {
    return firstOrNotFound(queryTracks("WHERE t.id = ?1", [id](sqlite3_stmt* stmt) {
        sqlite3_bind_int(stmt, 1, id);
    }));
}

TrackInfo Database::findTrackByHash(const std::string& fileHash, int excludingCategory) {
    if (fileHash.empty()) return firstOrNotFound({});
    return firstOrNotFound(queryTracks("WHERE t.file_hash = ?1 AND t.id NOT IN "
                                       "(SELECT track_id FROM track_categories WHERE category_id = ?2) LIMIT 1",
                                       [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, fileHash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, excludingCategory);
    }));
}

TrackInfo Database::findTrackByHashInCategory(const std::string& fileHash, int categoryId) {
    if (fileHash.empty() || categoryId < 0) return firstOrNotFound({});
    return firstOrNotFound(queryTracks("JOIN track_categories tc ON tc.track_id = t.id "
                                       "WHERE t.file_hash = ?1 AND tc.category_id = ?2 LIMIT 1", [&](sqlite3_stmt* stmt) {
        sqlite3_bind_text(stmt, 1, fileHash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, categoryId);
    }));
}

TrackInfo Database::findTrackByMetadata(const std::string& artist, const std::string& title,
                                        double duration, double toleranceSeconds) {
    return findDuplicate(storedMatchKey(artist, title, {}), duration, toleranceSeconds, -1);
}

bool Database::updateTrackAnalysis(const TrackInfo& track) {
    const char* sql =
        "UPDATE tracks SET normalization_gain = ?1, loudness_lufs = ?2, true_peak_db = ?3, cue_in = ?4, "
        "cue_out = ?5, duration = ?6, analysis_version = ?7, "
        "file_hash = CASE WHEN ?8 = '' THEN file_hash ELSE ?8 END, mix_point = ?10 WHERE id = ?9;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_double(stmt, 1, track.normalizationGain);
    sqlite3_bind_double(stmt, 2, track.loudnessLufs);
    sqlite3_bind_double(stmt, 3, track.truePeakDb);
    sqlite3_bind_double(stmt, 4, track.cueIn);
    sqlite3_bind_double(stmt, 5, track.cueOut);
    sqlite3_bind_double(stmt, 6, track.duration);
    sqlite3_bind_int(stmt, 7, track.analysisVersion);
    sqlite3_bind_text(stmt, 8, track.fileHash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 9, track.id);
    sqlite3_bind_double(stmt, 10, track.mixPoint);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<int> Database::getTrackIdsNeedingAnalysis(int currentVersion) {
    std::vector<int> ids;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM tracks WHERE analysis_version < ?1 ORDER BY last_played DESC, id DESC;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return ids;

    sqlite3_bind_int(stmt, 1, currentVersion);
    while (sqlite3_step(stmt) == SQLITE_ROW)
        ids.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);
    return ids;
}

int Database::recomputeNormalizationGains(const std::function<double(double, double)>& gainFor) {
    struct Change { int id; double gain; };
    std::vector<Change> changes;
    const bool read = each("SELECT id, loudness_lufs, true_peak_db, normalization_gain FROM tracks "
                           "WHERE analysis_version >= 1;", {},
                           [&](sqlite3_stmt* s) {
        const double gain = gainFor(sqlite3_column_double(s, 1), sqlite3_column_double(s, 2));
        if (std::abs(gain - sqlite3_column_double(s, 3)) > 1.0e-9)
            changes.push_back({ sqlite3_column_int(s, 0), gain });
    });
    if (!read) return -1;
    if (changes.empty()) return 0;

    if (!execute("BEGIN IMMEDIATE;")) return -1;
    bool ok = true;
    for (const auto& change : changes)
        ok = ok && run("UPDATE tracks SET normalization_gain = ?1 WHERE id = ?2;", [&change](sqlite3_stmt* s) {
            sqlite3_bind_double(s, 1, change.gain);
            sqlite3_bind_int(s, 2, change.id);
        });
    if (!ok || !execute("COMMIT;")) {
        execute("ROLLBACK;");
        return -1;
    }
    return (int) changes.size();
}

bool Database::run(const char* sql, const std::function<void(sqlite3_stmt*)>& bind) {
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "SQL error: " << sqlite3_errmsg(db) << " in SQL: " << sql << "\n";
        return false;
    }
    if (bind) bind(stmt);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) std::cerr << "SQL error: " << sqlite3_errmsg(db) << " in SQL: " << sql << "\n";
    sqlite3_finalize(stmt);
    return ok;
}

static void bindText(sqlite3_stmt* stmt, int index, const std::string& text) {
    sqlite3_bind_text(stmt, index, text.c_str(), -1, SQLITE_TRANSIENT);
}

bool Database::incrementPlayCount(int trackId) {
    return run("UPDATE tracks SET play_count = play_count + 1, last_played = datetime('now', 'localtime') WHERE id = ?1;",
               [trackId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, trackId); });
}

bool Database::updateTrackDetails(int trackId, const std::string& artist, const std::string& title, const std::string& path) {
    const std::string storedPath = toStoredPath(path);
    const auto matchKey = storedMatchKey(artist, title, {});
    const bool ok = run("UPDATE tracks SET artist = ?1, title = ?2, path = ?3, match_key = ?5, "
                        "updated_at = datetime('now', 'localtime') WHERE id = ?4;",
                        [&](sqlite3_stmt* s) {
                            bindText(s, 1, artist);
                            bindText(s, 2, title);
                            bindText(s, 3, storedPath);
                            sqlite3_bind_int(s, 4, trackId);
                            bindText(s, 5, matchKey);
                        });
    if (ok) indexTrack(trackId);
    return ok;
}

bool Database::deleteTrack(int trackId) {
    auto bindId = [trackId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, trackId); };
    execute("BEGIN TRANSACTION;");
    run("DELETE FROM track_categories WHERE track_id = ?1;", bindId);
    if (ftsAvailable) run("DELETE FROM tracks_fts WHERE rowid = ?1;", bindId);
    const bool success = run("DELETE FROM tracks WHERE id = ?1;", bindId);
    execute("COMMIT;");
    return success;
}

void Database::clearDatabase() {
    execute("DELETE FROM track_categories;");
    execute("DELETE FROM track_credits;");
    execute("DELETE FROM artist_aliases;");
    execute("DELETE FROM artists;");
    execute("DELETE FROM import_batches;");
    if (ftsAvailable) execute("DELETE FROM tracks_fts;");
    execute("DELETE FROM tracks;");
    execute("DELETE FROM categories;");
    ensureSystemCategories();
    execute("VACUUM;");
}

bool Database::backupTo(const std::string& targetPath) {
    return run("VACUUM INTO ?1;", [&](sqlite3_stmt* s) { bindText(s, 1, targetPath); });
}

//==============================================================================
// Categorías

namespace {
    struct SystemCategory {
        CategoryRole role;
        const char* name;        // nombre guardado (en pantalla se traduce)
        int folderFormat;        // 0 = carpetas por fecha, 1 = por artista
        const char* legacyName;  // cómo se llamaba en versiones anteriores
        // Si una categoría del usuario con ese nombre se adopta. Nunca para los temporales:
        // sus pistas se ocultarían de la biblioteca y se podrían borrar con "Vaciar".
        bool adoptByName = true;
    };

    const SystemCategory systemCategories[] = {
        { CategoryRole::StationIds,  "IDENTIFICACIONES", 1, nullptr },
        { CategoryRole::Spots,       "CUÑAS",            0, "ANUNCIOS" },
        { CategoryRole::Sweepers,    "SEPARADORES",      1, "PROMOCIONES" },
        // En minúsculas y con "_" (como _Entrada y _Descartadas): un nombre que el usuario no
        // puede crear (las categorías se escriben en mayúsculas). Carpetas por fecha.
        { CategoryRole::Temporaries, "_Temporales",      0, nullptr, false },
    };

    // Lo que sembraban las versiones anteriores al crear la base de datos: EMERGENCIA y
    // los géneros de Assets/genres.txt.
    const char* const legacyDefaultCategories[] = {
        "EMERGENCIA",
        "ANTOLOGUIA", "BACHATA", "BALADA", "BAMBUCO", "BLUES", "BOLERO", "CARRANGA", "CARRILERA",
        "CHAMPETA", "CHIRIMIA", "CORRIDOS", "CORRIDOS TUMBADOS", "CRISTIANA", "CUMBIA", "CURRULAO",
        "DANCE", "DECEMBRINA", "DESPECHO", "DISCO", "ELECTRONICA", "GUARACHA", "GUASCA", "HOUSE",
        "INSTRUMENTAL", "JAZZ", "JOROPO", "MANO A MANO", "MAPALE", "MERENGUE", "MEXICANA", "NORTEÑA",
        "PASILLO", "POP", "POPULAR", "PORRO", "RANCHERA", "REGGAE", "REGGAETON", "ROCK",
        "ROCK EN ESPAÑOL", "SALSA", "TANGO", "TECHNO", "TROPICAL", "URBANA", "VALLENATO", "VIEJOTECA",
        "ZUMBA",
    };
}

int Database::categoryIdByName(const std::string& name) {
    int id = -1;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT id FROM categories WHERE name = ?1;", -1, &stmt, nullptr) == SQLITE_OK) {
        bindText(stmt, 1, name);
        if (sqlite3_step(stmt) == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return id;
}

void Database::ensureSystemCategories() {
    for (const auto& sc : systemCategories) {
        const int role = (int) sc.role;
        int id = getSystemCategory(sc.role).id;

        // Si ya hay una con ese nombre, o con el de versiones anteriores, se adopta.
        for (const char* name : { sc.name, sc.legacyName }) {
            if (id != -1 || name == nullptr || !sc.adoptByName) continue;
            id = categoryIdByName(name);
            if (id != -1) {
                run("UPDATE categories SET system_role = ?1, name = ?2 WHERE id = ?3;", [&](sqlite3_stmt* s) {
                    sqlite3_bind_int(s, 1, role);
                    bindText(s, 2, sc.name);
                    sqlite3_bind_int(s, 3, id);
                });
            }
        }

        if (id == -1) {
            run("INSERT INTO categories (name, sort_order, folder_format, system_role) VALUES (?1, 0, ?2, ?3);",
                [&](sqlite3_stmt* s) {
                    bindText(s, 1, sc.name);
                    sqlite3_bind_int(s, 2, sc.folderFormat);
                    sqlite3_bind_int(s, 3, role);
                });
            continue;
        }

        // Si existían las dos (la nueva y la vieja), la vieja se une a la del sistema.
        const int legacyId = sc.legacyName != nullptr ? categoryIdByName(sc.legacyName) : -1;
        if (legacyId != -1 && legacyId != id) {
            run("INSERT OR IGNORE INTO track_categories (track_id, category_id) "
                "SELECT track_id, ?1 FROM track_categories WHERE category_id = ?2;", [&](sqlite3_stmt* s) {
                    sqlite3_bind_int(s, 1, id);
                    sqlite3_bind_int(s, 2, legacyId);
                });
            auto bindLegacy = [legacyId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, legacyId); };
            run("DELETE FROM track_categories WHERE category_id = ?1;", bindLegacy);
            run("DELETE FROM categories WHERE id = ?1;", bindLegacy);
        }
    }
}

void Database::removeLegacyDefaultCategories() {
    execute("BEGIN TRANSACTION;");
    for (const char* name : legacyDefaultCategories) {
        run("DELETE FROM categories WHERE name = ?1 AND system_role = 0 "
            "AND id NOT IN (SELECT category_id FROM track_categories);",
            [name](sqlite3_stmt* s) { bindText(s, 1, name); });
    }
    execute("COMMIT;");
}

bool Database::addCategory(const std::string& name) {
    return run("INSERT OR IGNORE INTO categories (name, sort_order, folder_format) "
               "VALUES (?1, (SELECT COALESCE(MAX(sort_order), 0) + 1 FROM categories), 1);",
               [&](sqlite3_stmt* s) { bindText(s, 1, name); });
}

bool Database::renameCategory(const std::string& oldName, const std::string& newName) {
    const bool ok = run("UPDATE categories SET name = ?1 WHERE name = ?2 AND system_role = 0;", [&](sqlite3_stmt* s) {
        bindText(s, 1, newName);
        bindText(s, 2, oldName);
    });
    return ok && sqlite3_changes(db) > 0;
}

bool Database::deleteCategory(const std::string& name) {
    auto bindName = [&name](sqlite3_stmt* s) { bindText(s, 1, name); };
    run("DELETE FROM track_categories WHERE category_id = "
        "(SELECT id FROM categories WHERE name = ?1 AND system_role = 0);", bindName);
    const bool ok = run("DELETE FROM categories WHERE name = ?1 AND system_role = 0;", bindName);
    return ok && sqlite3_changes(db) > 0;
}

std::vector<Category> Database::getCategories() {
    std::vector<Category> cats;
    const char* sql = "SELECT id, name, sort_order, folder_format, system_role FROM categories "
                      "ORDER BY CASE WHEN system_role > 0 THEN system_role ELSE 1000 END, sort_order, id;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            Category c;
            c.id = sqlite3_column_int(stmt, 0);
            auto nameText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            c.name = nameText ? nameText : "";
            c.sortOrder = sqlite3_column_int(stmt, 2);
            c.folderFormat = sqlite3_column_int(stmt, 3);
            c.role = (CategoryRole) sqlite3_column_int(stmt, 4);
            cats.push_back(c);
        }
        sqlite3_finalize(stmt);
    }
    return cats;
}

Category Database::getSystemCategory(CategoryRole role) {
    for (const auto& c : getCategories())
        if (c.role == role) return c;
    return { -1, {}, 0, 1 };
}

bool Database::updateCategoryOrder(const std::vector<std::string>& names) {
    execute("BEGIN TRANSACTION;");
    for (size_t i = 0; i < names.size(); ++i) {
        run("UPDATE categories SET sort_order = ?1 WHERE name = ?2 AND system_role = 0;", [&](sqlite3_stmt* s) {
            sqlite3_bind_int(s, 1, (int) i);
            bindText(s, 2, names[i]);
        });
    }
    return execute("COMMIT;");
}

std::vector<int> Database::getTrackCategories(int trackId) {
    std::vector<int> catIds;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT category_id FROM track_categories WHERE track_id = ?1;", -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, trackId);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            catIds.push_back(sqlite3_column_int(stmt, 0));
        }
        sqlite3_finalize(stmt);
    }
    return catIds;
}

std::unordered_map<int, std::vector<int>> Database::getAllTrackCategories() {
    std::unordered_map<int, std::vector<int>> result;
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, "SELECT track_id, category_id FROM track_categories;", -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW)
            result[sqlite3_column_int(stmt, 0)].push_back(sqlite3_column_int(stmt, 1));
        sqlite3_finalize(stmt);
    }
    return result;
}

std::unordered_map<int, CategoryRole> Database::getSystemRolesByTrack() {
    std::unordered_map<int, CategoryRole> result;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT tc.track_id, MAX(c.system_role) FROM track_categories tc "
                      "JOIN categories c ON c.id = tc.category_id WHERE c.system_role > 0 GROUP BY tc.track_id;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW)
            result[sqlite3_column_int(stmt, 0)] = (CategoryRole) sqlite3_column_int(stmt, 1);
        sqlite3_finalize(stmt);
    }
    return result;
}

bool Database::setTrackCategories(int trackId, const std::vector<int>& categoryIds) {
    execute("BEGIN TRANSACTION;");
    run("DELETE FROM track_categories WHERE track_id = ?1;", [trackId](sqlite3_stmt* s) { sqlite3_bind_int(s, 1, trackId); });

    for (int catId : categoryIds)
        addTrackCategory(trackId, catId);

    return execute("COMMIT;");
}

bool Database::addTrackCategory(int trackId, int categoryId) {
    return run("INSERT OR IGNORE INTO track_categories (track_id, category_id) VALUES (?1, ?2);", [&](sqlite3_stmt* s) {
        sqlite3_bind_int(s, 1, trackId);
        sqlite3_bind_int(s, 2, categoryId);
    });
}

bool Database::setCategoryFolderFormat(const std::string& name, int format) {
    return run("UPDATE categories SET folder_format = ?1 WHERE name = ?2;", [&](sqlite3_stmt* s) {
        sqlite3_bind_int(s, 1, format);
        bindText(s, 2, name);
    });
}
