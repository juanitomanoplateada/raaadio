#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <unordered_map>
#include <cstdint>
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

    // Catálogo (esquema 3). artist y title son lo que se muestra ("Eddy Herrera feat. Liz",
    // "Volveré (En vivo)"); los créditos por separado están en track_credits.
    int status = 0;               // 0 = en la biblioteca; 1 = importada, por revisar
    std::string version;          // "En vivo", "Remix"... (ya incluida en title)
    int year = 0;
    std::string confirmedAt;      // cuándo alguien confirmó los datos; vacío = sin revisar
    // Por revisar: se puede poner al aire a mano, pero AutoDJ y el generador no la usan.
    bool isPending() const { return status == 1; }

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

    //--------------------------------------------------------------------------
    // Catálogo (Catalog.cpp): artistas, créditos, lotes de importación y revisión.

    enum class CreditRole { Main = 1, Featured = 2, Composer = 3 };
    struct Credit {
        int artistId = -1;
        std::string name;
        CreditRole role = CreditRole::Main;
    };

    // De dónde vino una pista importada: el nombre del archivo y su carpeta originales, y
    // las etiquetas tal como venían. La revisión propone artista y título a partir de esto.
    struct ImportSource {
        std::string name;        // nombre del archivo, sin extensión
        std::string folder;      // carpeta de origen
        std::string tagArtist, tagTitle;
        int batch = 0;           // import_batches.id; 0 = sin lote
    };
    int createImportBatch(const std::string& source, int categoryId, int files);  // -1 si falla
    bool setImportSource(int trackId, const ImportSource& source);

    // Lo que se revisa: las importadas por revisar (pending) o la música de la biblioteca que
    // nadie ha confirmado todavía (de versiones anteriores). Sin cuñas, identificaciones ni
    // separadores.
    struct ReviewRecord {
        TrackInfo track;
        ImportSource source;
        std::string draft;       // borrador de la revisión (JSON), si alguien empezó a editarla
        std::vector<uint32_t> fingerprint;  // huella acústica, si ya se calculó
    };
    std::vector<ReviewRecord> getReviewRecords(bool pending);
    int countPending();
    int countUnreviewed();
    bool saveReviewDraft(int trackId, const std::string& json);

    // Artistas con créditos (clave → nombre, canciones como intérprete principal) y alias
    // aprendidos (clave del alias → clave del artista).
    struct ArtistRow { std::string name, key; int tracks = 0; };
    std::vector<ArtistRow> getKnownArtists();
    std::vector<std::pair<std::string, std::string>> getArtistAliases();
    std::vector<Credit> getTrackCredits(int trackId);

    // Confirma una pista: créditos, datos, ruta y estado en una transacción.
    struct CatalogEntry {
        std::string artist;                         // intérprete principal
        std::vector<std::string> featured, composers;
        std::string title, version;                 // título sin la versión
        int year = 0;
        std::string displayArtist, displayTitle;    // lo que se guarda en artist y title
        std::string path;
        std::string aliasKey;                       // otra forma de escribir al artista, para recordar
    };
    bool confirmTrack(int trackId, const CatalogEntry& entry);
    // Solo artista y título (cuñas, identificaciones, separadores: no llevan créditos).
    bool setTrackNames(int trackId, const std::string& artist, const std::string& title);
    bool setTrackPath(int trackId, const std::string& path);

    // La misma canción (artista principal y título, sin mayúsculas ni tildes) con una
    // duración parecida, entre la música; id -1 si no hay.
    TrackInfo findDuplicate(const std::string& matchKey, double duration, double toleranceSeconds, int excludeId);
    // Clave, duración y nombre de toda la música, de una vez (para buscar duplicados de
    // muchas filas sin una consulta por cada una).
    struct KeyRow { int id = -1; std::string key, artist, title; double duration = 0.0; };
    std::vector<KeyRow> getMusicKeys();

    //--------------------------------------------------------------------------
    // Búsqueda (FTS5): artista, título, créditos, versión, año, nombre original del archivo
    // y etiquetas, sin distinguir mayúsculas ni tildes. Sin FTS5, searchTracks usa LIKE.
    bool hasSearchIndex() const { return ftsAvailable; }
    void indexTrack(int trackId);
    // Rehace el índice si no tiene las mismas pistas que la base (migración, respaldo viejo).
    void ensureSearchIndex();
    void rebuildSearchIndex();

    // Intérpretes (principal e invitados, por clave) de cada pista con créditos: la
    // separación de artistas del generador los compara.
    std::unordered_map<int, std::vector<std::string>> getPerformerKeys();

    // Artistas: cada uno con sus canciones por rol y sus alias.
    struct ArtistOverview {
        int id = -1;
        std::string name, key;
        int main = 0, featured = 0, composer = 0;
        std::vector<std::string> aliases;
    };
    std::vector<ArtistOverview> getArtistsOverview();
    std::vector<int> getTracksOfArtist(int artistId);  // con cualquier rol
    // Cambia cómo se escribe; si el nombre nuevo es de otro artista no cambia nada y devuelve
    // su id en conflictId (el operador decide si fusionarlos). El nombre anterior queda como
    // alias si era otra clave.
    bool renameArtist(int artistId, const std::string& newName, int& conflictId);
    // Los créditos y alias de fromId pasan a intoId; fromId queda como alias de intoId.
    bool mergeArtists(int fromId, int intoId);
    bool removeAlias(const std::string& aliasKey);
    // Artista para mostrar de una pista con créditos (tras renombrar o fusionar).
    bool setTrackArtistLine(int trackId, const std::string& artist, const std::string& matchKey);

    // Lotes de importación, con cuántas canciones siguen por revisar y cuántas se confirmaron.
    struct BatchInfo {
        int id = -1;
        std::string createdAt, source;
        int files = 0, pending = 0, confirmed = 0;
    };
    std::vector<BatchInfo> getImportBatches();
    std::vector<int> getPendingTracksOfBatch(int batchId);

    // Identificación en línea (AcoustID y MusicBrainz): lo que se encontró de cada pista.
    enum class OnlineStatus { None = 0, Found = 1, NotFound = 2, Error = 3 };
    struct OnlineMatch {
        OnlineStatus status = OnlineStatus::None;
        double score = 0.0;  // de AcoustID, de 0 a 1
        std::string recordingId, artist, title, isrc, error;
        std::vector<std::string> featured, composers;
        int year = 0;
    };
    bool saveOnlineMatch(int trackId, const OnlineMatch& match);
    std::unordered_map<int, OnlineMatch> getOnlineMatches();
    // Música por revisar (primero) y la biblioteca sin revisar que no se ha buscado, o que dio
    // error la última vez.
    std::vector<int> getTracksNeedingOnlineLookup();
    // Identificadores de MusicBrainz y el ISRC de una pista confirmada con esos datos.
    bool setOnlineIds(int trackId, const std::string& recordingId, const std::string& isrc);
    std::pair<std::string, std::string> getOnlineIds(int trackId);

    // Huella acústica: una por pista de música (ver AcousticFingerprint).
    bool setFingerprint(int trackId, const std::vector<uint32_t>& fingerprint, int version);
    struct FingerprintRow {
        int id = -1;
        std::string artist, title;
        double duration = 0.0;
        std::vector<uint32_t> fingerprint;
    };
    // La música con huella de duración parecida (una grabación dura lo mismo en otro formato).
    std::vector<FingerprintRow> getFingerprintsNear(double duration, double toleranceSeconds);
    std::vector<int> getTrackIdsNeedingFingerprint(int version);  // música ya analizada

    // Música confirmada con su intérprete principal: para ordenar los archivos que no se
    // pudieron mover al confirmarlos (estaban sonando).
    struct Placement { int id = -1; std::string path, mainArtist, artist, title; };
    std::vector<Placement> getConfirmedMusicPlacement();

    // Una sola vez (esquema 3): calcula las claves de las pistas que ya estaban. Devuelve
    // cuántas pistas había.
    int migrateCatalog();

    // Salud de la base: "ok" o la descripción del daño (PRAGMA quick_check).
    std::string quickCheck();
    int getSchemaVersion();  // PRAGMA user_version

    //--------------------------------------------------------------------------
    // Estado de la emisora (StationState.cpp). Antes vivía en Config/*.json y
    // Templates/*.json; aquí se guarda con transacciones, sobrevive a un corte de luz y
    // sale entero en cada respaldo. Las filas que apuntan a una pista se borran solas si la
    // pista se borra (claves foráneas), y al guardar se omiten las que ya no existen.

    // Ajustes sueltos: clave → texto (los de emisión van como JSON bajo "station").
    std::string getSetting(const std::string& key, const std::string& fallback = {});
    bool setSetting(const std::string& key, const std::string& value);
    // Identificador de esta emisora, creado la primera vez: nombra su carpeta en el destino
    // externo de respaldo, aunque la emisora cambie de nombre o de disco.
    std::string getStationId();

    // Pauta: el N.º de cuña de cada pista y las franjas "HH:MM" de cada número.
    struct AdPlan {
        std::map<int, int> numberByTrack;
        std::map<int, std::vector<std::string>> slotsByNumber;
        bool autoSchedule = false;
    };
    AdPlan loadAdPlan();
    bool saveAdPlan(const AdPlan& plan);

    struct QueueItem { int trackId = -1; std::string breakKey; };
    std::vector<QueueItem> loadQueue();
    bool saveQueue(const std::vector<QueueItem>& items);

    std::vector<int> loadCartwall(int slots);  // id de pista por botón; -1 = libre
    bool saveCartwall(const std::vector<int>& trackIds);

    std::set<std::string> loadAdBreaksDone();  // tandas ya insertadas ("AAAA-MM-DD HH:MM")
    bool saveAdBreaksDone(const std::set<std::string>& keys);

    // Plantillas del generador: nombre → ajustes en JSON.
    std::vector<std::string> getTemplateNames();
    std::string loadTemplate(const std::string& name);  // vacío si no existe
    bool saveTemplate(const std::string& name, const std::string& json);
    bool deleteTemplate(const std::string& name);

    // Una sola vez (esquema 2): pasa a la base los archivos de Config y Templates de las
    // versiones anteriores (o de un respaldo viejo que se restauró) y los deja como
    // *.migrated. Todo en una transacción: si algo falla, no cambia nada. Devuelve cuántos
    // archivos importó, o -1 si falló.
    int importLegacyStationFiles(const juce::File& configFolder, const juce::File& templatesFolder);

    // Después de restaurar un respaldo, la cola y las tandas ya insertadas se traen de la
    // base que había antes: son del momento, no del día del respaldo.
    bool carryOverLiveState(const juce::File& previousDatabase);

private:
    sqlite3* db = nullptr;
    bool ftsAvailable = false;
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

    // Consulta preparada que devuelve filas: bind asigna los parámetros y row recibe cada una.
    bool each(const char* sql, const std::function<void(sqlite3_stmt*)>& bind,
              const std::function<void(sqlite3_stmt*)>& row);
    void createStationTables();
    void createCatalogTables();
    int artistIdFor(const std::string& name);  // lo crea si no existe; -1 si falla
};
