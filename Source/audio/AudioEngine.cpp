#include "AudioEngine.h"
#include "../helpers/AppSettings.h"

AudioEngine::AudioEngine(MusicLibrary& library, bool openDevices) : devicesEnabled(openDevices) {
    formatManager.registerBasicFormats();
    
    cueSource = std::make_unique<CueSource>(mixer);
    sourcePlayer.setSource(&mixer);
    cueSourcePlayer.setSource(cueSource.get());
    
    decks.push_back(std::make_unique<Deck>(1, library, formatManager, thumbnailCache));
    decks.push_back(std::make_unique<Deck>(2, library, formatManager, thumbnailCache));
    
    mixer.addDeck(decks[0].get());
    mixer.addDeck(decks[1].get());

    preview = std::make_unique<PreviewPlayer>(formatManager);
    mixer.setPreviewSource(preview.get());

    if (!devicesEnabled) return;

    juce::ApplicationProperties appProps;
    appProps.setStorageParameters(AppSettings::options());

    std::unique_ptr<juce::XmlElement> mainXml(appProps.getCommonSettings(true)->getXmlValue("audioDeviceState"));
    std::unique_ptr<juce::XmlElement> cueXml(appProps.getCommonSettings(true)->getXmlValue("cueDeviceState"));
    
    juce::String err = deviceManager.initialise(0, 2, mainXml.get(), true);
    if (err.isNotEmpty()) {
        juce::Logger::writeToLog("Main Config Failed: " + err + " - Falling back to default.");
        deviceManager.initialiseWithDefaultDevices(0, 2);
    }
    
    juce::String errCue = cueDeviceManager.initialise(0, 2, cueXml.get(), true);
    if (errCue.isNotEmpty()) {
        juce::Logger::writeToLog("Cue Config Failed: " + errCue + " - Falling back to default.");
        cueDeviceManager.initialiseWithDefaultDevices(0, 2);
    }
}

AudioEngine::~AudioEngine() {
    shutdown();
}

void AudioEngine::startAudioDevice() {
    if (!devicesEnabled) return;

    if (deviceManager.getCurrentAudioDevice() != nullptr) {
        deviceManager.addAudioCallback(&sourcePlayer);
        juce::Logger::writeToLog("Main Audio Callback Added.");
    } else {
        juce::Logger::writeToLog("WARNING: Main Audio Device not open in startAudioDevice.");
    }

    if (cueDeviceManager.getCurrentAudioDevice() != nullptr) {
        cueDeviceManager.addAudioCallback(&cueSourcePlayer);
        juce::Logger::writeToLog("Cue Audio Callback Added.");
    } else {
        juce::Logger::writeToLog("WARNING: Cue Audio Device not open in startAudioDevice.");
    }
}

void AudioEngine::shutdown() {
    deviceManager.removeAudioCallback(&sourcePlayer);
    cueDeviceManager.removeAudioCallback(&cueSourcePlayer);
    
    sourcePlayer.setSource(nullptr);
    cueSourcePlayer.setSource(nullptr);

    if (!devicesEnabled) return;

    juce::ApplicationProperties appProps;
    appProps.setStorageParameters(AppSettings::options());

    auto xmlMain = deviceManager.createStateXml();
    if (xmlMain) appProps.getCommonSettings(true)->setValue("audioDeviceState", xmlMain.get());
    
    auto xmlCue = cueDeviceManager.createStateXml();
    if (xmlCue) appProps.getCommonSettings(true)->setValue("cueDeviceState", xmlCue.get());
    
    appProps.saveIfNeeded();
}
