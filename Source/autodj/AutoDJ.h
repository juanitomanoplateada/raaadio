#pragma once
#include <JuceHeader.h>
#include "../audio/AudioEngine.h"
#include "../library/Playlist.h"

class AutoDJ : public juce::Timer {
public:
    enum State {
        Manual,
        AutoPlaying
    };

    AutoDJ(AudioEngine& engine, Playlist& playlist);
    ~AutoDJ() override;

    void setEnabled(bool enabled);
    bool isEnabled() const;
    void timerCallback() override;

    // Si nada suena, pone al aire el deck precargado o la siguiente pista de la cola
    // (protección contra silencio). Devuelve true si algo quedó sonando.
    bool resumePlayback();

private:
    AudioEngine& audioEngine;
    Playlist& playlist;
    State state = Manual;

    // true cuando AutoDJ intentó continuar la programación y no pudo (lista vacía,
    // archivo faltante o ilegible). Mientras esté activo, se reintenta en cada tick.
    bool recoveryPending = false;
    uint64_t lastQueueSeen = 0;

    void processDecks();
    void prioritiseUpcomingAnalysis();
    void checkDeckFinish(Deck& activeDeck, Deck& otherDeck);
    void recoverIfStalled(Deck& d1, Deck& d2);
    bool triggerNextDeck(Deck& targetDeck);
};
