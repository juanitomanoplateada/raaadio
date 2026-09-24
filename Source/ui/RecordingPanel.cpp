#include "RecordingPanel.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

RecordingPanel::RecordingPanel(RecordingManager& manager, AudioEngine& engine)
    : recordingManager(manager), audioEngine(engine)
{
    addAndMakeVisible(recButton);
    addAndMakeVisible(pauseButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(openFolderButton);

    // En reposo, REC es un botón neutro con el punto en rojo: el rojo sólido queda
    // reservado para lo que está al aire.
    UiStyle::setIcon(recButton, "record");
    UiStyle::setIcon(pauseButton, "pause");
    UiStyle::setIcon(saveButton, "stop");
    UiStyle::setVariant(openFolderButton, "ghost");
    UiStyle::setIcon(openFolderButton, "folder");

    recButton.onClick = [this] {
        bool d1 = audioEngine.getDeck(0).isRoutedToMain();
        bool d2 = audioEngine.getDeck(1).isRoutedToMain();

        if (!d1 && !d2) {
             juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                                    TRANS("rec_error_no_audio_title"), TRANS("rec_error_no_audio_msg"));
             return;
        }

        juce::String error = recordingManager.startRecording(d1, d2);
        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("rec_error_title"), error);
    };

    pauseButton.onClick = [this] {
        if (recordingManager.getState() == RecordingManager::Recording) {
            recordingManager.pauseRecording();
        } else if (recordingManager.getState() == RecordingManager::Paused) {
            recordingManager.resumeRecording();
        }
    };

    saveButton.onClick = [this] { recordingManager.stopRecording(); };
    openFolderButton.onClick = [this] { recordingManager.openRecordingsFolder(); };

    recordingManager.addChangeListener(this);
    lookAndFeelChanged();
    startTimer(250);
}

RecordingPanel::~RecordingPanel() {
    recordingManager.removeChangeListener(this);
}

void RecordingPanel::lookAndFeelChanged() {
    recButton.getProperties().set("textColour", ThemeManager::getInstance().getErrorColour().toString());
    recButton.setButtonText(TRANS("rec_btn_record"));
    saveButton.setButtonText(TRANS("rec_btn_save"));
    openFolderButton.setTooltip(TRANS("rec_tooltip_folder"));
    recButton.setTooltip(TRANS("tip_record"));
    saveButton.setTooltip(TRANS("tip_record_save"));
    updateButtons();
}

void RecordingPanel::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    const auto state = recordingManager.getState();
    const bool recording = state == RecordingManager::Recording;
    const bool paused = state == RecordingManager::Paused;

    auto box = timeArea.toFloat();
    const auto tint = recording ? t.getErrorColour() : (paused ? t.getWarningColour() : juce::Colour());
    g.setColour(tint.isTransparent() ? t.getRaisedColour() : tint.withAlpha(0.16f));
    g.fillRoundedRectangle(box, UiStyle::controlRadius);
    g.setColour(tint.isTransparent() ? t.getStrongOutlineColour() : tint.withAlpha(0.6f));
    g.drawRoundedRectangle(box.reduced(0.5f), UiStyle::controlRadius, 1.0f);

    auto dot = box.removeFromLeft(24.0f).withSizeKeepingCentre(9.0f, 9.0f);
    if (recording && blinkState) {
        g.setColour(t.getErrorColour());
        g.fillEllipse(dot);
    } else if (paused) {
        g.setColour(t.getWarningColour());
        g.drawEllipse(dot, 1.6f);
    } else if (!recording) {
        g.setColour(t.getMutedTextColour());
        g.drawEllipse(dot, 1.2f);
    }

    g.setColour(state == RecordingManager::Idle ? t.getMutedTextColour() : t.getPrimaryTextColour());
    g.setFont(UiStyle::mono(16.0f, true));
    g.drawText(timeText, box.withTrimmedRight(8.0f), juce::Justification::centredLeft, false);
}

void RecordingPanel::resized() {
    auto area = getLocalBounds();
    const int gap = 6;

    recButton.setBounds(area.removeFromLeft(76));
    area.removeFromLeft(gap);
    timeArea = area.removeFromLeft(104);
    area.removeFromLeft(gap);
    pauseButton.setBounds(area.removeFromLeft(38));
    area.removeFromLeft(gap);
    saveButton.setBounds(area.removeFromLeft(108));
    area.removeFromLeft(gap);
    openFolderButton.setBounds(area.removeFromLeft(38));
}

void RecordingPanel::timerCallback() {
    blinkState = !blinkState;
    timeText = recordingManager.getDurationString();
    repaint(timeArea);
}

void RecordingPanel::changeListenerCallback(juce::ChangeBroadcaster*) {
    updateButtons();
}

void RecordingPanel::updateButtons() {
    const auto state = recordingManager.getState();
    const bool idle = state == RecordingManager::Idle;
    const bool recording = state == RecordingManager::Recording;
    const bool paused = state == RecordingManager::Paused;

    recButton.setEnabled(idle);
    pauseButton.setEnabled(recording || paused);
    saveButton.setEnabled(recording || paused);

    UiStyle::setIcon(pauseButton, paused ? "play" : "pause");
    UiStyle::setVariant(pauseButton, paused ? "warning" : "");
    pauseButton.setTooltip(paused ? TRANS("rec_btn_resume") : TRANS("rec_btn_pause"));
    saveButton.setButtonText(state == RecordingManager::Saving ? TRANS("rec_btn_saving") : TRANS("rec_btn_save"));

    timeText = recordingManager.getDurationString();
    repaint();
}
