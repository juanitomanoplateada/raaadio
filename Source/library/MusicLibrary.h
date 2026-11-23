#pragma once
#include <JuceHeader.h>
#include <deque>
#include "../database/Database.h"
#include "TrackNaming.h"
#include "AudioConverter.h"

class MusicLibrary {
public:
    MusicLibrary();
    ~MusicLibrary();

    void initialise(const juce::File& dbFile);

    // Importa un archivo: lo copia tal cual a la biblioteca (sin recodificar) y lo analiza
    // (sonoridad y puntos de entrada y salida). Si ya estaba (mismo archivo o misma canción),
    // no lo copia otra vez. Lo que JUCE no decodifica por su cuenta (WMA, y con FFmpeg las
    // notas de voz de WhatsApp, M4A, AAC...: AudioConverter) se guarda como FLAC.
    // La música entra por revisar, en Library/_Entrada, con artista y título propuestos: la
    // revisión los confirma y la ordena en Library/Musica. Las cuñas, identificaciones y
    // separadores entran directo a la carpeta de su categoría.
    // NeedsFfmpeg: el archivo se puede importar convirtiéndolo, pero FFmpeg no está instalado.
    enum class ImportResult { Added, Duplicate, Failed, NeedsFfmpeg };
    ImportResult processAndAddTrack(const juce::File& sourceFile, const juce::String& targetCategory = "", int batch = 0);
    void importFiles(const juce::Array<juce::File>& files, const juce::String& targetCategory = "");
    // Una canción descargada (búsqueda en internet): entra por revisar como la música importada,
    // en un lote propio que se puede deshacer, y guarda su ISRC. Si hay una importación en curso
    // espera a que termine (se llama desde otro hilo).
    struct DownloadImport { ImportResult result = ImportResult::Failed; int trackId = -1; };
    DownloadImport importDownloaded(const juce::File& file, const juce::String& batchSource, const juce::String& isrc);
    void cancelImport();
    // "*.mp3;*.wav;..." con los formatos que la biblioteca sabe leer o convertir (para elegir archivos).
    juce::String getSupportedFileWildcard() const;
    bool isImportable(const juce::File& file) const;  // por la extensión
    bool isConverterAvailable() const { return converter.isAvailable(); }
    void setConverter(AudioConverter::Services services) { converter = AudioConverter(std::move(services)); }
    bool isImporting() const;
    double getProgress() const;
    juce::String getImportStatus() const;
    void resetLibrary();
    // Copia consistente de la base de datos en target (para respaldos): sirve con la
    // emisora al aire, aunque se esté importando o analizando.
    bool backupDatabaseTo(const juce::File& target);

    // Resultado de la última importación, para mostrarlo al terminar (toReview: las que
    // entraron por revisar; needFfmpeg: las que esperan a que se instale FFmpeg).
    struct ImportSummary { int added = 0; int duplicates = 0; int failed = 0; int toReview = 0; int needFfmpeg = 0; };
    ImportSummary getLastImportSummary() const;
    // Los archivos de la última importación que necesitan FFmpeg, y su categoría: se vuelven
    // a importar después de instalarlo.
    struct WaitingFiles { juce::Array<juce::File> files; juce::String category; };
    WaitingFiles getFilesWaitingForFfmpeg() const;
    int getImportCount() const { return importCount.load(); }  // importaciones iniciadas

    // Dos pistas con el mismo artista y título cuya duración difiere menos que esto se
    // consideran la misma canción.
    static constexpr double duplicateToleranceSeconds = 2.0;

    //==========================================================================
    // Análisis (TrackAnalyzer). Las pistas importadas con versiones anteriores se
    // analizan en segundo plano, de a una y con pausas para no competir por el disco
    // con los decks; las que van a sonar pronto se adelantan en la fila.
    void startBackgroundAnalysis();
    void stopBackgroundAnalysis();
    void prioritiseAnalysis(const std::vector<TrackInfo>& upcoming);
    int getPendingAnalysisCount() const { return pendingAnalysis.load(); }

    static bool needsAnalysis(const TrackInfo& track);
    // Analiza file y completa los campos de análisis de track (ganancia incluida).
    bool analyseFile(const juce::File& file, TrackInfo& track, const std::function<bool()>& shouldAbort = {});
    // Si a la pista le falta el análisis, lo hace (bloquea mientras decodifica el archivo:
    // se llama desde otro hilo), lo guarda y devuelve la pista actualizada.
    TrackInfo ensureAnalysed(const TrackInfo& track, const std::function<bool()>& shouldAbort = {});

    //==========================================================================
    std::vector<TrackInfo> search(const juce::String& query, const juce::String& categoryFilter = "");
    TrackInfo getTrackByPath(const juce::String& path);
    TrackInfo getTrackById(int id);
    void incrementPlayCount(int id);
    void deleteTrack(int id, bool deleteFile);

    //==========================================================================
    // Catálogo y revisión

    // Confirma artista, título y créditos de una pista (también al editar una ya confirmada):
    // guarda los créditos y, si es música, mueve el archivo a Library/Musica/<artista>/.
    // aliasFrom: cómo venía escrito el artista, si el operador lo corrigió (se recuerda).
    // Si el archivo está sonando no se puede mover: los datos se guardan igual y se mueve
    // al próximo inicio.
    struct SaveResult {
        bool ok = false;
        bool fileMoved = false;
        bool moveDeferred = false;  // el archivo no se pudo mover ahora
        juce::String error;
    };
    SaveResult confirmTrack(int id, const TrackNaming::Proposal& values, const juce::String& aliasFrom = {});
    // Saca una pista de la biblioteca: el archivo va a Library/_Descartadas (nunca se borra).
    // Si era una copia de otra (mergeInto), las categorías pasan a esa.
    bool discardTrack(int id, int mergeInto = -1);

    // Créditos guardados de una pista, como propuesta (para editarla).
    TrackNaming::Proposal getTrackCredits(const TrackInfo& track);
    std::vector<Database::ReviewRecord> getReviewRecords(bool pending);
    bool saveReviewDraft(int trackId, const juce::String& json);
    int getPendingCount();
    int getUnreviewedCount();
    // Lo que ayuda a leer un lote: artistas confirmados, alias y categorías.
    TrackNaming::Context makeNamingContext(const std::vector<TrackNaming::Source>& batch);
    // Dónde quedaría el archivo al confirmar.
    juce::File musicDestination(const TrackNaming::Proposal& values, const juce::String& extension) const;
    // Cambia cada vez que se importa, confirma o descarta algo (para refrescar contadores).
    int getCatalogVersion() const { return catalogVersion.load(); }

    // Se llama en el hilo de la interfaz cuando el archivo de una pista cambia de lugar (la
    // cola y la botonera guardan la ruta).
    std::function<void(int trackId, const juce::String& newPath)> onTrackRelocated;

    // Artistas y títulos en MAYÚSCULAS en pantalla y en la transmisión (lo guardado no cambia).
    bool isDisplayUppercase();
    void setDisplayUppercase(bool uppercase);

    //==========================================================================
    // Artistas: cambiar cómo se escribe uno, o fusionar dos que son el mismo. Todas sus
    // canciones cambian de nombre (y de carpeta en Library/Musica); lo anterior queda como
    // alias, para que una importación futura lo reconozca.
    struct ArtistResult {
        bool ok = false;
        int conflictId = -1;   // al renombrar: ya hay un artista con ese nombre (¿fusionarlos?)
        int tracksUpdated = 0;
        int movesDeferred = 0; // archivos que estaban sonando: se mueven al próximo inicio
    };
    std::vector<Database::ArtistOverview> getArtists();
    std::vector<TrackInfo> getTracksOfArtist(int artistId);
    ArtistResult renameArtist(int artistId, const juce::String& newName);
    ArtistResult mergeArtists(int fromId, int intoId);
    bool removeArtistAlias(const juce::String& aliasKey);

    //==========================================================================
    // Lotes de importación: deshacer uno saca de la biblioteca lo que sigue por revisar (los
    // archivos van a Library/_Descartadas); lo ya confirmado se queda.
    std::vector<Database::BatchInfo> getImportBatches();
    int undoImportBatch(int batchId);

    // Intérpretes (principal e invitados) por clave de las pistas con créditos.
    std::unordered_map<int, std::vector<std::string>> getPerformerKeys();

    // Huella acústica de una pista (la calcula el análisis en segundo plano; al importar,
    // la importación). false si el archivo no se puede leer.
    bool computeFingerprint(int trackId);


    bool addCategory(const juce::String& name);
    bool renameCategory(const juce::String& oldName, const juce::String& newName);
    bool deleteCategory(const juce::String& name);
    std::vector<Category> getCategories();
    // Categorías del sistema (identificaciones, cuñas, separadores): se buscan por rol.
    Category getSystemCategory(CategoryRole role);  // id -1 si no existe
    std::vector<TrackInfo> getTracksWithRole(CategoryRole role);
    // Nombre para mostrar: las del sistema, traducido; las demás, tal como se crearon.
    static juce::String getDisplayName(const Category& category);
    bool updateCategoryOrder(const juce::StringArray& names);
    bool setCategoryFolderFormat(const juce::String& name, int format);
    juce::String forceUpperCase(const juce::String& s);


    std::vector<int> getTrackCategories(int trackId);
    std::unordered_map<int, std::vector<int>> getAllTrackCategories();  // una sola consulta
    std::unordered_map<int, CategoryRole> getSystemRolesByTrack();  // las que no están son música
    bool setTrackCategories(int trackId, const std::vector<int>& categoryIds);

    // Registro de emisión (tabla play_log)
    bool addPlayLogEntry(const PlayLogEntry& entry);
    std::vector<PlayLogEntry> getRecentPlayLog(int limit);

    Database& getDatabase() { return db; }

    // La base con su candado tomado, para el estado de la emisora (pauta, cola, botonera,
    // ajustes...) desde cualquier hilo: library.withDatabase([](Database& db) { ... }).
    template <typename Fn>
    auto withDatabase(Fn&& fn) {
        const juce::ScopedLock sl(dbLock);
        return fn(db);
    }

    // Nombre de archivo o carpeta válido en Windows, macOS y Linux.
    static juce::String makeSafeFileName(const juce::String& name);

private:
    Database db;
    juce::CriticalSection dbLock;
    // Una importación a la vez (la de archivos y la de descargas usan importContext).
    juce::CriticalSection importLock;
    juce::File libraryFolder;
    juce::AudioFormatManager formatManager;
    AudioConverter converter;


    class ImportThread : public juce::Thread {
    public:
        ImportThread(MusicLibrary& owner, const juce::Array<juce::File>& filesToImport, const juce::String& cat = "");
        void run() override;

    private:
        MusicLibrary& library;
        juce::Array<juce::File> files;
        juce::String targetCategory;
    };

    std::unique_ptr<ImportThread> currentImportThread;
    std::atomic<double> importProgress{0.0};
    std::atomic<int> importAdded{0}, importDuplicates{0}, importFailed{0}, importToReview{0}, importNeedFfmpeg{0}, importCount{0};
    mutable juce::CriticalSection waitingLock;
    WaitingFiles waitingForFfmpeg;
    std::atomic<int> catalogVersion{0};
    std::unique_ptr<TrackNaming::Context> importContext;  // solo mientras corre ImportThread

    // Mueve un archivo de la biblioteca a target (o "target (2)"...). Devuelve dónde quedó;
    // si no se pudo mover, el mismo archivo.
    juce::File placeFile(const juce::File& current, const juce::File& target, bool& moved);
    // Quita las carpetas vacías que dejó un archivo movido, hasta la carpeta Library.
    void removeEmptyFolders(juce::File folder);
    bool isInsideLibrary(const juce::File& file) const;
    void notifyRelocated(int trackId, const juce::File& file);
    // Al abrir: mueve los archivos de música confirmada que no se pudieron mover antes.
    int relocateMisplacedTracks();
public:
    // Tras restaurar un respaldo de antes de ordenar la música, las pistas apuntan a donde
    // estaban sus archivos. Se buscan por su huella (SHA-256) entre los archivos de
    // Library/Musica que ninguna pista usa. Corre en el hilo del análisis en segundo plano.
    int repairMissingFiles(const std::function<bool()>& shouldStop = {});
private:
    bool isSystemTrack(int trackId);  // con dbLock tomado

    class AnalysisThread : public juce::Thread {
    public:
        explicit AnalysisThread(MusicLibrary& owner) : juce::Thread("Library Analysis"), library(owner) {}
        void run() override { library.runBackgroundAnalysis(*this); }
    private:
        MusicLibrary& library;
    };

    std::unique_ptr<AnalysisThread> analysisThread;
    juce::CriticalSection analysisQueueLock;
    std::deque<int> analysisQueue;
    std::atomic<int> pendingAnalysis{0};
    void runBackgroundAnalysis(juce::Thread& thread);
    int takeNextAnalysisId();
    // Tras renombrar o fusionar artistas: el artista para mostrar y el archivo de cada pista.
    ArtistResult refreshCreditedTracks(const std::vector<int>& trackIds);

    Category findCategory(const juce::String& name);  // con dbLock tomado; id -1 si no existe

    void scanAndImport(const juce::File& file, int& current, int total, const juce::String& targetCategory, int batch);
    int countFiles(const juce::Array<juce::File>& files);
};
