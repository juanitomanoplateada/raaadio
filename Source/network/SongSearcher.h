#pragma once
#include <JuceHeader.h>
#include <map>
#include <optional>
#include "SongSearch.h"

// Hace los pedidos de la búsqueda de canciones en su propio hilo (la ventana nunca se
// congela): la búsqueda en Deezer y, para la canción elegida, su detalle, su carátula y sus
// enlaces de YouTube. Cuida a YouTube: un pedido a la vez con una pausa entre ellos, solo para
// la canción que se mira, lo ya encontrado no se vuelve a pedir y, si YouTube pide esperar
// (HTTP 429), no se le pide nada durante 15 minutos.
class SongSearcher : public juce::ChangeBroadcaster, private juce::Thread {
public:
    // transport: para las pruebas; por omisión, HTTP de verdad. language: "es" o "en".
    explicit SongSearcher(HttpTransport::Transport transport = {}, const juce::String& language = "es");
    ~SongSearcher() override;

    // Busca en Deezer. Una búsqueda nueva reemplaza a la que esperaba su turno.
    void search(const juce::String& query);
    struct Results {
        int generation = 0;      // cambia cada vez que termina una búsqueda
        juce::String query;
        bool working = false;
        std::vector<SongSearch::Track> tracks;
        juce::String error;      // "offline", "http 503" o el mensaje de Deezer
    };
    Results getResults() const;

    // Enlaces de una canción. Lo pedido queda guardado: pedir la misma otra vez no se conecta
    // (salvo que la vez anterior diera error).
    void resolve(const SongSearch::Track& track);
    struct Links {
        enum class State { Pending, Done, Error };
        State state = State::Pending;
        SongSearch::Track track;                // con año e invitados, si Deezer los dio
        std::vector<SongSearch::Video> videos;  // de más a menos parecido
        juce::String error;                     // "offline", "youtube_wait", "http 400"...
        juce::Image cover;
    };
    // false si nunca se pidió.
    bool getLinks(juce::int64 trackId, Links& out) const;

    // Pausa mínima entre dos pedidos a YouTube; las pruebas la acortan.
    void setYouTubeInterval(int milliseconds) { youTubeInterval = milliseconds; }
    static constexpr int maxVideos = 8;

private:
    HttpTransport::Transport transport;
    juce::String language;
    std::atomic<int> youTubeInterval { 1500 };

    mutable juce::CriticalSection lock;
    std::optional<juce::String> pendingQuery;
    std::optional<SongSearch::Track> pendingTrack;
    Results results;
    std::map<juce::int64, Links> links;

    juce::uint32 lastYouTube = 0;
    juce::uint32 youTubeBlockedUntil = 0;

    void run() override;
    void runSearch(const juce::String& query);
    void runResolve(SongSearch::Track track);
    // Busca en YouTube Music o YouTube. false si no se pudo (error dice por qué).
    bool askYouTube(const HttpTransport::Request& request, bool music, std::vector<SongSearch::Video>& videos,
                    juce::String& error);
    HttpTransport::Response send(HttpTransport::Request request);
    static juce::String describe(const HttpTransport::Response& response);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongSearcher)
};
