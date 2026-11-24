#pragma once
#include <JuceHeader.h>

class MainComponent;
class SongDownloader;
class MusicLibrary;
class AudioEngine;
class Playlist;
class AdvertisingManager;
class StationConfig;
class StreamEncoder;

// Modo de captura de UI: renderiza la interfaz a un PNG con datos de demostración,
// sin ventana, sin dispositivos de audio y en una carpeta de laboratorio temporal.
// Sirve para revisar cambios visuales y para regenerar las capturas del README.
//
//   RAAADIO.exe --ui-snapshot salida.png [--size 1920x1080] [--theme dark|light]
//               [--lang es|en] [--view main|empty|history|ads|settings|settings-library|
//                                     settings-broadcast|settings-broadcast-2|settings-language|settings-about|
//                                     cartwall|generator|generator-advanced|track|import|library-online|online-search|
//                                     temporaries|temporaries-link|temporaries-empty]
class UiSnapshotRunner : private juce::Timer
{
public:
    struct Options
    {
        juce::File output;
        int width = 1920;
        int height = 1080;
        juce::String view = "main";
        juce::String theme;     // vacío = el guardado
        juce::String language;  // vacío = el guardado
    };

    // Devuelve true si la línea de comandos pide una captura.
    static bool parseCommandLine(const juce::StringArray& args, Options& result);

    // Crea un laboratorio vacío en la carpeta temporal y lo activa para esta ejecución.
    static void useIsolatedLab();

    explicit UiSnapshotRunner(const Options& options);
    ~UiSnapshotRunner() override;

    std::function<void(bool success)> onFinished;

    // La pantalla armada con los datos de demostración, para que las pruebas de interfaz la
    // operen igual que se ve en la captura.
    MainComponent& getMainComponent() const { return *main; }
    juce::Component* getView() const { return target; }
    MusicLibrary& getLibrary() const;
    AudioEngine& getAudioEngine() const;
    Playlist& getPlaylist() const;
    AdvertisingManager& getAdsManager() const;
    StationConfig& getStationConfig() const;
    StreamEncoder& getStreamEncoder() const;

private:
    void timerCallback() override;
    void populateDemoState();
    void pumpAudio(int blocks);
    bool captureAndSave();
    juce::Component* createView();

    Options options;
    std::unique_ptr<MainComponent> main;
    std::unique_ptr<SongDownloader> demoDownloader;  // descargas de ejemplo (sin yt-dlp de verdad)
    std::unique_ptr<juce::Component> dialogView;
    juce::Component* target = nullptr;
    juce::uint32 startTime = 0;

    JUCE_DECLARE_NON_COPYABLE(UiSnapshotRunner)
};
