#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <JuceHeader.h>

struct TrackInfo {
    int id;
    std::string path;
    std::string artist;
    std::string title;
    std::string category;      
    int playCount;
    double duration;
    std::string lastPlayed;
    double normalizationGain;
    std::string createdAt;
    std::string updatedAt;

    // Análisis de audio (TrackAnalyzer), hecho una vez al importar. analysisVersion 0 =
    // todavía sin analizar (pistas de versiones anteriores).
    double loudnessLufs = -70.0;  // sonoridad integrada EBU R 128
    double truePeakDb = -70.0;    // pico real, dBTP
    double cueIn = 0.0;           // segundos
    double cueOut = 0.0;          // segundos; 0 = hasta el final del archivo
    int analysisVersion = 0;
    std::string fileHash;         // SHA-256 del archivo, para detectar duplicados

    // Solo en la cola (no se guarda en la base de datos): la tanda comercial a la que
    // pertenece, con la clave de AdScheduler ("AAAA-MM-DD HH:MM"). Vacía si no es de una tanda.
    std::string breakKey;

    // Lo que dura al aire, del punto de entrada al de salida.
    double getAirDuration() const { return cueOut > cueIn ? cueOut - cueIn : duration; }
};

// Categorías fijas de la emisora: siempre existen, van primero y en este orden, y no se
// renombran, mueven ni borran. La app las reconoce por su rol, nunca por el nombre.
enum class CategoryRole {
    None = 0,        // categoría normal (música), creada por el usuario
    StationIds = 1,  // IDENTIFICACIONES: abren cada tanda comercial
    Spots = 2,       // CUÑAS: la publicidad paga, con número y horario en la pauta
    Sweepers = 3     // SEPARADORES: cortinillas, jingles y promos entre canciones
};

struct Category {
    int id;
    std::string name;
    int sortOrder;
    int folderFormat;
    CategoryRole role = CategoryRole::None;

    bool isSystem() const { return role != CategoryRole::None; }
};

// Fechas guardadas por SQLite con datetime('now', 'localtime'): "AAAA-MM-DD HH:MM:SS" en
// hora local. (Time::fromISO8601 las tomaría como UTC y correría la hora.)
inline juce::Time parseLocalTimestamp(const std::string& text) {
    const auto s = juce::String::fromUTF8(text.c_str());
    if (s.length() < 19) return {};
    return juce::Time(s.substring(0, 4).getIntValue(), s.substring(5, 7).getIntValue() - 1,
                      s.substring(8, 10).getIntValue(), s.substring(11, 13).getIntValue(),
                      s.substring(14, 16).getIntValue(), s.substring(17, 19).getIntValue(), 0, true);
}

// Una pista que salió al aire, tal como sonó (título y artista se copian para que el
// registro no cambie si luego se renombra o se borra la pista).
struct PlayLogEntry {
    std::string playedAt;   // "AAAA-MM-DD HH:MM:SS", hora local
    int trackId = -1;       // -1 si el archivo no estaba en la biblioteca
    std::string artist;
    std::string title;
    double duration = 0.0;
    std::string deck;
};

class Database {
public:
    Database();
    ~Database();

    bool open(const std::string& dbPath);
    void close();

    // Rutas portables: los archivos dentro de esta carpeta (el laboratorio) se guardan
    // con ruta relativa a ella, así la base de datos sigue sirviendo si el laboratorio
    // cambia de unidad o de equipo. Hacia afuera, TrackInfo::path siempre es absoluta.
    void setPathBase(const juce::File& labRoot);
    // Convierte a relativas las rutas absolutas guardadas antes de este cambio, incluidas
    // las de un laboratorio que se movió. beforeWriting se llama solo si hay algo que
    // cambiar (para respaldar antes). Devuelve cuántas filas cambió.
    int migrateToRelativePaths(const std::function<void()>& beforeWriting = {});
    
     
    bool initSchema();
    bool addTrack(const TrackInfo& track);
    std::vector<TrackInfo> searchTracks(const std::string& query, const std::string& categoryFilterId = "");
    TrackInfo getTrackByPath(const std::string& path);
    TrackInfo getTrackById(int id);
    bool incrementPlayCount(int trackId);
    bool updateTrackDetails(int trackId, const std::string& artist, const std::string& title, const std::string& path);
    bool deleteTrack(int trackId);
    void clearDatabase();
    // Copia consistente de la base (aunque esté en uso) en un archivo nuevo: VACUUM INTO.
    bool backupTo(const std::string& targetPath);

    // Análisis: guarda ganancia, sonoridad, pico, puntos de entrada/salida, duración,
    // versión y hash (si no está vacío) de la pista track.id.
    bool updateTrackAnalysis(const TrackInfo& track);
    std::vector<int> getTrackIdsNeedingAnalysis(int currentVersion);  // las más recientes al aire primero

    // Duplicados: el mismo archivo (hash) o la misma canción (artista, título y duración
    // parecida). Devuelven id -1 si no hay.
    TrackInfo findTrackByHash(const std::string& fileHash);
    TrackInfo findTrackByMetadata(const std::string& artist, const std::string& title,
                                  double duration, double toleranceSeconds);

    // Categorías. Renombrar, borrar y reordenar no tocan las categorías del sistema.
    bool addCategory(const std::string& name);
    bool renameCategory(const std::string& oldName, const std::string& newName);
    bool deleteCategory(const std::string& name);
    std::vector<Category> getCategories();  // primero las del sistema, en orden de rol
    Category getSystemCategory(CategoryRole role);  // id -1 si no existe
    bool updateCategoryOrder(const std::vector<std::string>& names);
    bool setCategoryFolderFormat(const std::string& name, int format);

     
    std::vector<int> getTrackCategories(int trackId);
    bool setTrackCategories(int trackId, const std::vector<int>& categoryIds);
    bool addTrackCategory(int trackId, int categoryId);  // sin quitar las que ya tiene
    std::unordered_map<int, std::vector<int>> getAllTrackCategories();  // id de pista → categorías
    // Pistas que están en alguna categoría del sistema (identificaciones, cuñas, separadores)
    // con su rol; las que no aparecen son música.
    std::unordered_map<int, CategoryRole> getSystemRolesByTrack();

    // Registro de emisión
    bool addPlayLogEntry(const PlayLogEntry& entry);
    std::vector<PlayLogEntry> getRecentPlayLog(int limit);  // la más reciente primero

private:
    sqlite3* db = nullptr;
    juce::File pathBase;

    bool execute(const std::string& sql);  // solo SQL fijo, sin datos del usuario
    // Sentencia preparada que no devuelve filas; bind asigna los parámetros (?1, ?2...).
    bool run(const char* sql, const std::function<void(sqlite3_stmt*)>& bind = {});
    std::string toStoredPath(const std::string& path) const;
    std::string toAbsolutePath(const std::string& storedPath) const;

    // Crea las categorías del sistema que falten, adoptando las de versiones anteriores
    // (ANUNCIOS pasa a CUÑAS, PROMOCIONES a SEPARADORES, con sus pistas).
    void ensureSystemCategories();
    // Una sola vez: borra las categorías que sembraban las versiones anteriores (géneros
    // y EMERGENCIA) si siguen vacías. Las que tienen pistas se quedan.
    void removeLegacyDefaultCategories();
    int categoryIdByName(const std::string& name);  // -1 si no existe

    // Consulta de pistas con sentencia preparada: bind asigna los parámetros y cada fila
    // se convierte con readTrackRow (columnas en el orden de trackColumns).
    std::vector<TrackInfo> queryTracks(const std::string& whereClause,
                                       const std::function<void(sqlite3_stmt*)>& bind);
    TrackInfo readTrackRow(sqlite3_stmt* stmt) const;
};
