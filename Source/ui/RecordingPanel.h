#pragma once
#include <JuceHeader.h>
#include "../audio/RecordingManager.h"
#include "../audio/AudioEngine.h"

// Controles de grabación del aire: grabar, pausar/reanudar, guardar y abrir la carpeta.
class RecordingPanel : public juce::Component, public juce::Timer, public juce::ChangeListener {
public:
    RecordingPanel(RecordingManager& manager, AudioEngine& engine);
    ~RecordingPanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    RecordingManager& recordingManager;
    AudioEngine& audioEngine;

    juce::TextButton recButton;
    juce::TextButton pauseButton;
    juce::TextButton saveButton;
    juce::TextButton openFolderButton;

    juce::Rectangle<int> timeArea;
    juce::String timeText { "00:00:00" };
    bool blinkState = false;

    void updateButtons();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingPanel)
};
