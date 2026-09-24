#pragma once
#include <JuceHeader.h>
#include <deque>
#include "../database/Database.h"

class MusicLibrary {
public:
    MusicLibrary();
    ~MusicLibrary();

    void initialise(const juce::File& dbFile);

    // Importa un archivo: lo copia tal cual a la biblioteca (sin recodificar), toma
    // artista y título de sus etiquetas, y lo analiza (sonoridad y puntos de entrada y
    // salida). Si ya estaba (mismo archivo o misma canción), no lo copia otra vez.
    enum class ImportResult { Added, Duplicate, Failed };
    ImportResult processAndAddTrack(const juce::File& sourceFile, const juce::String& targetCategory = "");
    void importFiles(const juce::Array<juce::File>& files, const juce::String& targetCategory = "");
    void cancelImport();
    // "*.mp3;*.wav;..." con los formatos que la biblioteca sabe leer (para elegir archivos).
    juce::String getSupportedFileWildcard() const;
    bool isImporting() const;
    double getProgress() const;
    juce::String getImportStatus() const;
    void resetLibrary();
    // Copia consistente de la base de datos en target (para respaldos): sirve con la
    // emisora al aire, aunque se esté importando o analizando.
    bool backupDatabaseTo(const juce::File& target);

    // Resultado de la última importación, para mostrarlo al terminar.
    struct ImportSummary { int added = 0; int duplicates = 0; int failed = 0; };
    ImportSummary getLastImportSummary() const;
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

    struct RenameResult {
        bool success;
        juce::String errorMessage;
    };
    RenameResult renameTrack(int id, const juce::String& newArtist, const juce::String& newTitle);
    bool updateTrackMetadata(int id, const juce::String& artist, const juce::String& title);



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
    bool setTrackCategories(int trackId, const std::vector<int>& categoryIds);

    // Registro de emisión (tabla play_log)
    bool addPlayLogEntry(const PlayLogEntry& entry);
    std::vector<PlayLogEntry> getRecentPlayLog(int limit);

    Database& getDatabase() { return db; }

    // Nombre de archivo o carpeta válido en Windows, macOS y Linux.
    static juce::String makeSafeFileName(const juce::String& name);

private:
    Database db;
    juce::CriticalSection dbLock;
    juce::File libraryFolder;
    juce::AudioFormatManager formatManager;


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
    std::atomic<int> importAdded{0}, importDuplicates{0}, importFailed{0}, importCount{0};

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

    struct Metadata {
        juce::String artist;
        juce::String title;
    };
    Metadata parseFilename(const juce::String& filename);
    // Etiquetas del archivo; lo que falte, del nombre ("Artista - Título").
    Metadata readMetadata(const juce::File& file, const juce::StringPairArray& readerMetadata);
    Category findCategory(const juce::String& name);  // con dbLock tomado; id -1 si no existe

    void scanAndImport(const juce::File& file, int& current, int total, const juce::String& targetCategory);
    int countFiles(const juce::Array<juce::File>& files);
};
