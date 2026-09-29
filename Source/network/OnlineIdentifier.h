#pragma once
#include <JuceHeader.h>
#include <deque>
#include "../library/MusicLibrary.h"
#include "HttpTransport.h"

// Identificación en línea de la música por revisar: huella de Chromaprint → AcoustID (qué
// grabación es) → MusicBrainz (sus datos). Trabaja en su propio hilo, respeta los límites de
// los servicios (AcoustID, 3 pedidos por segundo; MusicBrainz, 1) y guarda lo encontrado en la
// base: la revisión lo muestra como sugerencia, nunca lo aplica sola. Solo envía la huella y la
// duración, nunca el audio. Busca sola lo que entra si la emisora lo activa y pone su clave de
// AcoustID (Ajustes); "Buscar en línea" a mano solo pide la clave.
class OnlineIdentifier : public juce::ChangeBroadcaster, private juce::Thread {
public:
    using Request = HttpTransport::Request;
    using Response = HttpTransport::Response;
    using Transport = HttpTransport::Transport;

    // transport: para las pruebas; por omisión, HTTP de verdad.
    explicit OnlineIdentifier(MusicLibrary& library, Transport transport = {});
    ~OnlineIdentifier() override;

    // Ajustes, guardados en la base de la emisora.
    bool isEnabled();
    void setEnabled(bool enabled);
    juce::String getApiKey();
    void setApiKey(const juce::String& key);
    static bool isChromaprintAvailable();
    bool canSearch();  // hay clave y chromaprint.dll

    // A la fila: lo que está por revisar (y la biblioteca sin revisar) que no se ha buscado o
    // dio error. Solo si está activado.
    void enqueuePending();
    // "Buscar en línea" a mano: estas pistas primero, aunque ya se hayan buscado.
    void enqueue(const std::vector<int>& trackIds);

    struct Status {
        int queued = 0, found = 0, notFound = 0, errors = 0;
        bool working = false;
        juce::String lastError;
    };
    Status getStatus() const;

    enum class Outcome { Found, NotFound, Error, Skipped };
    // Una pista de principio a fin (lo hace el hilo; las pruebas lo llaman directo).
    Outcome identify(int trackId);

    // Menos de esto, AcoustID no está seguro: no se sugiere.
    static constexpr double minimumScore = 0.5;

private:
    MusicLibrary& library;
    Transport transport;
    juce::AudioFormatManager formats;

    mutable juce::CriticalSection lock;
    std::deque<int> queue;
    Status status;
    juce::uint32 lastAcoustId = 0, lastMusicBrainz = 0;

    void run() override;
    Response send(Request request);
    void waitTurn(juce::uint32& last, int intervalMs);
    Outcome save(int trackId, const Database::OnlineMatch& match);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OnlineIdentifier)
};
