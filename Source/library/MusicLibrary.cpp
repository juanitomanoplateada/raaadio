#include "MusicLibrary.h"
#include "TagReader.h"
#include "../audio/TrackAnalyzer.h"
#include "../audio/AcousticFingerprint.h"
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

    // Una sola vez: la pauta, la cola, la botonera, los ajustes y las plantillas pasan de
    // Config/*.json y Templates/*.json a la base. También sirve para un respaldo viejo que
    // se restauró (su base es del esquema 1 y trae esos archivos).
    if (db.getSchemaVersion() < 2)
        db.importLegacyStationFiles(PersistenceManager::getConfigFolder(), PersistenceManager::getTemplatesFolder());

    // Una sola vez (esquema 3): claves para reconocer canciones sin importar mayúsculas ni
    // tildes. Una biblioteca que ya existía se guardó toda en mayúsculas: se sigue viendo así
    // hasta que la emisora lo cambie en Ajustes.
    if (db.getSchemaVersion() < 3 && db.migrateCatalog() > 0)
        db.setSetting("library.displayUppercase", "1");
    TrackNaming::setDisplayUppercase(db.getSetting("library.displayUppercase") == "1");
    db.ensureSearchIndex();

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

    if (const int moved = relocateMisplacedTracks(); moved > 0)
        juce::Logger::writeToLog("MusicLibrary: " + juce::String(moved) + " confirmed tracks moved to their folder");
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
    importToReview = 0;
    ++importCount;
    currentImportThread = std::make_unique<ImportThread>(*this, files, targetCategory);
    currentImportThread->startThread();
}

MusicLibrary::DownloadImport MusicLibrary::importDownloaded(const juce::File& file, const juce::String& batchSource,
                                                            const juce::String& isrc) {
    const juce::ScopedLock il(importLock);
    DownloadImport result;
    int batch = 0;
    {
        const juce::ScopedLock sl(dbLock);
        batch = juce::jmax(0, db.createImportBatch(batchSource.toStdString(), -1, 1));
    }
    result.result = processAndAddTrack(file, "", batch);
    if (result.result == ImportResult::Added) {
        const juce::ScopedLock sl(dbLock);
        const auto ids = db.getPendingTracksOfBatch(batch);
        if (!ids.empty()) {
            result.trackId = ids.front();
            if (isrc.isNotEmpty()) db.setOnlineIds(result.trackId, "", isrc.toStdString());
        }
    }
    return result;
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
    return { importAdded.load(), importDuplicates.load(), importFailed.load(), importToReview.load() };
}


MusicLibrary::ImportThread::ImportThread(MusicLibrary& owner, const juce::Array<juce::File>& filesToImport, const juce::String& cat)
    : Thread("Library Import"), library(owner), files(filesToImport), targetCategory(cat) {}

void MusicLibrary::ImportThread::run() {
    const juce::ScopedLock il(library.importLock);
    int totalFiles = library.countFiles(files);
    int currentFileIndex = 0;

    // La música de una importación forma un lote (para revisarla junta).
    int batch = 0;
    {
        const juce::ScopedLock sl(library.dbLock);
        const auto category = library.findCategory(targetCategory);
        if (!category.isSystem() && totalFiles > 0) {
            juce::StringArray sources;
            for (const auto& f : files) sources.add(f.getFullPathName());
            batch = juce::jmax(0, library.db.createImportBatch(sources.joinIntoString("\n").toStdString(), category.id, totalFiles));
        }
    }

    // Los artistas conocidos se leen una vez por importación, no una por archivo.
    library.importContext = std::make_unique<TrackNaming::Context>(library.makeNamingContext({}));
    for (const auto& f : files) {
        if (threadShouldExit()) break;
        library.scanAndImport(f, currentFileIndex, totalFiles, targetCategory, batch);
    }
    library.importContext.reset();
    ++library.catalogVersion;
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

void MusicLibrary::scanAndImport(const juce::File& file, int& current, int total, const juce::String& targetCategory, int batch) {
    if (juce::Thread::currentThreadShouldExit()) return;

    if (file.isDirectory()) {
        auto children = file.findChildFiles(juce::File::findFiles, true);
        for (const auto& child : children) {
            if (juce::Thread::currentThreadShouldExit()) return;
            scanAndImport(child, current, total, targetCategory, batch);
        }
    } else if (formatManager.findFormatForFileExtension(file.getFileExtension()) != nullptr) {
        // Carátulas, listas y otros archivos de la carpeta se ignoran.
        switch (processAndAddTrack(file, targetCategory, batch)) {
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

// Un archivo libre en folder con ese nombre: "nombre.ext", "nombre (2).ext"...
static juce::File freeFileIn(const juce::File& folder, const juce::String& baseName, const juce::String& extension) {
    auto file = folder.getChildFile(baseName + extension);
    for (int n = 2; file.exists() && n < 1000; ++n)
        file = folder.getChildFile(baseName + " (" + juce::String(n) + ")" + extension);
    return file;
}

MusicLibrary::ImportResult MusicLibrary::processAndAddTrack(const juce::File& sourceFile, const juce::String& targetCategory, int batch) {
    juce::Logger::writeToLog("Processing: " + sourceFile.getFileName());

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(sourceFile));
    if (reader == nullptr || reader->sampleRate <= 0) {
        juce::Logger::writeToLog("Import: unreadable file " + sourceFile.getFullPathName());
        return ImportResult::Failed;
    }

    // Artista y título propuestos: de las etiquetas o del nombre. En la música es solo una
    // primera lectura; la revisión la corrige viendo el lote completo.
    const auto tags = TagReader::read(sourceFile, reader->metadataValues);
    TrackNaming::Source source;
    source.fileName = sourceFile.getFileNameWithoutExtension();
    source.group = sourceFile.getParentDirectory().getFullPathName();
    source.tagArtist = tags.artist;
    source.tagTitle = tags.title;

    Category category;
    TrackNaming::Proposal proposal;
    {
        const juce::ScopedLock sl(dbLock);
        category = findCategory(targetCategory);
    }
    {
        // Un artista que ya está en la biblioteca dice cómo leer el nombre ("Volveré - Wilfrido Vargas").
        const auto own = importContext == nullptr ? makeNamingContext({}) : TrackNaming::Context();
        const auto& context = importContext != nullptr ? *importContext : own;
        const auto orientation = TrackNaming::orientationEvidence(source, context) > 0 ? TrackNaming::Orientation::TitleArtist
                                                                                        : TrackNaming::Orientation::ArtistTitle;
        proposal = TrackNaming::propose(source, orientation, context);
    }
    const bool music = !category.isSystem();

    const auto hash = hashOf(sourceFile);
    const double duration = (double) reader->lengthInSamples / reader->sampleRate;

    // ¿Ya está en la biblioteca? El mismo archivo, o la misma canción (mismo artista y
    // título, duración parecida) aunque venga con otra codificación. Las cuñas,
    // identificaciones y separadores tienen duraciones fijas y nombres que se repiten (la
    // cuña de este mes se llama igual que la del anterior): ahí solo cuenta el mismo archivo.
    {
        const juce::ScopedLock sl(dbLock);
        auto existing = db.findTrackByHash(hash);
        if (existing.id == -1 && music && proposal.artist.isNotEmpty())
            existing = db.findDuplicate(proposal.key().toStdString(), duration, duplicateToleranceSeconds, -1);
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

    // Huella acústica de la música: la revisión la usa para reconocer la misma grabación con
    // otro nombre o en otro formato.
    std::vector<uint32_t> fingerprint;
    if (music)
        fingerprint = AcousticFingerprint::compute(*reader, t.cueIn, t.getAirDuration(),
                                                   [] { return juce::Thread::currentThreadShouldExit(); });

    // El archivo se copia tal cual: recodificar un MP3 a otro formato con pérdida lo degrada.
    // Lo que solo sabe leer Windows Media (WMA) se pasa a FLAC, que no pierde nada, para que
    // el deck no dependa de Windows Media dentro del hilo de audio.
    const bool copyAsIs = hasNativeDecoder(*reader);
    const auto extension = copyAsIs ? sourceFile.getFileExtension().toLowerCase() : juce::String(".flac");

    juce::File destFile;
    if (music) {
        // Por revisar: en _Entrada con su nombre original, hasta que se confirme.
        const auto inbox = libraryFolder.getChildFile("_Entrada").getChildFile(batch > 0 ? juce::String(batch) : juce::String("0"));
        inbox.createDirectory();
        destFile = freeFileIn(inbox, makeSafeFileName(sourceFile.getFileNameWithoutExtension()), extension);
    } else {
        juce::String subPath;
        if (category.folderFormat == 0) {
            auto now = juce::Time::getCurrentTime();
            subPath = juce::String(now.getYear()) + "/" + juce::String(now.getMonth() + 1) + "/" + juce::String(now.getDayOfMonth());
        } else {
            subPath = makeSafeFileName(proposal.artist.isNotEmpty() ? proposal.artist : juce::String("_"));
        }
        const auto targetDir = libraryFolder.getChildFile(makeSafeFileName(category.name)).getChildFile(subPath);
        targetDir.createDirectory();
        const auto baseName = proposal.artist.isNotEmpty() ? proposal.credit() + " - " + proposal.fullTitle() : proposal.fullTitle();
        destFile = freeFileIn(targetDir, makeSafeFileName(baseName), extension);
    }

    const bool stored = copyAsIs ? sourceFile.copyFileTo(destFile) : writeFlacCopy(*reader, destFile);
    reader.reset();
    if (!stored) {
        juce::Logger::writeToLog("Import: could not store " + destFile.getFullPathName());
        destFile.deleteFile();
        return ImportResult::Failed;
    }

    t.path = destFile.getFullPathName().toStdString();
    t.artist = proposal.credit().toStdString();
    t.title = (proposal.fullTitle().isNotEmpty() ? proposal.fullTitle() : source.fileName).toStdString();
    t.version = proposal.version.toStdString();
    t.year = proposal.year;
    t.status = music ? 1 : 0;
    t.category = "";
    t.fileHash = hash;

    const juce::ScopedLock sl(dbLock);
    if (!db.addTrack(t)) {
        destFile.deleteFile();
        return ImportResult::Failed;
    }

    const auto inserted = db.getTrackByPath(t.path);
    if (inserted.id != -1) {
        if (category.id != -1) db.setTrackCategories(inserted.id, { category.id });
        if (music) {
            Database::ImportSource origin;
            origin.name = source.fileName.toStdString();
            origin.folder = source.group.toStdString();
            origin.tagArtist = source.tagArtist.toStdString();
            origin.tagTitle = source.tagTitle.toStdString();
            origin.batch = batch;
            db.setImportSource(inserted.id, origin);
            db.setFingerprint(inserted.id, fingerprint, AcousticFingerprint::version);
            ++importToReview;
        }
    }
    ++catalogVersion;
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
    // Primero, lo que un respaldo viejo dejó apuntando a donde estaban los archivos.
    repairMissingFiles([&thread] { return thread.threadShouldExit(); });

    // Huellas acústicas de la música que no la tiene (de versiones anteriores, o recién
    // analizada): después del análisis, que va primero porque lo pide lo que está por sonar.
    std::deque<int> fingerprintQueue;
    std::set<int> fingerprintSkipped;  // archivos que no están: se reintentan en el próximo inicio
    bool refillFingerprints = true;

    while (!thread.threadShouldExit()) {
        const int id = takeNextAnalysisId();
        if (id < 0) {
            if (fingerprintQueue.empty() && refillFingerprints) {
                std::vector<int> ids;
                {
                    const juce::ScopedLock sl(dbLock);
                    ids = db.getTrackIdsNeedingFingerprint(AcousticFingerprint::version);
                }
                for (int fid : ids)
                    if (fingerprintSkipped.count(fid) == 0) fingerprintQueue.push_back(fid);
                refillFingerprints = false;
            }
            if (!fingerprintQueue.empty()) {
                const int fid = fingerprintQueue.front();
                fingerprintQueue.pop_front();
                if (!computeFingerprint(fid)) fingerprintSkipped.insert(fid);
                thread.wait(300);
                continue;
            }
            thread.wait(-1);  // hasta que llegue un pedido (o el cierre)
            continue;
        }
        refillFingerprints = true;  // lo que se analice ahora también necesita su huella

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

 

//==============================================================================
// Catálogo y revisión

bool MusicLibrary::isInsideLibrary(const juce::File& file) const {
    return file.isAChildOf(libraryFolder);
}

bool MusicLibrary::isSystemTrack(int trackId) {
    const auto categories = db.getCategories();
    for (int id : db.getTrackCategories(trackId))
        for (const auto& c : categories)
            if (c.id == id && c.isSystem()) return true;
    return false;
}

juce::File MusicLibrary::musicDestination(const TrackNaming::Proposal& values, const juce::String& extension) const {
    const auto folder = libraryFolder.getChildFile("Musica").getChildFile(makeSafeFileName(values.artist.trim()));
    return folder.getChildFile(makeSafeFileName(values.credit() + " - " + values.fullTitle()) + extension);
}

// ¿Ya está donde debe? target, o "target (2)" en la misma carpeta (otra canción ocupaba el nombre).
static bool isPlacedAt(const juce::File& current, const juce::File& target) {
    if (current == target) return true;
    if (current.getParentDirectory() != target.getParentDirectory()
        || current.getFileExtension() != target.getFileExtension()) return false;
    const auto stem = target.getFileNameWithoutExtension();
    const auto name = current.getFileNameWithoutExtension();
    if (!name.startsWith(stem + " (") || !name.endsWithChar(')')) return false;
    return name.substring(stem.length() + 2, name.length() - 1).containsOnly("0123456789");
}

juce::File MusicLibrary::placeFile(const juce::File& current, const juce::File& target, bool& moved) {
    moved = false;
    if (isPlacedAt(current, target) || !current.existsAsFile()) return current;

    const auto folder = target.getParentDirectory();
    if (!folder.createDirectory().wasOk()) return current;
    const auto destination = freeFileIn(folder, target.getFileNameWithoutExtension(), target.getFileExtension());
    // Un archivo que está sonando no se puede mover en Windows: queda donde estaba.
    if (!current.moveFileTo(destination)) {
        juce::Logger::writeToLog("Library: could not move " + current.getFullPathName() + " (in use?)");
        return current;
    }
    moved = true;
    removeEmptyFolders(current.getParentDirectory());
    return destination;
}

void MusicLibrary::removeEmptyFolders(juce::File folder) {
    while (folder.isAChildOf(libraryFolder) && folder.isDirectory()
           && folder.getNumberOfChildFiles(juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles) == 0) {
        const auto parent = folder.getParentDirectory();
        if (!folder.deleteFile()) break;
        folder = parent;
    }
}

void MusicLibrary::notifyRelocated(int trackId, const juce::File& file) {
    auto callback = onTrackRelocated;
    if (!callback) return;
    const auto path = file.getFullPathName();
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr
        && juce::MessageManager::getInstance()->isThisTheMessageThread())
        callback(trackId, path);
    else
        juce::MessageManager::callAsync([callback, trackId, path] { callback(trackId, path); });
}

MusicLibrary::SaveResult MusicLibrary::confirmTrack(int id, const TrackNaming::Proposal& input, const juce::String& aliasFrom) {
    SaveResult result;

    auto values = input;
    values.artist = values.artist.trim();
    values.title = values.title.trim();
    values.version = values.version.trim();
    for (auto* list : { &values.featured, &values.composers }) {
        list->trim();
        list->removeEmptyStrings();
        list->removeDuplicates(true);
    }

    TrackInfo track;
    bool system = false;
    {
        const juce::ScopedLock sl(dbLock);
        track = db.getTrackById(id);
        if (track.id != -1) system = isSystemTrack(id);
    }
    if (track.id == -1) { result.error = "not found"; return result; }
    if (values.title.isEmpty() || (!system && values.artist.isEmpty())) { result.error = "missing"; return result; }

    // Cuñas, identificaciones y separadores: solo artista y título, sin mover el archivo.
    if (system) {
        const juce::ScopedLock sl(dbLock);
        result.ok = db.setTrackNames(id, values.credit().toStdString(), values.fullTitle().toStdString());
        if (result.ok) ++catalogVersion;
        return result;
    }

    // Música: a Library/Musica/<artista>/<artista> - <título>. Un archivo fuera de la
    // carpeta de la emisora no se mueve.
    const juce::File current(juce::String::fromUTF8(track.path.c_str()));
    auto finalFile = current;
    if (isInsideLibrary(current) && current.existsAsFile()) {
        const auto target = musicDestination(values, current.getFileExtension());
        finalFile = placeFile(current, target, result.fileMoved);
        result.moveDeferred = !result.fileMoved && !isPlacedAt(current, target);
    }

    Database::CatalogEntry entry;
    entry.artist = values.artist.toStdString();
    for (const auto& f : values.featured) entry.featured.push_back(f.toStdString());
    for (const auto& c : values.composers) entry.composers.push_back(c.toStdString());
    entry.title = values.title.toStdString();
    entry.version = values.version.toStdString();
    entry.year = values.year;
    entry.displayArtist = values.credit().toStdString();
    entry.displayTitle = values.fullTitle().toStdString();
    entry.path = finalFile.getFullPathName().toStdString();
    // Cómo venía escrito el artista, si el operador lo corrigió sin tocar el título (si
    // cambió todo, no era el mismo dato: no se aprende nada).
    entry.aliasKey = TrackNaming::key(aliasFrom).toStdString();

    {
        const juce::ScopedLock sl(dbLock);
        result.ok = db.confirmTrack(id, entry);
    }

    if (!result.ok) {
        if (result.fileMoved) {
            finalFile.moveFileTo(current);  // todo o nada: el archivo vuelve a su lugar
            result.fileMoved = false;
        }
        result.error = "database";
        return result;
    }
    if (result.fileMoved) notifyRelocated(id, finalFile);
    ++catalogVersion;
    return result;
}

bool MusicLibrary::discardTrack(int id, int mergeInto) {
    TrackInfo track;
    {
        const juce::ScopedLock sl(dbLock);
        track = db.getTrackById(id);
        if (track.id == -1) return false;
        if (mergeInto >= 0 && mergeInto != id && db.getTrackById(mergeInto).id != -1)
            for (int category : db.getTrackCategories(id)) db.addTrackCategory(mergeInto, category);
    }

    // El archivo no se borra: queda en _Descartadas por si fue un error.
    const juce::File file(juce::String::fromUTF8(track.path.c_str()));
    if (isInsideLibrary(file) && file.existsAsFile()) {
        const auto bin = libraryFolder.getChildFile("_Descartadas").getChildFile(juce::Time::getCurrentTime().formatted("%Y-%m-%d"));
        bin.createDirectory();
        const auto destination = freeFileIn(bin, file.getFileNameWithoutExtension(), file.getFileExtension());
        if (!file.moveFileTo(destination)) {
            juce::Logger::writeToLog("Library: could not move " + file.getFullPathName() + " to the discarded folder");
            return false;
        }
        removeEmptyFolders(file.getParentDirectory());
    }

    bool ok;
    {
        const juce::ScopedLock sl(dbLock);
        ok = db.deleteTrack(id);
    }
    ++catalogVersion;
    return ok;
}

TrackNaming::Proposal MusicLibrary::getTrackCredits(const TrackInfo& track) {
    std::vector<Database::Credit> credits;
    {
        const juce::ScopedLock sl(dbLock);
        credits = db.getTrackCredits(track.id);
    }

    TrackNaming::Proposal p;
    p.version = juce::String::fromUTF8(track.version.c_str());
    p.year = track.year;
    auto title = juce::String::fromUTF8(track.title.c_str());
    if (p.version.isNotEmpty() && title.endsWith(" (" + p.version + ")")) title = title.dropLastCharacters(p.version.length() + 3);
    p.title = title;

    for (const auto& c : credits) {
        const auto name = juce::String::fromUTF8(c.name.c_str());
        if (c.role == Database::CreditRole::Main && p.artist.isEmpty()) p.artist = name;
        else if (c.role == Database::CreditRole::Featured) p.featured.add(name);
        else if (c.role == Database::CreditRole::Composer) p.composers.add(name);
    }
    // Sin créditos (pista de una versión anterior, o una cuña): el artista tal como se guardó.
    if (p.artist.isEmpty())
        p.artist = juce::String::fromUTF8(track.artist.c_str()).upToFirstOccurrenceOf(" feat. ", false, false);
    return p;
}

std::vector<Database::ReviewRecord> MusicLibrary::getReviewRecords(bool pending) {
    const juce::ScopedLock sl(dbLock);
    return db.getReviewRecords(pending);
}

bool MusicLibrary::saveReviewDraft(int trackId, const juce::String& json) {
    const juce::ScopedLock sl(dbLock);
    return db.saveReviewDraft(trackId, json.toStdString());
}

int MusicLibrary::getPendingCount() {
    const juce::ScopedLock sl(dbLock);
    return db.countPending();
}

int MusicLibrary::getUnreviewedCount() {
    const juce::ScopedLock sl(dbLock);
    return db.countUnreviewed();
}

TrackNaming::Context MusicLibrary::makeNamingContext(const std::vector<TrackNaming::Source>& batch) {
    std::map<juce::String, TrackNaming::KnownArtist> known;
    std::map<juce::String, juce::String> aliases;
    juce::StringArray categoryNames;
    {
        const juce::ScopedLock sl(dbLock);
        for (const auto& a : db.getKnownArtists())
            known[juce::String::fromUTF8(a.key.c_str())] = { juce::String::fromUTF8(a.name.c_str()), a.tracks };
        for (const auto& [alias, artistKey] : db.getArtistAliases())
            aliases[juce::String::fromUTF8(alias.c_str())] = juce::String::fromUTF8(artistKey.c_str());
        for (const auto& c : db.getCategories())
            if (!c.isSystem()) categoryNames.add(juce::String::fromUTF8(c.name.c_str()));
    }
    return TrackNaming::Context(std::move(known), std::move(aliases), categoryNames, batch);
}

int MusicLibrary::relocateMisplacedTracks() {
    std::vector<Database::Placement> placements;
    {
        const juce::ScopedLock sl(dbLock);
        placements = db.getConfirmedMusicPlacement();
    }

    int moved = 0;
    for (const auto& p : placements) {
        const juce::File current(juce::String::fromUTF8(p.path.c_str()));
        if (!isInsideLibrary(current) || !current.existsAsFile()) continue;

        const auto folder = libraryFolder.getChildFile("Musica").getChildFile(makeSafeFileName(juce::String::fromUTF8(p.mainArtist.c_str())));
        const auto name = juce::String::fromUTF8(p.artist.c_str()) + " - " + juce::String::fromUTF8(p.title.c_str());
        const auto target = folder.getChildFile(makeSafeFileName(name) + current.getFileExtension());

        bool didMove = false;
        const auto placed = placeFile(current, target, didMove);
        if (!didMove) continue;

        bool saved;
        {
            const juce::ScopedLock sl(dbLock);
            saved = db.setTrackPath(p.id, placed.getFullPathName().toStdString());
        }
        if (!saved) { placed.moveFileTo(current); continue; }
        notifyRelocated(p.id, placed);
        ++moved;
    }
    return moved;
}

int MusicLibrary::repairMissingFiles(const std::function<bool()>& shouldStop) {
    // Huella → pistas cuyo archivo (dentro de la carpeta de la emisora) ya no está.
    std::map<std::string, std::vector<int>> missing;
    std::set<juce::String> referenced;
    for (const auto& t : search("", "")) {
        const juce::File file(juce::String::fromUTF8(t.path.c_str()));
        referenced.insert(file.getFullPathName().toLowerCase());
        if (!t.fileHash.empty() && isInsideLibrary(file) && !file.existsAsFile())
            missing[t.fileHash].push_back(t.id);
    }
    if (missing.empty()) return 0;

    int repaired = 0;
    const auto music = libraryFolder.getChildFile("Musica");
    for (const auto& file : music.findChildFiles(juce::File::findFiles, true)) {
        if (missing.empty() || (shouldStop && shouldStop())) break;
        if (referenced.count(file.getFullPathName().toLowerCase()) > 0) continue;
        if (formatManager.findFormatForFileExtension(file.getFileExtension()) == nullptr) continue;

        const auto found = missing.find(hashOf(file));
        if (found == missing.end()) continue;
        for (int id : found->second) {
            bool saved;
            {
                const juce::ScopedLock sl(dbLock);
                saved = db.setTrackPath(id, file.getFullPathName().toStdString());
            }
            if (!saved) continue;
            notifyRelocated(id, file);
            ++repaired;
        }
        missing.erase(found);
    }
    if (repaired > 0) {
        juce::Logger::writeToLog("Library: " + juce::String(repaired) + " tracks found again in Musica by their fingerprint");
        ++catalogVersion;
    }
    return repaired;
}

bool MusicLibrary::isDisplayUppercase() {
    return TrackNaming::isDisplayUppercase();
}

void MusicLibrary::setDisplayUppercase(bool uppercase) {
    TrackNaming::setDisplayUppercase(uppercase);
    const juce::ScopedLock sl(dbLock);
    db.setSetting("library.displayUppercase", uppercase ? "1" : "0");
}

std::vector<int> MusicLibrary::getTrackCategories(int trackId) {
    const juce::ScopedLock sl(dbLock);
    return db.getTrackCategories(trackId);
}

std::unordered_map<int, std::vector<int>> MusicLibrary::getAllTrackCategories() {
    const juce::ScopedLock sl(dbLock);
    return db.getAllTrackCategories();
}

std::unordered_map<int, CategoryRole> MusicLibrary::getSystemRolesByTrack() {
    const juce::ScopedLock sl(dbLock);
    return db.getSystemRolesByTrack();
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

//==============================================================================
// Huella acústica

bool MusicLibrary::computeFingerprint(int trackId) {
    TrackInfo track;
    {
        const juce::ScopedLock sl(dbLock);
        track = db.getTrackById(trackId);
    }
    const juce::File file(juce::String::fromUTF8(track.path.c_str()));
    if (track.id == -1 || !file.existsAsFile()) return false;

    std::vector<uint32_t> fingerprint;
    if (std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file)); reader != nullptr)
        fingerprint = AcousticFingerprint::compute(*reader, track.cueIn, track.getAirDuration(),
                                                   [] { return juce::Thread::currentThreadShouldExit(); });
    if (juce::Thread::currentThreadShouldExit()) return false;

    // Aunque salga vacía (un archivo que no se puede leer) queda anotada: no se vuelve a intentar.
    const juce::ScopedLock sl(dbLock);
    db.setFingerprint(trackId, fingerprint, AcousticFingerprint::version);
    return !fingerprint.empty();
}

//==============================================================================
// Artistas

std::vector<Database::ArtistOverview> MusicLibrary::getArtists() {
    const juce::ScopedLock sl(dbLock);
    return db.getArtistsOverview();
}

std::vector<TrackInfo> MusicLibrary::getTracksOfArtist(int artistId) {
    const juce::ScopedLock sl(dbLock);
    std::vector<TrackInfo> tracks;
    for (int id : db.getTracksOfArtist(artistId)) {
        const auto t = db.getTrackById(id);
        if (t.id != -1) tracks.push_back(t);
    }
    return tracks;
}

MusicLibrary::ArtistResult MusicLibrary::refreshCreditedTracks(const std::vector<int>& trackIds) {
    ArtistResult result;
    result.ok = true;
    for (int id : trackIds) {
        const auto track = getTrackById(id);
        if (track.id == -1) continue;

        // El artista para mostrar sale de los créditos, que ya cambiaron.
        const auto values = getTrackCredits(track);
        {
            const juce::ScopedLock sl(dbLock);
            if (!db.setTrackArtistLine(id, values.credit().toStdString(), values.key().toStdString())) {
                result.ok = false;
                continue;
            }
        }
        ++result.tracksUpdated;

        // Y el archivo, a la carpeta del artista con su nombre nuevo.
        const juce::File current(juce::String::fromUTF8(track.path.c_str()));
        if (track.confirmedAt.empty() || !isInsideLibrary(current) || !current.existsAsFile()) continue;
        const auto target = musicDestination(values, current.getFileExtension());
        bool moved = false;
        const auto placed = placeFile(current, target, moved);
        if (!moved) {
            if (!isPlacedAt(current, target)) ++result.movesDeferred;  // estaba sonando
            continue;
        }
        bool saved;
        {
            const juce::ScopedLock sl(dbLock);
            saved = db.setTrackPath(id, placed.getFullPathName().toStdString());
        }
        if (!saved) {
            placed.moveFileTo(current);
            continue;
        }
        notifyRelocated(id, placed);
    }
    ++catalogVersion;
    return result;
}

MusicLibrary::ArtistResult MusicLibrary::renameArtist(int artistId, const juce::String& newName) {
    ArtistResult result;
    std::vector<int> tracks;
    {
        const juce::ScopedLock sl(dbLock);
        tracks = db.getTracksOfArtist(artistId);
        if (!db.renameArtist(artistId, newName.trim().toStdString(), result.conflictId)) return result;
    }
    const auto refreshed = refreshCreditedTracks(tracks);
    result.ok = true;
    result.tracksUpdated = refreshed.tracksUpdated;
    result.movesDeferred = refreshed.movesDeferred;
    return result;
}

MusicLibrary::ArtistResult MusicLibrary::mergeArtists(int fromId, int intoId) {
    ArtistResult result;
    std::vector<int> tracks;
    {
        const juce::ScopedLock sl(dbLock);
        tracks = db.getTracksOfArtist(fromId);
        if (!db.mergeArtists(fromId, intoId)) return result;
    }
    const auto refreshed = refreshCreditedTracks(tracks);
    result.ok = true;
    result.tracksUpdated = refreshed.tracksUpdated;
    result.movesDeferred = refreshed.movesDeferred;
    return result;
}

bool MusicLibrary::removeArtistAlias(const juce::String& aliasKey) {
    const juce::ScopedLock sl(dbLock);
    return db.removeAlias(aliasKey.toStdString());
}

//==============================================================================
// Lotes de importación

std::vector<Database::BatchInfo> MusicLibrary::getImportBatches() {
    const juce::ScopedLock sl(dbLock);
    return db.getImportBatches();
}

int MusicLibrary::undoImportBatch(int batchId) {
    std::vector<int> ids;
    {
        const juce::ScopedLock sl(dbLock);
        ids = db.getPendingTracksOfBatch(batchId);
    }
    int removed = 0;
    for (int id : ids)
        if (discardTrack(id)) ++removed;
    return removed;
}

std::unordered_map<int, std::vector<std::string>> MusicLibrary::getPerformerKeys() {
    const juce::ScopedLock sl(dbLock);
    return db.getPerformerKeys();
}
