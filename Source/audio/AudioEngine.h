#pragma once
#include <JuceHeader.h>
#include "Mixer.h"
#include "Deck.h"
#include "../library/Playlist.h"

class AudioEngine {
public:
    // Los decks usan la biblioteca (ganancia, puntos de entrada y salida, registro de
    // emisión), así que debe existir antes que el motor y destruirse después.
    // openDevices = false no abre ni guarda la configuración de los dispositivos de audio
    // (modo de captura de UI: la interfaz funciona completa pero no suena nada).
    explicit AudioEngine(MusicLibrary& library, bool openDevices = true);
    ~AudioEngine();
    
    void startAudioDevice();
    void shutdown();
    
    Mixer& getMixer() { return mixer; }
    Deck& getDeck(int index) { return *decks[(size_t) index]; }
    juce::AudioFormatManager& getFormatManager() { return formatManager; }
    juce::AudioThumbnailCache& getThumbnailCache() { return thumbnailCache; }

private:
    bool devicesEnabled = true;
    juce::AudioDeviceManager deviceManager;
    juce::AudioDeviceManager cueDeviceManager;
    
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache{100};
    
    Mixer mixer;
    std::vector<std::unique_ptr<Deck>> decks;
    juce::AudioSourcePlayer sourcePlayer;
    juce::AudioSourcePlayer cueSourcePlayer;
    
    class CueSource : public juce::AudioSource {
    public:
        CueSource(Mixer& m) : mixer(m) {}
        void prepareToPlay(int S, double R) override { }
        void releaseResources() override { }
        void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override {
            mixer.readCueBlock(bufferToFill);
        }
        Mixer& mixer;
    };
    std::unique_ptr<CueSource> cueSource;
    
public:
    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    juce::AudioDeviceManager& getCueDeviceManager() { return cueDeviceManager; }
};
