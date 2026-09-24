#include "MusicLibrary.h"
#include "TagReader.h"
#include "../audio/TrackAnalyzer.h"
#include "../helpers/PersistenceManager.h"

MusicLibrary::MusicLibrary() {
    juce::Logger::writeToLog("MusicLibrary::MusicLibrary ctor entry");
    juce::Logger::writeToLog("MusicLibrary::MusicLibrary ctor exit");
}

MusicLibrary::~MusicLibrary() {
    // Los hilos usan la base de datos: se detienen antes de cerrarla.
    cancelImport();
    stopBackgroundAnalysis();
    db.close();
}

void MusicLibrary::initialise(const juce::File& dbFile) {
    db.open(dbFile.getFullPathName().toStdString());
    db.initSchema();

    // Las rutas se guardan relativas al laboratorio para que sea portable. Una base de datos
    // de una versión anterior se respalda antes de convertir sus rutas.
    db.setPathBase(PersistenceManager::getLabRoot());
    const int migrated = db.migrateToRelativePaths([this] {
        const auto result = PersistenceManager::createBackup([this](const juce::File& target) { return backupDatabaseTo(target); });
        juce::Logger::writeToLog("MusicLibrary: backup before path migration: "
                                 + (result.wasOk() ? juce::String("ok") : result.getErrorMessage()));
    });
    if (migrated > 0)
        juce::Logger::writeToLog("MusicLibrary: " + juce::String(migrated) + " track paths converted to lab-relative");

    libraryFolder = PersistenceManager::getLibraryFolder();
    
    if (!libraryFolder.exists()) {
        libraryFolder.createDirectory();
        
        // Check for old legacy locations (only if this is a fresh raaadio_lab)
        auto docDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        auto oldFolder1 = docDir.getChildFile("RaaadioLibrary");
        if (oldFolder1.exists()) {
             libraryFolder.deleteRecursively();  
             oldFolder1.moveFileTo(libraryFolder);
        } else {
             auto oldFolder2 = docDir.getChildFile("DJRadioLibrary");
             if (oldFolder2.exists()) {
                 libraryFolder.deleteRecursively(); 
                 oldFolder2.moveFileTo(libraryFolder);
             }
        }
        
    }
    
    formatManager.registerBasicFormats();
}

juce::String MusicLibrary::getSupportedFileWildcard() const {
    return formatManager.getWildcardForAllFormats();
}

void MusicLibrary::importFiles(const juce::Array<juce::File>& files, const juce::String& targetCategory) {
    if (isImporting()) return;
    
    importProgress = 0.0;
    importAdded = 0;
    importDuplicates = 0;
    importFailed = 0;
    ++importCount;
    currentImportThread = std::make_unique<ImportThread>(*this, files, targetCategory);
    currentImportThread->startThread();
}

void MusicLibrary::cancelImport() {
    if (currentImportThread) {
        currentImportThread->signalThreadShouldExit();
        currentImportThread->stopThread(2000);
        currentImportThread.reset();
        importProgress = 0.0;
    }
}

bool MusicLibrary::isImporting() const {
    return currentImportThread && currentImportThread->isThreadRunning();
}

double MusicLibrary::getProgress() const {
    return importProgress;
}

MusicLibrary::ImportSummary MusicLibrary::getLastImportSummary() const {
    return { importAdded.load(), importDuplicates.load(), importFailed.load() };
}

 
MusicLibrary::ImportThread::ImportThread(MusicLibrary& owner, const juce::Array<juce::File>& filesToImport, const juce::String& cat)
    : Thread("Library Import"), library(owner), files(filesToImport), targetCategory(cat) {}

void MusicLibrary::ImportThread::run() {
    int totalFiles = library.countFiles(files);
    int currentFileIndex = 0;
    
    for (const auto& f : files) {
        if (threadShouldExit()) break;
        library.scanAndImport(f, currentFileIndex, totalFiles, targetCategory);
    }
}

int MusicLibrary::countFiles(const juce::Array<juce::File>& files) {
    int count = 0;
    for (const auto& f : files) {
        if (f.isDirectory()) {
            count += countFiles(f.findChildFiles(juce::File::findFiles, true));
        } else if (formatManager.findFormatForFileExtension(f.getFileExtension()) != nullptr) {
            count++;
        }
    }
    return count;
}

void MusicLibrary::scanAndImport(const juce::File& file, int& current, int total, const juce::String& targetCategory) {
    if (juce::Thread::currentThreadShouldExit()) return;

    if (file.isDirectory()) {
        auto children = file.findChildFiles(juce::File::findFiles, true);
        for (const auto& child : children) {
            if (juce::Thread::currentThreadShouldExit()) return;
            scanAndImport(child, current, total, targetCategory);
        }
    } else if (formatManager.findFormatForFileExtension(file.getFileExtension()) != nullptr) {
        // Carátulas, listas y otros archivos de la carpeta se ignoran.
        switch (processAndAddTrack(file, targetCategory)) {
            case ImportResult::Added:     ++importAdded; break;
            case ImportResult::Duplicate: ++importDuplicates; break;
            case ImportResult::Failed:
                if (!juce::Thread::currentThreadShouldExit()) ++importFailed;
                break;
        }
        current++;
        if (total > 0) importProgress = (double)current / total;
    }
}


bool MusicLibrary::backupDatabaseTo(const juce::File& target) {
    const juce::ScopedLock sl(dbLock);
    target.getParentDirectory().createDirectory();
    target.deleteFile();  // VACUUM INTO no escribe sobre un archivo que ya existe
    return db.backupTo(target.getFullPathName().toStdString()) && target.existsAsFile();
}

void MusicLibrary::resetLibrary() {
    cancelImport();
    
    {
        const juce::ScopedLock sl(dbLock);
        db.clearDatabase();
    }
    
     
     
     
     
}

MusicLibrary::Metadata MusicLibrary::parseFilename(const juce::String& filename) {
    Metadata meta;
    auto parts = juce::StringArray::fromTokens(filename, "-", "");
    if (parts.size() >= 2) {
        meta.artist = forceUpperCase(parts[0].trim());  
        parts.remove(0);
        meta.title = forceUpperCase(parts.joinIntoString("-").trim());  
    } else {
        meta.artist = "UNKNOWN";
        meta.title = forceUpperCase(filename);  
    }
    return meta;
}

juce::String MusicLibrary::forceUpperCase(const juce::String& s) {
    juce::String result = s.toUpperCase();
    
    // Explicitly handle Spanish characters that might fail standard toUpperCase()
    static const char* lowerChars[] = { "á", "é", "í", "ó", "ú", "ñ" };
    static const char* upperChars[] = { "Á", "É", "Í", "Ó", "Ú", "Ñ" };
    
    for (int i = 0; i < 6; ++i) {
        result = result.replace(lowerChars[i], upperChars[i]);
    }
    
    return result;
}

juce::String MusicLibrary::makeSafeFileName(const juce::String& name) {
    juce::String safe;
    for (auto c : name)
        safe += (c < 0x20 || juce::String("/\\:*?\"<>|").containsChar(c)) ? (juce::juce_wchar) '_' : c;

    // Windows no admite nombres que terminen en punto o espacio.
    safe = safe.trim();
    while (safe.endsWithChar('.')) safe = safe.dropLastCharacters(1).trimEnd();
    if (safe.length() > 120) safe = safe.substring(0, 120).trimEnd();
    return safe.isNotEmpty() ? safe : juce::String("_");
}

MusicLibrary::Metadata MusicLibrary::readMetadata(const juce::File& file, const juce::StringPairArray& readerMetadata) {
    const auto tags = TagReader::read(file, readerMetadata);
    const auto fromName = parseFilename(file.getFileNameWithoutExtension());

    Metadata meta;
    meta.artist = tags.artist.isNotEmpty() ? forceUpperCase(tags.artist) : fromName.artist;
    meta.title = tags.title.isNotEmpty() ? forceUpperCase(tags.title) : fromName.title;
    return meta;
}

Category MusicLibrary::findCategory(const juce::String& name) {
    Category found { -1, {}, 0, 1 };
    if (name.isEmpty()) return found;

    const auto normalized = forceUpperCase(name);
    for (const auto& c : db.getCategories()) {
        const juce::String catName(c.name);
        if (catName.equalsIgnoreCase(name) || forceUpperCase(catName) == normalized)
            return c;
    }
    return found;
}

static void applyAnalysis(TrackInfo& track, const TrackAnalysis& analysis) {
    track.duration = analysis.lengthSeconds;
    track.loudnessLufs = analysis.loudnessLufs;
    track.truePeakDb = analysis.truePeakDb;
    track.cueIn = analysis.cueIn;
    track.cueOut = analysis.cueOut;
    track.normalizationGain = TrackAnalyzer::gainFor(analysis.loudnessLufs, analysis.truePeakDb);
    track.analysisVersion = TrackAnalyzer::version;
}

static std::string hashOf(const juce::File& file) {
    return juce::SHA256(file).toHexString().toStdString();
}

// Formatos que JUCE decodifica por su cuenta (el MP3 con JUCE_USE_MP3AUDIOFORMAT).
static bool hasNativeDecoder(const juce::AudioFormatReader& reader) {
    for (auto* name : { "WAV file", "AIFF file", "FLAC file", "Ogg-Vorbis file", "MP3 file" })
        if (reader.getFormatName() == name) return true;
    return false;
}

static bool writeFlacCopy(juce::AudioFormatReader& reader, const juce::File& dest) {
    juce::FlacAudioFormat flac;
    dest.deleteFile();
    auto stream = dest.createOutputStream();
    if (stream == nullptr) return false;

    const auto channels = juce::jmin(2u, reader.numChannels);  // el deck reproduce dos canales
    std::unique_ptr<juce::AudioFormatWriter> writer(
        flac.createWriterFor(stream.get(), reader.sampleRate, channels, reader.bitsPerSample > 16 ? 24 : 16, {}, 0));
    if (writer == nullptr) return false;
    stream.release();
    return writer->writeFromAudioReader(reader, 0, reader.lengthInSamples);
}

MusicLibrary::ImportResult MusicLibrary::processAndAddTrack(const juce::File& sourceFile, const juce::String& targetCategory) {
    juce::Logger::writeToLog("Processing: " + sourceFile.getFileName());

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0) {
        juce::Logger::writeToLog("Import: unreadable file " + sourceFile.getFullPathName());
        return ImportResult::Failed;
    }

    const auto meta = readMetadata(sourceFile, reader->metadataValues);
    const auto hash = hashOf(sourceFile);
    const double duration = (double) reader->lengthInSamples / reader->sampleRate;

    // ¿Ya está en la biblioteca? El mismo archivo, o la misma canción (mismo artista y
    // título, duración parecida) aunque venga con otra codificación. Las cuñas,
    // identificaciones y separadores tienen duraciones fijas y nombres que se repiten (la
    // cuña de este mes se llama igual que la del anterior): ahí solo cuenta el mismo archivo.
    {
        const juce::ScopedLock sl(dbLock);
        const auto category = findCategory(targetCategory);
        auto existing = db.findTrackByHash(hash);
        if (existing.id == -1 && meta.artist != "UNKNOWN" && !category.isSystem())
            existing = db.findTrackByMetadata(meta.artist.toStdString(), meta.title.toStdString(),
                                              duration, duplicateToleranceSeconds);
        if (existing.id != -1) {
            // Importarla en otra categoría la suma a esa categoría, sin copiar el archivo otra vez.
            if (category.id != -1) db.addTrackCategory(existing.id, category.id);
            juce::Logger::writeToLog("Import: duplicate of track " + juce::String(existing.id) + ", skipped: "
                                     + sourceFile.getFileName());
            return ImportResult::Duplicate;
        }
    }

    TrackInfo t {};
    const auto analysis = TrackAnalyzer::analyse(*reader, [] { return juce::Thread::currentThreadShouldExit(); });
    if (!analysis.valid) return ImportResult::Failed;  // ilegible o importación cancelada
    applyAnalysis(t, analysis);

    int format = 1;
    {
        const juce::ScopedLock sl(dbLock);
        const auto category = findCategory(targetCategory);
        if (category.id != -1) format = category.folderFormat;
    }

    juce::String subPath;
    if (format == 0) {
        auto now = juce::Time::getCurrentTime();
        subPath = juce::String(now.getYear()) + "/" +
                  juce::String(now.getMonth() + 1) + "/" +
                  juce::String(now.getDayOfMonth());
    } else {
        subPath = makeSafeFileName(meta.artist);
    }

    juce::String rootFolder = targetCategory.isEmpty() ? "GENERAL" : makeSafeFileName(targetCategory);
    juce::File targetDir = libraryFolder.getChildFile(rootFolder).getChildFile(subPath);
    targetDir.createDirectory();

    // El archivo se copia tal cual: recodificar un MP3 a otro formato con pérdida lo degrada.
    // Lo que solo sabe leer Windows Media (WMA) se pasa a FLAC, que no pierde nada, para que
    // el deck no dependa de Windows Media dentro del hilo de audio.
    const bool copyAsIs = hasNativeDecoder(*reader);
    const auto extension = copyAsIs ? sourceFile.getFileExtension().toLowerCase() : juce::String(".flac");
    const auto baseName = makeSafeFileName(meta.artist + " - " + meta.title);
    juce::File destFile = targetDir.getChildFile(baseName + extension);
    if (destFile.exists())
        destFile = targetDir.getChildFile(baseName + "_" + juce::String(juce::Time::getMillisecondCounter()) + extension);

    const bool stored = copyAsIs ? sourceFile.copyFileTo(destFile) : writeFlacCopy(*reader, destFile);
    reader.reset();
    if (!stored) {
        juce::Logger::writeToLog("Import: could not store " + destFile.getFullPathName());
        destFile.deleteFile();
        return ImportResult::Failed;
    }

    t.path = destFile.getFullPathName().toStdString();
    t.artist = meta.artist.toStdString();
    t.title = meta.title.toStdString();
    t.category = "";
    t.fileHash = hash;

    const juce::ScopedLock sl(dbLock);
    if (!db.addTrack(t)) {
        destFile.deleteFile();
        return ImportResult::Failed;
    }

    const auto category = findCategory(targetCategory);
    if (category.id != -1) {
        const auto inserted = db.getTrackByPath(t.path);
        if (inserted.id != -1) db.setTrackCategories(inserted.id, { category.id });
    }
    return ImportResult::Added;
}

//==============================================================================
// Análisis

bool MusicLibrary::needsAnalysis(const TrackInfo& track) {
    return track.analysisVersion < TrackAnalyzer::version;
}

bool MusicLibrary::analyseFile(const juce::File& file, TrackInfo& track, const std::function<bool()>& shouldAbort) {
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));
    if (reader == nullptr) return false;

    const auto analysis = TrackAnalyzer::analyse(*reader, shouldAbort);
    if (!analysis.valid) return false;

    applyAnalysis(track, analysis);
    return true;
}

TrackInfo MusicLibrary::ensureAnalysed(const TrackInfo& track, const std::function<bool()>& shouldAbort) {
    if (track.id == -1 || !needsAnalysis(track)) return track;

    const juce::File file(track.path);
    TrackInfo updated = track;
    if (!file.existsAsFile() || !analyseFile(file, updated, shouldAbort)) return track;
    if (updated.fileHash.empty()) updated.fileHash = hashOf(file);

    const juce::ScopedLock sl(dbLock);
    db.updateTrackAnalysis(updated);
    return updated;
}

void MusicLibrary::startBackgroundAnalysis() {
    if (analysisThread != nullptr) return;

    std::vector<int> ids;
    {
        const juce::ScopedLock sl(dbLock);
        ids = db.getTrackIdsNeedingAnalysis(TrackAnalyzer::version);
    }
    {
        const juce::ScopedLock sl(analysisQueueLock);
        analysisQueue.assign(ids.begin(), ids.end());
        pendingAnalysis = (int) analysisQueue.size();
    }
    if (!ids.empty())
        juce::Logger::writeToLog("Analysis: " + juce::String((int) ids.size()) + " tracks pending (loudness and cue points)");

    analysisThread = std::make_unique<AnalysisThread>(*this);
    analysisThread->startThread(juce::Thread::Priority::low);
}

void MusicLibrary::stopBackgroundAnalysis() {
    if (analysisThread == nullptr) return;
    analysisThread->signalThreadShouldExit();
    analysisThread->notify();
    analysisThread->stopThread(5000);
    analysisThread.reset();
}

void MusicLibrary::prioritiseAnalysis(const std::vector<TrackInfo>& upcoming) {
    if (analysisThread == nullptr) return;  // sin análisis en segundo plano, nadie atendería la fila

    bool added = false;
    {
        const juce::ScopedLock sl(analysisQueueLock);
        // Al revés, para que la primera de la lista quede primera en la fila.
        for (auto it = upcoming.rbegin(); it != upcoming.rend(); ++it) {
            if (it->id == -1 || !needsAnalysis(*it)) continue;
            auto found = std::find(analysisQueue.begin(), analysisQueue.end(), it->id);
            if (found != analysisQueue.end()) analysisQueue.erase(found);
            analysisQueue.push_front(it->id);
            added = true;
        }
        pendingAnalysis = (int) analysisQueue.size();
    }
    if (added && analysisThread != nullptr) analysisThread->notify();
}

int MusicLibrary::takeNextAnalysisId() {
    const juce::ScopedLock sl(analysisQueueLock);
    if (analysisQueue.empty()) return -1;
    const int id = analysisQueue.front();
    analysisQueue.pop_front();
    pendingAnalysis = (int) analysisQueue.size();
    return id;
}

void MusicLibrary::runBackgroundAnalysis(juce::Thread& thread) {
    while (!thread.threadShouldExit()) {
        const int id = takeNextAnalysisId();
        if (id < 0) {
            thread.wait(-1);  // hasta que llegue un pedido (o el cierre)
            continue;
        }

        TrackInfo track;
        {
            const juce::ScopedLock sl(dbLock);
            track = db.getTrackById(id);
        }
        if (track.id == -1 || !needsAnalysis(track)) continue;  // borrada, o ya la analizó el deck

        // Un archivo que no está (unidad desconectada) se reintenta en el próximo inicio.
        const juce::File file(track.path);
        if (!file.existsAsFile()) continue;

        const auto updated = ensureAnalysed(track, [&thread] { return thread.threadShouldExit(); });
        if (needsAnalysis(updated) && !thread.threadShouldExit())
            juce::Logger::writeToLog("Analysis: could not read " + file.getFullPathName());

        // Pausa entre pistas: el deck lee del mismo disco (a veces una memoria USB).
        thread.wait(300);
    }
}

std::vector<TrackInfo> MusicLibrary::search(const juce::String& query, const juce::String& categoryFilter) {
    const juce::ScopedLock sl(dbLock);
    return db.searchTracks(query.toStdString(), categoryFilter.toStdString());
}

TrackInfo MusicLibrary::getTrackByPath(const juce::String& path) {
    const juce::ScopedLock sl(dbLock);
    return db.getTrackByPath(path.toStdString());
}

 
bool MusicLibrary::addCategory(const juce::String& name) {
    const juce::ScopedLock sl(dbLock);
    return db.addCategory(name.toStdString());
}

bool MusicLibrary::renameCategory(const juce::String& oldName, const juce::String& newName) {
    const juce::ScopedLock sl(dbLock);
    return db.renameCategory(oldName.toStdString(), newName.toStdString());
}

bool MusicLibrary::deleteCategory(const juce::String& name) {
    const juce::ScopedLock sl(dbLock);
    return db.deleteCategory(name.toStdString());
}

std::vector<Category> MusicLibrary::getCategories() {
    const juce::ScopedLock sl(dbLock);
    return db.getCategories();
}

Category MusicLibrary::getSystemCategory(CategoryRole role) {
    const juce::ScopedLock sl(dbLock);
    return db.getSystemCategory(role);
}

std::vector<TrackInfo> MusicLibrary::getTracksWithRole(CategoryRole role) {
    const juce::ScopedLock sl(dbLock);
    const auto category = db.getSystemCategory(role);
    if (category.id == -1) return {};  // un filtro vacío traería toda la biblioteca
    return db.searchTracks("", category.name);
}

juce::String MusicLibrary::getDisplayName(const Category& category) {
    switch (category.role) {
        case CategoryRole::StationIds: return TRANS("cat_station_ids");
        case CategoryRole::Spots:      return TRANS("cat_spots");
        case CategoryRole::Sweepers:   return TRANS("cat_sweepers");
        default:                       return juce::String(category.name);
    }
}

TrackInfo MusicLibrary::getTrackById(int id) {
    juce::ScopedLock lock(dbLock);
    return db.getTrackById(id);
}

void MusicLibrary::incrementPlayCount(int id) {
    juce::ScopedLock lock(dbLock);
    db.incrementPlayCount(id);
}

bool MusicLibrary::updateCategoryOrder(const juce::StringArray& names) {
    juce::ScopedLock lock(dbLock);
    std::vector<std::string> stdNames;
    for (const auto& n : names) stdNames.push_back(n.toStdString());
    return db.updateCategoryOrder(stdNames);
}

void MusicLibrary::deleteTrack(int id, bool deleteFile) {
    juce::ScopedLock lock(dbLock);
    
    if (deleteFile) {
        TrackInfo t = db.getTrackById(id);
        if (t.id != -1) {
            juce::File f(t.path);
            if (f.exists()) {
                bool deleted = f.deleteFile();
                if (!deleted) {
                    juce::Logger::writeToLog("Failed to delete file: " + t.path);
                } else {
                    juce::Logger::writeToLog("Deleted file: " + t.path);
                }
            }
        }
    }
    
    db.deleteTrack(id);
}

 

MusicLibrary::RenameResult MusicLibrary::renameTrack(int id, const juce::String& newArtist, const juce::String& newTitle) {
    juce::ScopedLock sl(dbLock);
    
     
    TrackInfo info = db.getTrackById(id);  
    if (info.id == -1) return {false, "Track not found"};
    
    juce::File currentFile(info.path);
    if (!currentFile.exists()) return {false, "Source file not found"};
    
     
    auto sanitize = [](juce::String s) {
        return s.replaceCharacter('/', '_').replaceCharacter('\\', '_')
                .replaceCharacter(':', '_').replaceCharacter('*', '_')
                .replaceCharacter('?', '_').replaceCharacter('"', '_')
                .replaceCharacter('<', '_').replaceCharacter('>', '_')
                .replaceCharacter('|', '_');
    };
    
    juce::String safeArtist = sanitize(newArtist).trim();
    juce::String safeTitle = sanitize(newTitle).trim();
    
    if (safeArtist.isEmpty() || safeTitle.isEmpty()) return {false, "Artist or Title cannot be empty"};
    
    juce::String newFileName = safeArtist + " - " + safeTitle + currentFile.getFileExtension();
    juce::File newFile = currentFile.getParentDirectory().getChildFile(newFileName);
    
     
    if (newFile != currentFile && newFile.exists()) {
        return {false, "File already exists: " + newFileName};
    }
    
     
    if (newFile != currentFile) {
        if (!currentFile.moveFileTo(newFile)) {
            return {false, "Failed to move/rename file"};
        }
    }
    
     
    if (db.updateTrackDetails(id, safeArtist.toStdString(), safeTitle.toStdString(), newFile.getFullPathName().toStdString())) {
        return {true, ""};
    } else {
        return {false, "Database update failed"};
    }
}

bool MusicLibrary::updateTrackMetadata(int id, const juce::String& artist, const juce::String& title) {
    juce::ScopedLock sl(dbLock);
    
    TrackInfo info = db.getTrackById(id);
    if (info.id == -1) return false;

    juce::String upperArtist = forceUpperCase(artist.trim());
    juce::String upperTitle = forceUpperCase(title.trim());

    if (upperArtist.isEmpty() || upperTitle.isEmpty()) return false;

    return db.updateTrackDetails(id, upperArtist.toStdString(), upperTitle.toStdString(), info.path);
}


std::vector<int> MusicLibrary::getTrackCategories(int trackId) {
    const juce::ScopedLock sl(dbLock);
    return db.getTrackCategories(trackId);
}

std::unordered_map<int, std::vector<int>> MusicLibrary::getAllTrackCategories() {
    const juce::ScopedLock sl(dbLock);
    return db.getAllTrackCategories();
}

bool MusicLibrary::setTrackCategories(int trackId, const std::vector<int>& categoryIds) {
    const juce::ScopedLock sl(dbLock);
    return db.setTrackCategories(trackId, categoryIds);
}
bool MusicLibrary::addPlayLogEntry(const PlayLogEntry& entry) {
    const juce::ScopedLock sl(dbLock);
    return db.addPlayLogEntry(entry);
}

std::vector<PlayLogEntry> MusicLibrary::getRecentPlayLog(int limit) {
    const juce::ScopedLock sl(dbLock);
    return db.getRecentPlayLog(limit);
}

bool MusicLibrary::setCategoryFolderFormat(const juce::String& name, int format) {
    juce::ScopedLock sl(dbLock);
    return db.setCategoryFolderFormat(name.toStdString(), format);
}
