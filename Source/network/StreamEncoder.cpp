#include "StreamEncoder.h"

namespace {
    // El escritor Ogg escribe aquí; los bytes van directo al servidor. No es dueño del
    // socket: cada flujo encadenado crea uno nuevo sobre la misma conexión.
    class SocketOutputStream : public juce::OutputStream {
    public:
        SocketOutputStream(juce::StreamingSocket& s, std::atomic<bool>& failedFlag) : socket(s), failed(failedFlag) {}

        bool write(const void* data, size_t numBytes) override {
            if (failed) return false;
            size_t sent = 0;
            while (sent < numBytes) {
                const int n = socket.write(static_cast<const char*>(data) + sent, (int) (numBytes - sent));
                if (n <= 0) {
                    failed = true;
                    return false;
                }
                sent += (size_t) n;
            }
            position += (juce::int64) numBytes;
            return true;
        }
        void flush() override {}
        juce::int64 getPosition() override { return position; }
        bool setPosition(juce::int64) override { return false; }

    private:
        juce::StreamingSocket& socket;
        std::atomic<bool>& failed;
        juce::int64 position = 0;
    };
}

StreamEncoder::StreamEncoder() : juce::Thread("Stream Encoder") {}

StreamEncoder::~StreamEncoder() {
    stop();
}

void StreamEncoder::start(const Server& newServer) {
    stop();
    server = newServer;
    status = Status::Connecting;
    startThread();
}

void StreamEncoder::stop() {
    signalThreadShouldExit();
    {
        // Una escritura trabada en una red lenta se destraba cerrando el socket.
        const juce::ScopedLock sl(lock);
        if (activeSocket != nullptr) activeSocket->close();
    }
    notify();
    stopThread(8000);
    sending = false;
    status = Status::Off;
}

juce::String StreamEncoder::getLastError() const {
    const juce::ScopedLock sl(lock);
    return lastError;
}

void StreamEncoder::setNowPlaying(const juce::String& newArtist, const juce::String& newTitle) {
    const juce::ScopedLock sl(lock);
    if (newArtist == artist && newTitle == title) return;
    artist = newArtist;
    title = newTitle;
    metadataChanged = true;
}

void StreamEncoder::pushBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) {
    if (!sending.load()) return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);  // si la red se atrasa, se pierde lo que no cabe
    const int channels = juce::jmin(2, buffer.getNumChannels());
    for (int ch = 0; ch < 2; ++ch) {
        const int source = juce::jmin(ch, channels - 1);
        if (size1 > 0) fifoBuffer.copyFrom(ch, start1, buffer, source, startSample, size1);
        if (size2 > 0) fifoBuffer.copyFrom(ch, start2, buffer, source, startSample + size1, size2);
    }
    fifo.finishedWrite(size1 + size2);
}

juce::String StreamEncoder::buildSourceRequest(const Server& s) {
    const auto mount = s.mount.startsWithChar('/') ? s.mount : "/" + s.mount;
    return "SOURCE " + mount + " HTTP/1.0\r\n"
           "Authorization: Basic " + juce::Base64::toBase64(s.user + ":" + s.password) + "\r\n"
           "User-Agent: RAAADIO\r\n"
           "Content-Type: application/ogg\r\n"
           "ice-name: " + s.name + "\r\n"
           "ice-public: 0\r\n"
           "\r\n";
}

bool StreamEncoder::isAccepted(const juce::String& statusLine) {
    return (statusLine.startsWith("HTTP/") || statusLine.startsWith("ICY")) && statusLine.contains(" 200");
}

void StreamEncoder::fail(const juce::String& message) {
    {
        const juce::ScopedLock sl(lock);
        lastError = message;
    }
    status = Status::Error;
    juce::Logger::writeToLog("StreamEncoder: " + message);
}

void StreamEncoder::run() {
    wait(0);  // descarta un aviso que haya quedado de una detención anterior
    int backoffSeconds = 2;
    while (!threadShouldExit()) {
        if (connectAndStream()) backoffSeconds = 2;  // estuvo conectado: se reintenta pronto
        if (threadShouldExit()) break;
        wait(backoffSeconds * 1000);
        backoffSeconds = juce::jmin(backoffSeconds * 2, 30);
    }
}

bool StreamEncoder::connectAndStream() {
    // Mientras se reintenta después de un error, el estado sigue en error (con su motivo)
    // hasta que la conexión vuelva: así la barra superior no parpadea entre los dos.
    if (status.load() != Status::Error) status = Status::Connecting;
    juce::StreamingSocket socket;
    {
        const juce::ScopedLock sl(lock);
        activeSocket = &socket;
    }
    struct Release {
        StreamEncoder& owner;
        ~Release() { const juce::ScopedLock sl(owner.lock); owner.activeSocket = nullptr; }
    } release { *this };

    if (!socket.connect(server.host, server.port, 5000)) {
        fail(TRANS("stream_err_connect") + " " + server.host + ":" + juce::String(server.port));
        return false;
    }

    const auto request = buildSourceRequest(server);
    if (socket.write(request.toRawUTF8(), (int) request.getNumBytesAsUTF8()) < 0) {
        fail(TRANS("stream_err_lost"));
        return false;
    }

    // Respuesta del servidor, hasta la línea en blanco.
    juce::String response;
    while (response.length() < 4096 && !response.endsWith("\r\n\r\n")) {
        char c = 0;
        if (socket.waitUntilReady(true, 5000) != 1 || socket.read(&c, 1, false) != 1) break;
        response += juce::String::charToString((juce::juce_wchar) (juce::uint8) c);
    }
    const auto statusLine = response.upToFirstOccurrenceOf("\r\n", false, false).trim();
    if (!isAccepted(statusLine)) {
        fail(statusLine.isEmpty() ? TRANS("stream_err_no_response") : TRANS("stream_err_rejected") + ": " + statusLine);
        return false;
    }

    std::atomic<bool> failed { false };
    std::unique_ptr<juce::AudioFormatWriter> writer;
    auto openWriter = [&] {
        juce::StringPairArray metadata;
        {
            const juce::ScopedLock sl(lock);
            metadata.set(juce::OggVorbisAudioFormat::id3title, title.isNotEmpty() ? title : server.name);
            metadata.set(juce::OggVorbisAudioFormat::id3artist, artist);
            metadataChanged = false;
        }
        metadata.set(juce::OggVorbisAudioFormat::encoderName, "RAAADIO");

        juce::OggVorbisAudioFormat ogg;
        auto* out = new SocketOutputStream(socket, failed);
        writer.reset(ogg.createWriterFor(out, preparedRate.load(), 2, 16, metadata, server.quality));
        if (writer == nullptr) delete out;  // si no crea el escritor, el flujo sigue siendo nuestro
    };

    fifo.reset();
    openWriter();
    if (writer == nullptr) {
        fail(TRANS("stream_err_encoder"));
        return false;
    }
    sending = true;
    status = Status::Connected;
    {
        const juce::ScopedLock sl(lock);
        lastError = {};
    }
    juce::Logger::writeToLog("StreamEncoder: connected to " + server.host + ":" + juce::String(server.port) + server.mount);

    while (!threadShouldExit() && !failed) {
        bool newTrack;
        {
            const juce::ScopedLock sl(lock);
            newTrack = metadataChanged;
        }
        if (newTrack) {
            writer.reset();  // cierra el flujo Ogg actual; el siguiente lleva el título nuevo
            if (failed) break;
            openWriter();
            if (writer == nullptr) break;
        }

        const int ready = fifo.getNumReady();
        if (ready < 1024) {
            wait(10);
            continue;
        }
        int start1, size1, start2, size2;
        fifo.prepareToRead(ready, start1, size1, start2, size2);
        if (size1 > 0) writer->writeFromAudioSampleBuffer(fifoBuffer, start1, size1);
        if (size2 > 0) writer->writeFromAudioSampleBuffer(fifoBuffer, start2, size2);
        fifo.finishedRead(size1 + size2);
    }

    sending = false;
    writer.reset();
    socket.close();
    if (!threadShouldExit()) fail(TRANS("stream_err_lost"));
    return true;
}
