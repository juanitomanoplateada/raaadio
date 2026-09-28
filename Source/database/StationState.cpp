#include "Database.h"

// Estado de la emisora en la base de datos: ajustes, pauta, cola, botonera, tandas ya
// insertadas y plantillas del generador. Ver Database.h.

namespace {
    void bindText(sqlite3_stmt* stmt, int index, const std::string& text) {
        sqlite3_bind_text(stmt, index, text.c_str(), -1, SQLITE_TRANSIENT);
    }

    std::string columnText(sqlite3_stmt* stmt, int column) {
        const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, column));
        return text != nullptr ? text : "";
    }

    // Una sentencia que no devuelve filas.
    bool exec(sqlite3* db, const char* sql, const std::function<void(sqlite3_stmt*)>& bind = {}) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            std::cerr << "SQL error: " << sqlite3_errmsg(db) << " in SQL: " << sql << std::endl;
            return false;
        }
        if (bind) bind(stmt);
        const int rc = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            std::cerr << "SQL error: " << sqlite3_errmsg(db) << " in SQL: " << sql << std::endl;
            return false;
        }
        return true;
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

    bool writeSetting(sqlite3* db, const std::string& key, const std::string& value) {
        return exec(db, "INSERT OR REPLACE INTO settings (key, value) VALUES (?1, ?2);", [&](sqlite3_stmt* s) {
            bindText(s, 1, key);
            bindText(s, 2, value);
        });
    }

    // Las filas que apuntan a una pista solo se escriben si la pista existe todavía.
    bool writeAdPlan(sqlite3* db, const Database::AdPlan& plan) {
        bool ok = exec(db, "DELETE FROM ad_numbers;") && exec(db, "DELETE FROM ad_slots;");
        for (const auto& [trackId, number] : plan.numberByTrack) {
            if (number < 0 || number > 99) continue;
            ok = ok && exec(db, "INSERT INTO ad_numbers (track_id, number) SELECT ?1, ?2 "
                                "WHERE EXISTS (SELECT 1 FROM tracks WHERE id = ?1);", [&](sqlite3_stmt* s) {
                sqlite3_bind_int(s, 1, trackId);
                sqlite3_bind_int(s, 2, number);
            });
        }
        for (const auto& [number, slots] : plan.slotsByNumber) {
            if (number < 0 || number > 99) continue;
            for (const auto& slot : slots)
                ok = ok && exec(db, "INSERT OR IGNORE INTO ad_slots (number, slot) VALUES (?1, ?2);", [&](sqlite3_stmt* s) {
                    sqlite3_bind_int(s, 1, number);
                    bindText(s, 2, slot);
                });
        }
        return ok && writeSetting(db, "ads.autoSchedule", plan.autoSchedule ? "1" : "0");
    }

    bool writeQueue(sqlite3* db, const std::vector<Database::QueueItem>& items) {
        bool ok = exec(db, "DELETE FROM queue;");
        for (size_t i = 0; i < items.size(); ++i)
            ok = ok && exec(db, "INSERT INTO queue (position, track_id, break_key) SELECT ?1, ?2, ?3 "
                                "WHERE EXISTS (SELECT 1 FROM tracks WHERE id = ?2);", [&](sqlite3_stmt* s) {
                sqlite3_bind_int(s, 1, (int) i);
                sqlite3_bind_int(s, 2, items[i].trackId);
                bindText(s, 3, items[i].breakKey);
            });
        return ok;
    }

    bool writeCartwall(sqlite3* db, const std::vector<int>& trackIds) {
        bool ok = exec(db, "DELETE FROM cartwall;");
        for (size_t slot = 0; slot < trackIds.size(); ++slot) {
            if (trackIds[slot] < 0) continue;
            ok = ok && exec(db, "INSERT INTO cartwall (slot, track_id) SELECT ?1, ?2 "
                                "WHERE EXISTS (SELECT 1 FROM tracks WHERE id = ?2);", [&](sqlite3_stmt* s) {
                sqlite3_bind_int(s, 1, (int) slot);
                sqlite3_bind_int(s, 2, trackIds[slot]);
            });
        }
        return ok;
    }

    bool writeAdBreaksDone(sqlite3* db, const std::set<std::string>& keys) {
        bool ok = exec(db, "DELETE FROM ad_breaks_done;");
        for (const auto& key : keys)
            ok = ok && exec(db, "INSERT OR IGNORE INTO ad_breaks_done (break_key) VALUES (?1);",
                            [&](sqlite3_stmt* s) { bindText(s, 1, key); });
        return ok;
    }

    bool writeTemplate(sqlite3* db, const std::string& name, const std::string& json) {
        return exec(db, "INSERT OR REPLACE INTO generator_templates (name, settings) VALUES (?1, ?2);", [&](sqlite3_stmt* s) {
            bindText(s, 1, name);
            bindText(s, 2, json);
        });
    }
}

void Database::createStationTables() {
    execute("CREATE TABLE IF NOT EXISTS settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    execute("CREATE TABLE IF NOT EXISTS ad_numbers ("
            "track_id INTEGER PRIMARY KEY REFERENCES tracks(id) ON DELETE CASCADE, "
            "number INTEGER NOT NULL CHECK (number BETWEEN 0 AND 99));");
    execute("CREATE TABLE IF NOT EXISTS ad_slots ("
            "number INTEGER NOT NULL CHECK (number BETWEEN 0 AND 99), "
            "slot TEXT NOT NULL, PRIMARY KEY (number, slot));");
    execute("CREATE TABLE IF NOT EXISTS ad_breaks_done (break_key TEXT PRIMARY KEY);");
    execute("CREATE TABLE IF NOT EXISTS queue ("
            "position INTEGER PRIMARY KEY, "
            "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE, "
            "break_key TEXT NOT NULL DEFAULT '');");
    execute("CREATE TABLE IF NOT EXISTS cartwall ("
            "slot INTEGER PRIMARY KEY, "
            "track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE);");
    execute("CREATE TABLE IF NOT EXISTS generator_templates (name TEXT PRIMARY KEY, settings TEXT NOT NULL);");
}

bool Database::each(const char* sql, const std::function<void(sqlite3_stmt*)>& bind,
                    const std::function<void(sqlite3_stmt*)>& row) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        std::cerr << "SQL error: " << sqlite3_errmsg(db) << " in SQL: " << sql << std::endl;
        return false;
    }
    if (bind) bind(stmt);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        row(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::string Database::quickCheck() {
    if (db == nullptr) return "not open";
    std::vector<std::string> problems;
    const bool ran = each("PRAGMA quick_check;", {}, [&problems](sqlite3_stmt* s) {
        if (problems.size() < 5) problems.push_back(columnText(s, 0));
    });
    if (!ran) return sqlite3_errmsg(db);
    if (problems.size() == 1 && problems[0] == "ok") return "ok";
    std::string text;
    for (const auto& p : problems) text += (text.empty() ? "" : "; ") + p;
    return text.empty() ? "unknown" : text;
}

int Database::getSchemaVersion() {
    int version = 0;
    each("PRAGMA user_version;", {}, [&version](sqlite3_stmt* s) { version = sqlite3_column_int(s, 0); });
    return version;
}

std::string Database::getSetting(const std::string& key, const std::string& fallback) {
    std::string value = fallback;
    each("SELECT value FROM settings WHERE key = ?1;", [&](sqlite3_stmt* s) { bindText(s, 1, key); },
         [&value](sqlite3_stmt* s) { value = columnText(s, 0); });
    return value;
}

bool Database::setSetting(const std::string& key, const std::string& value) {
    return writeSetting(db, key, value);
}

std::string Database::getStationId() {
    auto id = getSetting("station.id");
    if (id.empty()) {
        id = juce::Uuid().toString().substring(0, 8).toStdString();
        setSetting("station.id", id);
    }
    return id;
}

Database::AdPlan Database::loadAdPlan() {
    AdPlan plan;
    each("SELECT track_id, number FROM ad_numbers;", {},
         [&plan](sqlite3_stmt* s) { plan.numberByTrack[sqlite3_column_int(s, 0)] = sqlite3_column_int(s, 1); });
    each("SELECT number, slot FROM ad_slots ORDER BY number, slot;", {},
         [&plan](sqlite3_stmt* s) { plan.slotsByNumber[sqlite3_column_int(s, 0)].push_back(columnText(s, 1)); });
    plan.autoSchedule = getSetting("ads.autoSchedule") == "1";
    return plan;
}

bool Database::saveAdPlan(const AdPlan& plan) {
    Transaction t(db);
    return t.commit(writeAdPlan(db, plan));
}

std::vector<Database::QueueItem> Database::loadQueue() {
    std::vector<QueueItem> items;
    each("SELECT track_id, break_key FROM queue ORDER BY position;", {},
         [&items](sqlite3_stmt* s) { items.push_back({ sqlite3_column_int(s, 0), columnText(s, 1) }); });
    return items;
}

bool Database::saveQueue(const std::vector<QueueItem>& items) {
    Transaction t(db);
    return t.commit(writeQueue(db, items));
}

std::vector<int> Database::loadCartwall(int slots) {
    std::vector<int> ids((size_t) juce::jmax(0, slots), -1);
    each("SELECT slot, track_id FROM cartwall;", {}, [&ids](sqlite3_stmt* s) {
        const int slot = sqlite3_column_int(s, 0);
        if (slot >= 0 && slot < (int) ids.size()) ids[(size_t) slot] = sqlite3_column_int(s, 1);
    });
    return ids;
}

bool Database::saveCartwall(const std::vector<int>& trackIds) {
    Transaction t(db);
    return t.commit(writeCartwall(db, trackIds));
}

std::set<std::string> Database::loadAdBreaksDone() {
    std::set<std::string> keys;
    each("SELECT break_key FROM ad_breaks_done;", {}, [&keys](sqlite3_stmt* s) { keys.insert(columnText(s, 0)); });
    return keys;
}

bool Database::saveAdBreaksDone(const std::set<std::string>& keys) {
    Transaction t(db);
    return t.commit(writeAdBreaksDone(db, keys));
}

std::vector<std::string> Database::getTemplateNames() {
    std::vector<std::string> names;
    each("SELECT name FROM generator_templates ORDER BY name;", {}, [&names](sqlite3_stmt* s) { names.push_back(columnText(s, 0)); });
    return names;
}

std::string Database::loadTemplate(const std::string& name) {
    std::string json;
    each("SELECT settings FROM generator_templates WHERE name = ?1;", [&](sqlite3_stmt* s) { bindText(s, 1, name); },
         [&json](sqlite3_stmt* s) { json = columnText(s, 0); });
    return json;
}

bool Database::saveTemplate(const std::string& name, const std::string& json) {
    return writeTemplate(db, name, json);
}

bool Database::deleteTemplate(const std::string& name) {
    return exec(db, "DELETE FROM generator_templates WHERE name = ?1;", [&](sqlite3_stmt* s) { bindText(s, 1, name); });
}

//==============================================================================
int Database::importLegacyStationFiles(const juce::File& configFolder, const juce::File& templatesFolder) {
    juce::Array<juce::File> imported, unreadable, secrets;

    // Un archivo que no se puede leer no se importa, pero tampoco se vuelve a intentar: se
    // deja como *.unreadable para revisarlo a mano.
    auto parse = [&unreadable](const juce::File& file) {
        const auto json = juce::JSON::parse(file.loadFileAsString());
        if (json.isVoid()) unreadable.add(file);
        return json;
    };

    Transaction t(db);
    bool ok = t.began;

    const auto ads = configFolder.getChildFile("ads_data.json");
    if (ok && ads.existsAsFile()) {
        const auto json = parse(ads);
        if (json.isObject()) {
            AdPlan plan;
            if (const auto* items = json["assignments"].getArray())
                for (const auto& item : *items)
                    if (item.hasProperty("tid") && item.hasProperty("num"))
                        plan.numberByTrack[(int) item["tid"]] = (int) item["num"];
            if (const auto* items = json["schedules"].getArray())
                for (const auto& item : *items)
                    if (const auto* times = item["times"].getArray())
                        for (const auto& time : *times)
                            plan.slotsByNumber[(int) item["num"]].push_back(time.toString().toStdString());
            plan.autoSchedule = (bool) json.getProperty("autoSchedule", false);
            ok = writeAdPlan(db, plan);
            imported.add(ads);
        }
    }

    const auto queue = configFolder.getChildFile("queue.json");
    if (ok && queue.existsAsFile()) {
        const auto json = parse(queue);
        if (const auto* items = json["tracks"].getArray()) {
            std::vector<QueueItem> list;
            for (const auto& item : *items) {
                // El id es lo fiable; la ruta solo sirve si la pista ya no tiene ese id.
                int id = getTrackById((int) item["id"]).id;
                if (id == -1 && item["path"].toString().isNotEmpty())
                    id = getTrackByPath(item["path"].toString().toStdString()).id;
                if (id != -1) list.push_back({ id, item["break"].toString().toStdString() });
            }
            ok = writeQueue(db, list);
            imported.add(queue);
        }
    }

    const auto carts = configFolder.getChildFile("cartwall.json");
    if (ok && carts.existsAsFile()) {
        const auto json = parse(carts);
        if (const auto* ids = json.getArray()) {
            std::vector<int> list;
            for (const auto& id : *ids) list.push_back((int) id);
            ok = writeCartwall(db, list);
            imported.add(carts);
        }
    }

    const auto breaks = configFolder.getChildFile("ad_breaks_done.json");
    if (ok && breaks.existsAsFile()) {
        const auto json = parse(breaks);
        if (const auto* keys = json.getArray()) {
            std::set<std::string> set;
            for (const auto& key : *keys) set.insert(key.toString().toStdString());
            ok = writeAdBreaksDone(db, set);
            imported.add(breaks);
        }
    }

    const auto station = configFolder.getChildFile("station.json");
    if (ok && station.existsAsFile()) {
        const auto json = parse(station);
        if (json.isObject()) {
            ok = writeSetting(db, "station", juce::JSON::toString(json, true).toStdString());
            imported.add(station);
            // Una clave en claro (versiones muy anteriores) no se deja en el disco.
            if (json.hasProperty("streamPassword")) secrets.add(station);
        }
    }

    for (const auto& file : templatesFolder.findChildFiles(juce::File::findFiles, false, "*.json")) {
        if (!ok) break;
        const auto json = parse(file);
        if (json.isObject()) {
            ok = writeTemplate(db, file.getFileNameWithoutExtension().toStdString(), juce::JSON::toString(json, true).toStdString());
            imported.add(file);
        }
    }

    ok = ok && exec(db, "PRAGMA user_version = 2;");
    if (!t.commit(ok)) {
        juce::Logger::writeToLog("Database: could not import the station files from Config; they stay as they were");
        return -1;
    }

    // Ya están en la base: los archivos quedan como constancia, sin volver a leerse.
    for (const auto& file : imported) {
        if (secrets.contains(file)) file.deleteFile();
        else file.moveFileTo(file.withFileExtension(file.getFileExtension().substring(1) + ".migrated"));
    }
    for (const auto& file : unreadable)
        file.moveFileTo(file.withFileExtension(file.getFileExtension().substring(1) + ".unreadable"));

    if (imported.size() + unreadable.size() > 0)
        juce::Logger::writeToLog("Database: imported " + juce::String(imported.size()) + " station files into the database"
                                 + (unreadable.isEmpty() ? juce::String() : ", " + juce::String(unreadable.size()) + " unreadable"));
    return imported.size();
}

bool Database::carryOverLiveState(const juce::File& previousDatabase) {
    if (!previousDatabase.existsAsFile()) return false;
    if (!exec(db, "ATTACH DATABASE ?1 AS previous;", [&](sqlite3_stmt* s) { bindText(s, 1, previousDatabase.getFullPathName().toStdString()); }))
        return false;

    auto hasTable = [this](const char* name) {
        bool found = false;
        each("SELECT 1 FROM previous.sqlite_master WHERE type = 'table' AND name = ?1;",
             [name](sqlite3_stmt* s) { sqlite3_bind_text(s, 1, name, -1, SQLITE_STATIC); },
             [&found](sqlite3_stmt*) { found = true; });
        return found;
    };

    bool ok = true;
    {
        Transaction t(db);
        if (hasTable("queue"))
            ok = exec(db, "DELETE FROM main.queue;")
              && exec(db, "INSERT INTO main.queue (position, track_id, break_key) SELECT position, track_id, break_key "
                          "FROM previous.queue WHERE track_id IN (SELECT id FROM main.tracks);");
        if (ok && hasTable("ad_breaks_done"))
            ok = exec(db, "DELETE FROM main.ad_breaks_done;")
              && exec(db, "INSERT INTO main.ad_breaks_done (break_key) SELECT break_key FROM previous.ad_breaks_done;");
        ok = t.commit(ok);
    }
    exec(db, "DETACH DATABASE previous;");
    return ok;
}
