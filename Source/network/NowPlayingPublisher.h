#pragma once
#include <JuceHeader.h>
#include "NowPlaying.h"

class Deck;
class Playlist;
class MusicLibrary;
class StreamEncoder;
class ApiServer;

// Mantiene al día lo que ven los oyentes y lo que se publica: el título del stream (con el
// nombre de la emisora durante las tandas), el JSON del servidor local y, si hay una
// dirección configurada, el mismo JSON enviado por POST a la página web en cada cambio
// (y cada minuto, como señal de que la emisora sigue al aire).
class NowPlayingPublisher : private juce::Timer, private juce::AsyncUpdater, private juce::ChangeListener {
public:
    NowPlayingPublisher(Deck& deckA, Deck& deckB, Playlist& playlist, HistoryManager& history, MusicLibrary& library,
                        StreamEncoder& streamEncoder, ApiServer& apiServer);
    ~NowPlayingPublisher() override;

    void setStationName(const juce::String& name);
    void setPushUrl(const juce::String& url);

    // Algo cambió al aire. Se puede llamar desde cualquier hilo; se actualiza enseguida.
    void refresh();

    // Resultado del último envío a la página web, para la pestaña Emisión.
    struct PushStatus {
        bool attempted = false;
        bool ok = false;
        int httpStatus = 0;  // 0 = sin respuesta
        juce::Time time;
    };
    PushStatus getPushStatus() const;

    static bool isValidPushUrl(const juce::String& url);

private:
    void timerCallback() override;
    void handleAsyncUpdate() override { rebuild(); }
    void changeListenerCallback(juce::ChangeBroadcaster*) override { refresh(); }

    void rebuild();
    juce::String signature() const;

    Deck& deckA;
    Deck& deckB;
    Playlist& playlist;
    HistoryManager& history;
    MusicLibrary& library;
    StreamEncoder& streamEncoder;
    ApiServer& apiServer;

    juce::String stationName = "RAAADIO";
    juce::String pushUrl;
    juce::String lastSignature;
    juce::uint32 lastRebuild = 0;

    class Poster;  // hilo que hace los POST
    std::unique_ptr<Poster> poster;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NowPlayingPublisher)
};
