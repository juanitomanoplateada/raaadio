#include "MainComponent.h"
#include "ui/AudioSettingsDialog.h"
#include "helpers/ThemeManager.h"
#include "helpers/PersistenceManager.h"
#include "helpers/UiStyle.h"

MainComponent::MainComponent(MainComponentOptions opts)
    : options(opts), audioEngine(library, opts.openAudioDevices)
{
    juce::Logger::writeToLog("MainComponent::MainComponent body entry");
    
    auto raaadioDir = PersistenceManager::getLabRoot();

    raaadioDir.createDirectory();
    PersistenceManager::getDatabaseFolder().createDirectory();
    PersistenceManager::getLibraryFolder().createDirectory();
    PersistenceManager::getRecordingsFolder().createDirectory();
    PersistenceManager::getBackupsFolder().createDirectory();
    PersistenceManager::migrateLegacyFolders();

    juce::File dbFile = PersistenceManager::getDatabaseFolder().getChildFile("raaadio.db");

    // Versiones muy anteriores guardaban la base y la pauta en Documentos. La pauta se
    // copia a Config para que la base la importe al abrirse.
    if (!PersistenceManager::isLabRootConfigured())
    {
        auto docDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        juce::File oldDb = docDir.getChildFile("djradio.db");
        if (oldDb.exists() && !dbFile.exists()) {
            oldDb.copyFileTo(dbFile);
        }
        const auto oldAds = docDir.getChildFile("ads_data.json");
        const auto adsFile = PersistenceManager::getConfigFolder().getChildFile("ads_data.json");
        if (oldAds.existsAsFile() && !adsFile.exists() && !adsFile.withFileExtension("json.migrated").exists()) {
            PersistenceManager::getConfigFolder().createDirectory();
            oldAds.copyFileTo(adsFile);
        }
    }

    // Un respaldo que se pidió restaurar se aplica ahora, con la base de datos todavía cerrada.
    juce::File databaseBeforeRestore;
    if (PersistenceManager::hasPendingRestore()) {
        const auto restored = PersistenceManager::applyPendingRestore(&databaseBeforeRestore);
        juce::Logger::writeToLog("Pending restore: " + (restored.wasOk() ? juce::String("applied") : restored.getErrorMessage()));
        juce::MessageManager::callAsync([restored] {
            juce::AlertWindow::showMessageBoxAsync(restored.wasOk() ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                                                   TRANS("backup_title"),
                                                   restored.wasOk() ? TRANS("restore_applied") : restored.getErrorMessage());
        });
    }

    // Una base dañada (un corte de luz en mal momento, un disco que falla) se reemplaza por el
    // último respaldo verificado antes de abrirla; la dañada se guarda aparte.
    const auto health = PersistenceManager::checkDatabase(dbFile);
    if (health.damaged) {
        juce::MessageManager::callAsync([health] {
            const auto message = health.recovered
                ? TRANS("db_recovered").replace("%1", health.restoredFrom.getFullPathName())
                                       .replace("%2", health.damagedCopy.getFileName())
                : TRANS("db_damaged_no_backup").replace("%1", health.problem);
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("db_damaged_title"), message);
        });
    }

    juce::Logger::writeToLog("Step 2.1: Initialising Library at " + dbFile.getFullPathName());
    library.initialise(dbFile);
    juce::Logger::writeToLog("Step 2.1: Library Initialised.");

    // Después de restaurar, la cola y las tandas ya insertadas son las de antes de restaurar.
    if (databaseBeforeRestore.existsAsFile())
        library.withDatabase([&databaseBeforeRestore](Database& db) { return db.carryOverLiveState(databaseBeforeRestore); });

    // Historial desde el registro de emisión, y la cola que quedó al cerrar la última vez.
    historyManager.initialise(library);
    playlistStore = std::make_unique<PlaylistStore>(playlist, library);
    playlistStore->restore();

    adsManager.initialise(library);

    // Sonoridad y puntos de entrada/salida de lo importado con versiones anteriores.
    if (options.analyseLibrary)
        library.startBackgroundAnalysis();

    recordingManager.initialise(audioEngine.getFormatManager());
    audioEngine.getMixer().addOutputTap(&recordingManager);

    // Emisión: botonera, registro por hora, transmisión, protección contra silencio, pauta
    // automática y respaldos. Los ajustes viven en la base de datos.
    stationConfig = std::make_unique<StationConfig>(&library);

    // Respaldos: el diario a su hora (o al abrir, si esa hora pasó sin respaldo), al destino
    // externo y con la música. En modo de captura de UI no se respalda nada.
    backupService = std::make_unique<BackupService>(library, *stationConfig);
    if (options.runAutoBackup)
        backupService->start();

    cartPlayer = std::make_unique<CartPlayer>(library, audioEngine.getFormatManager());
    cartPlayer->onCartPlayed = [this](const TrackInfo& track) {
        if (track.id != -1) library.incrementPlayCount(track.id);
        historyManager.addEntry(track, TRANS("tab_cartwall"));
    };
    audioEngine.getMixer().setCartSource(cartPlayer.get());

    airLog.setFolder(PersistenceManager::getAirLogFolder());
    audioEngine.getMixer().addOutputTap(&airLog);
    audioEngine.getMixer().addOutputTap(&streamEncoder);

    deadAirGuard = std::make_unique<DeadAirGuard>(audioEngine, autoDJ, playlist, library);
    adScheduler = std::make_unique<AdScheduler>(library, adsManager, playlist, audioEngine.getDeck(0), audioEngine.getDeck(1));

    auto& d1 = audioEngine.getDeck(0);
    auto& d2 = audioEngine.getDeck(1);

    // Lo que oyen los oyentes (una pista en preescucha, solo CUE, no cuenta): el título del
    // stream y los datos de anterior, actual y siguientes para la API y la página web.
    nowPlaying = std::make_unique<NowPlayingPublisher>(d1, d2, playlist, historyManager, library, streamEncoder, apiServer);

    applyStationSettings();
    stationConfig->addChangeListener(this);

    if (options.startApiServer)
        apiServer.start(8080);

    auto onDeckStateChange = [this] {
        if (nowPlaying != nullptr) nowPlaying->refresh();
    };
    d1.onStateChange = onDeckStateChange;
    d2.onStateChange = onDeckStateChange;
    
    d1.onTrackLog = [this](const TrackInfo& track) {
        historyManager.addEntry(track, "Deck A");
    };
    d2.onTrackLog = [this](const TrackInfo& track) {
        historyManager.addEntry(track, "Deck B");
    };

    juce::Logger::writeToLog("Step 2.3: Creating UI Components...");
    deckA = std::make_unique<DeckComponent>(audioEngine.getDeck(0));
    deckB = std::make_unique<DeckComponent>(audioEngine.getDeck(1));
    
    deckA->setPlaylist(&playlist); 
    deckB->setPlaylist(&playlist);
    
    mixerComp = std::make_unique<MixerComponent>(audioEngine.getMixer(), audioEngine.getDeck(0), audioEngine.getDeck(1), autoDJ);
    
    playlistComp = std::make_unique<PlaylistComponent>(playlist, library);
    playlistComp->setDecks(&audioEngine.getDeck(0), &audioEngine.getDeck(1)); 
    
    libraryComp = std::make_unique<LibraryComponent>(library, historyManager, adsManager, *cartPlayer);
    libraryComp->setDecks(&audioEngine.getDeck(0), &audioEngine.getDeck(1));
    libraryComp->setPlaylist(&playlist);
    libraryComp->setPreviewPlayer(&audioEngine.getPreview());
    onlineIdentifier = std::make_unique<OnlineIdentifier>(library);
    libraryComp->setOnlineIdentifier(onlineIdentifier.get());

    // Al confirmar una canción en la revisión su archivo cambia de lugar: la cola guarda la
    // ruta de cada fila, así que se le avisa.
    library.onTrackRelocated = [this](int trackId, const juce::String& newPath) {
        playlist.updatePath(trackId, newPath.toStdString());
    };

    statusBar = std::make_unique<StatusBar>(audioEngine, autoDJ, recordingManager, *deadAirGuard, streamEncoder);
    statusBar->onSettingsRequested = [this] { openSettings(); };
    statusBar->healthSource = [this] { return evaluateHealth(); };

    addAndMakeVisible(*statusBar);
    addAndMakeVisible(*deckA);
    addAndMakeVisible(*deckB);
    addAndMakeVisible(*mixerComp);
    addAndMakeVisible(*playlistComp);
    addAndMakeVisible(*libraryComp);
    
    setSize(1024, 768);

    if (options.openAudioDevices)
        startTimer(500);
    juce::Logger::writeToLog("MainComponent: ctor done");
}

void MainComponent::parentHierarchyChanged() {
    auto* window = getTopLevelComponent();
    if (window == keyWindow.getComponent()) return;
    if (keyWindow != nullptr) keyWindow->removeKeyListener(&cartKeys);
    keyWindow = window;
    if (window != nullptr) window->addKeyListener(&cartKeys);
}

bool MainComponent::CartKeys::keyPressed(const juce::KeyPress& key, juce::Component*) {
    static const int functionKeys[CartPlayer::numCarts] = {
        juce::KeyPress::F1Key, juce::KeyPress::F2Key, juce::KeyPress::F3Key, juce::KeyPress::F4Key,
        juce::KeyPress::F5Key, juce::KeyPress::F6Key, juce::KeyPress::F7Key, juce::KeyPress::F8Key,
        juce::KeyPress::F9Key, juce::KeyPress::F10Key, juce::KeyPress::F11Key, juce::KeyPress::F12Key
    };
    for (int i = 0; i < CartPlayer::numCarts; ++i) {
        if (key.getKeyCode() == functionKeys[i] && !key.getModifiers().isAnyModifierKeyDown()) {
            owner.cartPlayer->toggle(i);
            return true;
        }
    }
    return false;
}

void MainComponent::applyStationSettings() {
    const auto& s = stationConfig->get();

    deadAirGuard->setEnabled(s.deadAirEnabled);
    deadAirGuard->setThresholdSeconds(s.deadAirSeconds);
    deadAirGuard->setEmergencyCategory(s.emergencyCategory);

    // En modo de captura de UI no se graba ni se transmite nada.
    airLog.setRetentionDays(s.airLogRetentionDays);
    airLog.setEnabled(s.airLogEnabled && options.openAudioDevices);

    StreamEncoder::Server server;
    server.host = s.streamHost;
    server.port = s.streamPort;
    server.mount = s.streamMount;
    server.user = s.streamUser;
    server.password = s.streamPassword;
    server.name = s.streamName;
    server.format = s.streamFormat == StationSettings::StreamFormat::Mp3 ? StreamEncoder::Format::Mp3
                                                                         : StreamEncoder::Format::OggVorbis;
    server.quality = s.streamQuality;
    server.bitrate = s.streamBitrate;
    server.genre = s.streamGenre;
    server.description = s.streamDescription;
    server.website = s.streamWebsite;
    server.isPublic = s.streamPublic;
    server.metadataDelaySeconds = s.streamMetadataDelay;

    // Durante las tandas los oyentes ven el nombre de la emisora.
    nowPlaying->setStationName(s.streamName);
    nowPlaying->setPushUrl(options.openAudioDevices ? s.nowPlayingPushUrl : juce::String());

    const bool wantStream = s.streamEnabled && s.streamHost.isNotEmpty() && options.openAudioDevices;
    if (!wantStream) {
        if (streamEncoder.getStatus() != StreamEncoder::Status::Off) streamEncoder.stop();
    } else if (streamEncoder.getStatus() == StreamEncoder::Status::Off || server != runningStream) {
        streamEncoder.start(server);
        runningStream = server;
    }
}

void MainComponent::timerCallback() {
    stopTimer(); 
    juce::Logger::writeToLog("MainComponent: Timer Fired. Starting Audio Engine...");
    audioEngine.startAudioDevice();
}

bool MainComponent::isOnAir() {
    auto deckOnAir = [](Deck& d) { return d.isPlaying() && d.isRoutedToMain(); };

    auto recState = recordingManager.getState();
    bool recording = (recState == RecordingManager::Recording || recState == RecordingManager::Paused);

    return deckOnAir(audioEngine.getDeck(0)) || deckOnAir(audioEngine.getDeck(1)) || recording;
}

MainComponent::~MainComponent() {
    // La interfaz se destruye antes que los servicios que observa (grabación, decks, AutoDJ).
    statusBar = nullptr;
    libraryComp = nullptr;
    onlineIdentifier = nullptr;  // espera a que termine un pedido en curso
    playlistComp = nullptr;
    mixerComp = nullptr;
    deckA = nullptr;
    deckB = nullptr;

    backupService = nullptr;  // espera a que termine un respaldo en curso (la copia de música se corta)
    library.onTrackRelocated = nullptr;
    playlistStore = nullptr;  // guarda la cola por última vez
    stationConfig->removeChangeListener(this);
    nowPlaying = nullptr;  // antes que los decks, la cola y el historial que observa
    if (keyWindow != nullptr) keyWindow->removeKeyListener(&cartKeys);

    // Primero se cierra el audio; después se sueltan los que reciben la salida al aire.
    audioEngine.shutdown();
    auto& mixer = audioEngine.getMixer();
    mixer.removeOutputTap(&recordingManager);
    mixer.removeOutputTap(&airLog);
    mixer.removeOutputTap(&streamEncoder);
    mixer.setCartSource(nullptr);
    streamEncoder.stop();
    airLog.setEnabled(false);
}

void MainComponent::openSettings() {
    auto* dialog = new SettingsDialog(audioEngine.getDeviceManager(), audioEngine.getCueDeviceManager(), library, adsManager,
                                      *stationConfig, streamEncoder, nowPlaying.get(), backupService.get(), PersistenceManager::getAirLogFolder());
    // Al borrar la biblioteca, la cola y la botonera apuntarían a pistas que ya no existen.
    // Lo que suena en los decks sigue: el archivo no se borra.
    dialog->setOnlineIdentifier(onlineIdentifier.get());
    dialog->onLibraryReset = [this] {
        playlist.clear();
        cartPlayer->clearAll();
        libraryComp->refreshLibrary();
    };
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dialog);
    opt.dialogTitle = TRANS("settings");
    opt.dialogBackgroundColour = ThemeManager::getInstance().getBaseBackgroundColour();
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;
    auto* w = opt.launchAsync();
    w->centreWithSize(640, 660);
}

void MainComponent::paint(juce::Graphics& g) {
    g.fillAll(ThemeManager::getInstance().getBaseBackgroundColour());
}

void MainComponent::resized() {
    const int gap = UiStyle::gap;
    auto area = getLocalBounds();

    statusBar->setBounds(area.removeFromTop(56));
    area.reduce(gap, gap);

    // Fila superior: deck A, mezclador y deck B.
    const int decksHeight = juce::jlimit(290, 400, (int) (area.getHeight() * 0.40f));
    auto top = area.removeFromTop(decksHeight);
    area.removeFromTop(gap);

    const int mixerWidth = juce::jlimit(280, 340, (int) (top.getWidth() * 0.18f));
    const int deckWidth = (top.getWidth() - mixerWidth - gap * 2) / 2;
    deckA->setBounds(top.removeFromLeft(deckWidth));
    top.removeFromLeft(gap);
    mixerComp->setBounds(top.removeFromLeft(mixerWidth));
    top.removeFromLeft(gap);
    deckB->setBounds(top);

    // Fila inferior: cola de reproducción y biblioteca.
    playlistComp->setBounds(area.removeFromLeft((area.getWidth() - gap) / 2));
    area.removeFromLeft(gap);
    libraryComp->setBounds(area);
}

StationHealth::Alert MainComponent::evaluateHealth() {
    const auto nowMs = juce::Time::getMillisecondCounter();
    if (diskCheckedAt == 0 || nowMs - diskCheckedAt > 60 * 1000) {
        const auto lab = PersistenceManager::getLabRoot();
        diskFree = lab.getBytesFreeOnVolume();
        diskTotal = lab.getVolumeTotalSize();
        diskCheckedAt = nowMs;
    }

    const auto& s = stationConfig->get();
    const auto backup = backupService->getStatus();
    StationHealth::Inputs in;
    in.now = juce::Time::getCurrentTime();
    in.diskFree = diskFree;
    in.diskTotal = diskTotal;
    in.backupEnabled = s.backupEnabled && options.runAutoBackup;
    in.latestBackup = PersistenceManager::getLatestBackupTime();
    in.lastBackupOk = backup.lastOk;
    in.lastBackupError = backup.lastError;
    in.externalConfigured = s.backupExternalFolder.isNotEmpty();
    in.externalAttempted = backup.externalAttempted;
    in.externalOk = backup.externalOk;
    in.externalError = backup.externalError;
    return StationHealth::evaluate(in);
}
