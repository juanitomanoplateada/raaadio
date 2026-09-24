#pragma once
#include <JuceHeader.h>
#include <deque>
#include <tuple>
#include "../audio/OutputTap.h"
#include "../audio/MasterLimiter.h"

// Transmisión por internet: codifica la salida al aire en MP3 u Ogg Vorbis y la envía a
// un servidor Icecast (método SOURCE, compatible con Icecast 2, Zeno.fm y similares). Si se
// corta la conexión, reintenta sola con esperas crecientes.
//
// El título de lo que suena llega a los oyentes de dos maneras, según el formato:
//  - Ogg: cada cambio de canción abre un nuevo flujo Ogg encadenado con título y artista.
//  - MP3: el título ("Artista - Título") se manda aparte, con /admin/metadata, en otra
//    conexión y desde otro hilo para no frenar el audio; el servidor lo intercala en el
//    flujo de cada oyente. Se puede retrasar unos segundos para que coincida con el audio.
class StreamEncoder : public OutputTap, private juce::Thread {
public:
    enum class Format { OggVorbis, Mp3 };

    struct Server {
        juce::String host;
        int port = 8000;
        juce::String mount = "/radio";
        juce::String user = "source";
        juce::String password;
        juce::String name = "RAAADIO";
        Format format = Format::Mp3;
        int quality = 4;    // Ogg Vorbis: índice de calidad (4 ≈ 128 kbps)
        int bitrate = 128;  // MP3: kbps
        juce::String genre, description, website;
        bool isPublic = false;
        int metadataDelaySeconds = 0;  // solo MP3

        auto tie() const {
            return std::tie(host, port, mount, user, password, name, format, quality, bitrate,
                            genre, description, website, isPublic, metadataDelaySeconds);
        }
        bool operator==(const Server& o) const { return tie() == o.tie(); }
        bool operator!=(const Server& o) const { return !(*this == o); }
    };

    enum class Status { Off, Connecting, Connected, Error };

    // Techo de pico de lo que se transmite: al comprimir aparecen picos por encima del
    // original, y en 0 dBFS se oirían como distorsión en el reproductor del oyente.
    static constexpr float ceilingDb = -1.0f;

    StreamEncoder();
    ~StreamEncoder() override;

    void start(const Server& server);
    void stop();
    Status getStatus() const { return status.load(); }
    juce::String getLastError() const;

    // Lo que ven los oyentes. Sin artista (una cuña, una identificación), solo el título,
    // que en ese caso suele ser el nombre de la emisora.
    void setNowPlaying(const juce::String& artist, const juce::String& title);

    // OutputTap (hilo de audio)
    void prepare(double sampleRate) override { preparedRate = sampleRate; }
    void pushBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;

    // Partes del protocolo, separadas para las pruebas.
    static juce::String buildSourceRequest(const Server& server, double sampleRate = 44100.0);
    static juce::String buildMetadataRequest(const Server& server, const juce::String& song);
    static juce::String songText(const juce::String& artist, const juce::String& title);
    static bool isAccepted(const juce::String& responseStatusLine);

private:
    // Títulos para MP3, cada uno a su hora, por /admin/metadata.
    class MetadataSender : private juce::Thread {
    public:
        MetadataSender();
        ~MetadataSender() override;
        void start(const Server& server);
        void stop();
        void post(const juce::String& song, juce::uint32 dueMillisecondCounter);
        void clear();

    private:
        void run() override;
        void send(const juce::String& song);

        Server server;
        juce::CriticalSection lock;
        std::deque<std::pair<juce::uint32, juce::String>> pending;
        juce::StreamingSocket* activeSocket = nullptr;
    };

    void run() override;
    bool connectAndStream();
    void fail(const juce::String& message);

    Server server;
    std::atomic<Status> status { Status::Off };
    std::atomic<bool> sending { false };  // el hilo de audio solo encola mientras hay conexión
    std::atomic<double> preparedRate { 44100.0 };

    juce::AbstractFifo fifo { 131072 };
    juce::AudioBuffer<float> fifoBuffer { 2, 131072 };
    MasterLimiter limiter { ceilingDb };
    MetadataSender metadataSender;

    mutable juce::CriticalSection lock;
    juce::StreamingSocket* activeSocket = nullptr;  // para cortar una escritura trabada al detener
    juce::String lastError;
    juce::String artist, title;
    bool metadataChanged = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StreamEncoder)
};
