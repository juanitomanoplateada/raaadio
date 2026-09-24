#include "MainComponent.h"
#include "ui/AudioSettingsDialog.h"
#include "autodj/OnAir.h"
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
    PersistenceManager::getConfigFolder().createDirectory();
    PersistenceManager::getLibraryFolder().createDirectory();
    PersistenceManager::getRecordingsFolder().createDirectory();
    PersistenceManager::getTemplatesFolder().createDirectory();
    PersistenceManager::getBackupsFolder().createDirectory();

    juce::File dbFile = PersistenceManager::getDatabaseFolder().getChildFile("raaadio.db");
    
    // Legacy migration check
    if (!PersistenceManager::isLabRootConfigured())
    {
        auto docDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        juce::File oldDb = docDir.getChildFile("djradio.db");
        if (oldDb.exists() && !dbFile.exists()) {
            oldDb.copyFileTo(dbFile); 
        }
    }
    
    // Un respaldo que se pidió restaurar se aplica ahora, con la base de datos todavía cerrada.
    if (PersistenceManager::hasPendingRestore()) {
        const auto restored = PersistenceManager::applyPendingRestore();
        juce::Logger::writeToLog("Pending restore: " + (restored.wasOk() ? juce::String("applied") : restored.getErrorMessage()));
        juce::MessageManager::callAsync([restored] {
            juce::AlertWindow::showMessageBoxAsync(restored.wasOk() ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon,
                                                   TRANS("backup_title"),
                                                   restored.wasOk() ? TRANS("restore_applied") : restored.getErrorMessage());
        });
    }

    juce::Logger::writeToLog("Step 2.1: Initialising Library at " + dbFile.getFullPathName());
    library.initialise(dbFile);
    juce::Logger::writeToLog("Step 2.1: Library Initialised.");

    // Historial desde el registro de emisión, y la cola que quedó al cerrar la última vez.
    historyManager.initialise(library);
    playlistStore = std::make_unique<PlaylistStore>(playlist, library,
                                                    PersistenceManager::getConfigFolder().getChildFile("queue.json"));
    playlistStore->restore();

    juce::File adsFile = PersistenceManager::getConfigFolder().getChildFile("ads_data.json");
    
    // Legacy migration check for ads
    if (!PersistenceManager::isLabRootConfigured())
    {
        auto docDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
        juce::File oldAds = docDir.getChildFile("ads_data.json");
        if (oldAds.exists() && !adsFile.exists()) {
             oldAds.copyFileTo(adsFile);
        }
    }
    
    adsManager.initialise(adsFile);

    // Auto-Backup
    if (options.runAutoBackup)
        PersistenceManager::performAutoBackupIfNeeded([this](const juce::File& target) { return library.backupDatabaseTo(target); });

    // Sonoridad y puntos de entrada/salida de lo importado con versiones anteriores.
    if (options.analyseLibrary)
        library.startBackgroundAnalysis();

    recordingManager.initialise(audioEngine.getFormatManager());
    audioEngine.getMixer().addOutputTap(&recordingManager);

    // Emisión: botonera, registro por hora, transmisión, protección contra silencio y
    // pauta automática. Los ajustes viven en Config/station.json.
    const auto configFolder = PersistenceManager::getConfigFolder();
    stationConfig = std::make_unique<StationConfig>(configFolder.getChildFile("station.json"));

    cartPlayer = std::make_unique<CartPlayer>(library, audioEngine.getFormatManager(), configFolder.getChildFile("cartwall.json"));
    cartPlayer->onCartPlayed = [this](const TrackInfo& track) {
        if (track.id != -1) library.incrementPlayCount(track.id);
        historyManager.addEntry(track, TRANS("tab_cartwall"));
    };
    audioEngine.getMixer().setCartSource(cartPlayer.get());

    airLog.setFolder(PersistenceManager::getRecordingsFolder().getChildFile("Registro"));
    audioEngine.getMixer().addOutputTap(&airLog);
    audioEngine.getMixer().addOutputTap(&streamEncoder);

    deadAirGuard = std::make_unique<DeadAirGuard>(audioEngine, autoDJ, playlist, library);
    adScheduler = std::make_unique<AdScheduler>(library, adsManager, playlist, audioEngine.getDeck(0), audioEngine.getDeck(1),
                                                configFolder.getChildFile("ad_breaks_done.json"));

    applyStationSettings();
    stationConfig->addChangeListener(this);

    if (options.startApiServer)
        apiServer.start(8080);
    
    auto& d1 = audioEngine.getDeck(0);
    auto& d2 = audioEngine.getDeck(1);
    
    auto updateApi = [this, &d1, &d2] {
        // Lo que oyen los oyentes: una pista en preescucha (solo CUE) no cuenta.
        juce::String artist = TRANS("radio_station");
        juce::String title = TRANS("ready");
        if (const auto* deck = OnAir::nowPlaying(d1, d2)) {
            artist = deck->getCurrentTrack().artist;
            title = deck->getCurrentTrack().title;
        }

        apiServer.updateNowPlaying(artist, title);
        streamEncoder.setNowPlaying(artist, title);  // título en los reproductores de los oyentes
    };
    
    d1.onStateChange = updateApi;
    d2.onStateChange = updateApi;
    
    d1.onTrackLog = [this](const TrackInfo& track) {
        historyManager.addEntry(track, "Deck A");
    };
    d2.onTrackLog = [this](const TrackInfo& track) {
        historyManager.addEntry(track, "Deck B");
    };
    
    updateApi();
    
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

    statusBar = std::make_unique<StatusBar>(audioEngine, autoDJ, recordingManager, *deadAirGuard, streamEncoder);
    statusBar->onSettingsRequested = [this] { openSettings(); };

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
    server.quality = s.streamQuality;

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
    playlistComp = nullptr;
    mixerComp = nullptr;
    deckA = nullptr;
    deckB = nullptr;

    playlistStore = nullptr;  // guarda la cola por última vez
    stationConfig->removeChangeListener(this);
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
                                      *stationConfig, streamEncoder, PersistenceManager::getRecordingsFolder().getChildFile("Registro"));
    // Al borrar la biblioteca, la cola y la botonera apuntarían a pistas que ya no existen.
    // Lo que suena en los decks sigue: el archivo no se borra.
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
