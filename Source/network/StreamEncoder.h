#pragma once
#include <JuceHeader.h>
#include "../audio/OutputTap.h"

// Transmisión por internet: codifica la salida al aire en Ogg Vorbis y la envía a un
// servidor Icecast (método SOURCE, compatible con Icecast 2). Si se corta la conexión,
// reintenta sola con esperas crecientes. Cada cambio de canción abre un nuevo flujo Ogg
// encadenado con el título y el artista, que los reproductores muestran.
class StreamEncoder : public OutputTap, private juce::Thread {
public:
    struct Server {
        juce::String host;
        int port = 8000;
        juce::String mount = "/radio.ogg";
        juce::String user = "source";
        juce::String password;
        juce::String name = "RAAADIO";
        int quality = 4;  // índice de calidad Ogg Vorbis (4 ≈ 128 kbps)

        bool operator==(const Server& o) const {
            return host == o.host && port == o.port && mount == o.mount && user == o.user
                && password == o.password && name == o.name && quality == o.quality;
        }
        bool operator!=(const Server& o) const { return !(*this == o); }
    };

    enum class Status { Off, Connecting, Connected, Error };

    StreamEncoder();
    ~StreamEncoder() override;

    void start(const Server& server);
    void stop();
    Status getStatus() const { return status.load(); }
    juce::String getLastError() const;

    void setNowPlaying(const juce::String& artist, const juce::String& title);

    // OutputTap (hilo de audio)
    void prepare(double sampleRate) override { preparedRate = sampleRate; }
    void pushBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;

    // Partes del protocolo, separadas para las pruebas.
    static juce::String buildSourceRequest(const Server& server);
    static bool isAccepted(const juce::String& responseStatusLine);

private:
    void run() override;
    bool connectAndStream();
    void fail(const juce::String& message);

    Server server;
    std::atomic<Status> status { Status::Off };
    std::atomic<bool> sending { false };  // el hilo de audio solo encola mientras hay conexión
    std::atomic<double> preparedRate { 44100.0 };

    juce::AbstractFifo fifo { 131072 };
    juce::AudioBuffer<float> fifoBuffer { 2, 131072 };

    mutable juce::CriticalSection lock;
    juce::StreamingSocket* activeSocket = nullptr;  // para cortar una escritura trabada al detener
    juce::String lastError;
    juce::String artist, title;
    bool metadataChanged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamEncoder)
};
