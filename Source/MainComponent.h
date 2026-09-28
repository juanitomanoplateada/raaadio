#pragma once
#include <JuceHeader.h>
#include "audio/AudioEngine.h"
#include "library/MusicLibrary.h"
#include "library/HistoryManager.h"
#include "library/AdvertisingManager.h"
#include "library/Playlist.h"
#include "library/PlaylistStore.h"
#include "autodj/AutoDJ.h"
#include "ui/PlaylistComponent.h"
#include "ui/LibraryComponent.h"
#include "ui/DeckComponent.h"
#include "ui/MixerComponent.h"
#include "ui/StatusBar.h"
#include "audio/RecordingManager.h"
#include "audio/AirLogRecorder.h"
#include "audio/CartPlayer.h"
#include "autodj/DeadAirGuard.h"
#include "autodj/AdScheduler.h"
#include "network/ApiServer.h"
#include "network/StreamEncoder.h"
#include "network/NowPlayingPublisher.h"
#include "network/OnlineIdentifier.h"
#include "helpers/StationSettings.h"
#include "helpers/BackupService.h"
#include "helpers/StationHealth.h"

// Qué servicios arranca la ventana principal. Todo activo en uso normal; el modo
// de captura de UI (UiSnapshot) los apaga para no sonar ni tocar datos reales.
struct MainComponentOptions {
    bool openAudioDevices = true;
    bool startApiServer = true;
    bool runAutoBackup = true;
    bool analyseLibrary = true;  // análisis en segundo plano de pistas de versiones anteriores
};

class MainComponent : public juce::Component, public juce::DragAndDropContainer,
                      private juce::Timer, private juce::ChangeListener {
public:
    explicit MainComponent(MainComponentOptions options = MainComponentOptions());
    ~MainComponent() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void parentHierarchyChanged() override;

    // true si algún deck suena en la salida principal o hay una grabación en curso.
    bool isOnAir();

private:
    friend class UiSnapshotRunner;

    MainComponentOptions options;
    MusicLibrary library;       // antes que audioEngine: los decks la usan
    AudioEngine audioEngine;
    HistoryManager historyManager;
    AdvertisingManager adsManager;
    Playlist playlist;
    AutoDJ autoDJ{audioEngine, playlist};
    std::unique_ptr<PlaylistStore> playlistStore;

    std::unique_ptr<DeckComponent> deckA;
    std::unique_ptr<DeckComponent> deckB;
    std::unique_ptr<MixerComponent> mixerComp;
    std::unique_ptr<PlaylistComponent> playlistComp;
    std::unique_ptr<LibraryComponent> libraryComp;
    std::unique_ptr<StatusBar> statusBar;

    RecordingManager recordingManager;
    ApiServer apiServer;

    // Servicios de emisión. Se crean en el constructor, con la biblioteca ya abierta, y se
    // destruyen antes que AutoDJ, la cola y el motor de audio que usan.
    std::unique_ptr<StationConfig> stationConfig;
    std::unique_ptr<BackupService> backupService;
    std::unique_ptr<CartPlayer> cartPlayer;
    AirLogRecorder airLog;
    StreamEncoder streamEncoder;
    StreamEncoder::Server runningStream;
    std::unique_ptr<NowPlayingPublisher> nowPlaying;
    std::unique_ptr<DeadAirGuard> deadAirGuard;
    std::unique_ptr<AdScheduler> adScheduler;
    std::unique_ptr<OnlineIdentifier> onlineIdentifier;  // AcoustID y MusicBrainz, si la emisora lo activa

    // Sin esta ventana JUCE no muestra las ayudas emergentes (setTooltip) de ningún control.
    juce::TooltipWindow tooltipWindow { this, 700 };

    void openSettings();
    void changeListenerCallback(juce::ChangeBroadcaster*) override { applyStationSettings(); }
    void applyStationSettings();

    // Alertas para la barra superior: disco de la emisora y respaldos. El espacio libre se
    // mide una vez por minuto.
    StationHealth::Alert evaluateHealth();
    juce::int64 diskFree = -1, diskTotal = -1;
    juce::uint32 diskCheckedAt = 0;

    // F1-F12 disparan la botonera. Se escuchan en la ventana principal, que recibe toda
    // tecla que ningún control usó (aunque nada tenga el foco).
    struct CartKeys : public juce::KeyListener {
        explicit CartKeys(MainComponent& o) : owner(o) {}
        bool keyPressed(const juce::KeyPress& key, juce::Component*) override;
        MainComponent& owner;
    };
    CartKeys cartKeys { *this };
    juce::Component::SafePointer<juce::Component> keyWindow;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
