#pragma once
#include <JuceHeader.h>
#include "../audio/Deck.h"
#include "WaveformComponent.h"

class Playlist;

class DeckComponent : public juce::Component,
                        public juce::FileDragAndDropTarget,
                        public juce::DragAndDropTarget,
                        public juce::ChangeListener,
                        public juce::Timer
{
public:
    DeckComponent(Deck& deck);
    ~DeckComponent();

    void setPlaylist(Playlist* p);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;

    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

private:
    enum class State { Empty, Ready, Paused, OnAir, Cue, Silent };
    State getState() const;
    bool isOnAir() const;
    juce::String getDeckLetter() const;

    Deck& deck;
    Playlist* playlistRef = nullptr;
    WaveformComponent waveform;

    juce::TextButton playButton;
    juce::TextButton pauseButton;
    juce::TextButton skipButton;
    juce::TextButton loopButton;
    juce::TextButton quitButton;
    juce::TextButton infoButton;

    juce::Slider speedSlider;
    juce::TextButton resetSpeedButton;

    // Áreas calculadas en resized() y dibujadas en paint()
    juce::Rectangle<int> headerArea, titleArea, artistArea, timeArea, pitchArea;

    bool dragHover = false;
    bool shownLoaded = false;
    bool shownPlaying = false;
    juce::String shownPath;
    double shownCueIn = 0.0, shownEnd = 0.0;  // cambian cuando termina un análisis en segundo plano
    bool blinkOn = true;
    int lastPaintedSecond = -1;
    State lastState = State::Empty;

    void updateVisibility();
    bool rejectDropIfOnAir();
    void openTrackInfo();
};
