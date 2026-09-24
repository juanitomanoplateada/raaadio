#include "StreamEncoder.h"
#include "Mp3Encoder.h"

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

    // Un texto del usuario dentro de una cabecera HTTP: en una sola línea.
    juce::String headerValue(const juce::String& text) {
        return text.replaceCharacters("\r\n\t", "   ").trim();
    }

    juce::String withSlash(const juce::String& mount) {
        return mount.startsWithChar('/') ? mount : "/" + mount;
    }

    juce::String basicAuth(const StreamEncoder::Server& s) {
        return "Authorization: Basic " + juce::Base64::toBase64(s.user + ":" + s.password) + "\r\n";
    }

    juce::String hostHeader(const StreamEncoder::Server& s) {
        return "Host: " + s.host + (s.port == 80 ? juce::String() : ":" + juce::String(s.port)) + "\r\n";
    }

    const juce::String userAgent = "User-Agent: RAAADIO/" + juce::String(ProjectInfo::versionString) + "\r\n";

    // Lee la respuesta del servidor hasta la línea en blanco y devuelve la primera línea.
    juce::String readStatusLine(juce::StreamingSocket& socket, int timeoutMs) {
        juce::String response;
        while (response.length() < 4096 && !response.endsWith("\r\n\r\n")) {
            char c = 0;
            if (socket.waitUntilReady(true, timeoutMs) != 1 || socket.read(&c, 1, false) != 1) break;
            response += juce::String::charToString((juce::juce_wchar) (juce::uint8) c);
        }
        return response.upToFirstOccurrenceOf("\r\n", false, false).trim();
    }
}

StreamEncoder::StreamEncoder() : juce::Thread("Stream Encoder") {}

StreamEncoder::~StreamEncoder() {
    stop();
}

void StreamEncoder::start(const Server& newServer) {
    stop();
    server = newServer;
    status = Status::Connecting;
    if (server.format == Format::Mp3) metadataSender.start(server);
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
    metadataSender.stop();
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

juce::String StreamEncoder::buildSourceRequest(const Server& s, double sampleRate) {
    const bool mp3 = s.format == Format::Mp3;
    const int rate = mp3 ? Mp3Encoder::outputRateFor(sampleRate) : juce::roundToInt(sampleRate);

    juce::String request = "SOURCE " + withSlash(s.mount) + " HTTP/1.0\r\n"
                           + hostHeader(s) + basicAuth(s) + userAgent
                           + "Content-Type: " + (mp3 ? "audio/mpeg" : "application/ogg") + "\r\n"
                           + "ice-name: " + headerValue(s.name) + "\r\n"
                           + "ice-public: " + (s.isPublic ? "1" : "0") + "\r\n";
    if (s.genre.isNotEmpty()) request << "ice-genre: " << headerValue(s.genre) << "\r\n";
    if (s.description.isNotEmpty()) request << "ice-description: " << headerValue(s.description) << "\r\n";
    if (s.website.isNotEmpty()) request << "ice-url: " << headerValue(s.website) << "\r\n";
    if (mp3) {
        request << "ice-bitrate: " << s.bitrate << "\r\n"
                << "ice-audio-info: ice-samplerate=" << rate << ";ice-bitrate=" << s.bitrate << ";ice-channels=2\r\n";
    } else {
        request << "ice-audio-info: ice-samplerate=" << rate << ";ice-quality=" << s.quality << ";ice-channels=2\r\n";
    }
    return request + "\r\n";
}

juce::String StreamEncoder::buildMetadataRequest(const Server& s, const juce::String& song) {
    auto escape = [](const juce::String& text) { return juce::URL::addEscapeChars(text, true, false); };
    return "GET /admin/metadata?mount=" + escape(withSlash(s.mount)) + "&mode=updinfo&song=" + escape(song)
           + "&charset=UTF-8 HTTP/1.0\r\n"
           + hostHeader(s) + basicAuth(s) + userAgent + "\r\n";
}

juce::String StreamEncoder::songText(const juce::String& artist, const juce::String& title) {
    const auto a = artist.trim(), t = title.trim();
    if (a.isEmpty()) return t;
    if (t.isEmpty()) return a;
    return a + " - " + t;
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

    const bool mp3 = server.format == Format::Mp3;
    const double rate = preparedRate.load();

    // El codificador MP3 se prepara antes de conectar: si falta LAME no tiene sentido
    // ocupar el punto de montaje.
    Mp3Encoder mp3Encoder;
    if (mp3 && !mp3Encoder.open(rate, server.bitrate)) {
        fail(Mp3Encoder::isAvailable() ? TRANS("stream_err_encoder") : TRANS("stream_err_no_lame"));
        return false;
    }

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

    const auto request = buildSourceRequest(server, rate);
    if (socket.write(request.toRawUTF8(), (int) request.getNumBytesAsUTF8()) < 0) {
        fail(TRANS("stream_err_lost"));
        return false;
    }

    const auto statusLine = readStatusLine(socket, 5000);
    if (!isAccepted(statusLine)) {
        fail(statusLine.isEmpty() ? TRANS("stream_err_no_response") : TRANS("stream_err_rejected") + ": " + statusLine);
        return false;
    }

    std::atomic<bool> failed { false };
    SocketOutputStream mp3Out(socket, failed);
    std::unique_ptr<juce::AudioFormatWriter> writer;  // Ogg

    // Ogg: un flujo con el título en sus comentarios. MP3: el título va aparte; al
    // conectar sale enseguida (el oyente que entra ya oye esta canción), los siguientes
    // con el retardo configurado.
    auto startTrack = [&](bool justConnected) {
        juce::String currentArtist, currentTitle;
        {
            const juce::ScopedLock sl(lock);
            currentArtist = artist;
            currentTitle = title.isNotEmpty() ? title : server.name;
            metadataChanged = false;
        }
        if (mp3) {
            const auto delay = justConnected ? 0u : (juce::uint32) server.metadataDelaySeconds * 1000u;
            metadataSender.post(songText(currentArtist, currentTitle), juce::Time::getMillisecondCounter() + delay);
            return true;
        }

        juce::StringPairArray metadata;
        metadata.set(juce::OggVorbisAudioFormat::id3title, currentTitle);
        metadata.set(juce::OggVorbisAudioFormat::id3artist, currentArtist);
        metadata.set(juce::OggVorbisAudioFormat::encoderName, "RAAADIO");

        writer.reset();  // cierra el flujo Ogg anterior
        if (failed) return false;
        juce::OggVorbisAudioFormat ogg;
        auto* out = new SocketOutputStream(socket, failed);
        writer.reset(ogg.createWriterFor(out, rate, 2, 16, metadata, server.quality));
        if (writer == nullptr) delete out;  // si no crea el escritor, el flujo sigue siendo nuestro
        return writer != nullptr;
    };

    fifo.reset();
    limiter.prepare(rate);
    if (!startTrack(true)) {
        fail(TRANS("stream_err_encoder"));
        return false;
    }
    sending = true;
    status = Status::Connected;
    {
        const juce::ScopedLock sl(lock);
        lastError = {};
    }
    juce::Logger::writeToLog("StreamEncoder: connected to " + server.host + ":" + juce::String(server.port)
                             + withSlash(server.mount) + (mp3 ? " (MP3)" : " (Ogg Vorbis)"));

    juce::MemoryBlock encoded;
    auto encodeRegion = [&](int start, int size) {
        if (size <= 0) return;
        limiter.process(fifoBuffer.getArrayOfWritePointers(), 2, start, size);
        if (mp3) {
            if (!mp3Encoder.encode(fifoBuffer.getReadPointer(0, start), fifoBuffer.getReadPointer(1, start), size, encoded))
                failed = true;
        } else {
            writer->writeFromAudioSampleBuffer(fifoBuffer, start, size);
        }
    };

    while (!threadShouldExit() && !failed) {
        bool newTrack;
        {
            const juce::ScopedLock sl(lock);
            newTrack = metadataChanged;
        }
        if (newTrack && !startTrack(false)) break;

        const int ready = fifo.getNumReady();
        if (ready < 1024) {
            wait(10);
            continue;
        }
        int start1, size1, start2, size2;
        fifo.prepareToRead(ready, start1, size1, start2, size2);
        encodeRegion(start1, size1);
        encodeRegion(start2, size2);
        fifo.finishedRead(size1 + size2);

        if (encoded.getSize() > 0) {
            mp3Out.write(encoded.getData(), encoded.getSize());
            encoded.reset();
        }
    }

    sending = false;
    metadataSender.clear();  // los títulos pendientes eran para esta conexión
    if (mp3 && !failed) {
        mp3Encoder.flush(encoded);
        if (encoded.getSize() > 0) mp3Out.write(encoded.getData(), encoded.getSize());
    }
    writer.reset();
    socket.close();
    if (!threadShouldExit()) fail(TRANS("stream_err_lost"));
    return true;
}

//==============================================================================
StreamEncoder::MetadataSender::MetadataSender() : juce::Thread("Stream Metadata") {}

StreamEncoder::MetadataSender::~MetadataSender() {
    stop();
}

void StreamEncoder::MetadataSender::start(const Server& newServer) {
    stop();
    server = newServer;
    startThread();
}

void StreamEncoder::MetadataSender::stop() {
    signalThreadShouldExit();
    {
        const juce::ScopedLock sl(lock);
        if (activeSocket != nullptr) activeSocket->close();
    }
    notify();
    stopThread(8000);
    clear();
}

void StreamEncoder::MetadataSender::post(const juce::String& song, juce::uint32 due) {
    {
        const juce::ScopedLock sl(lock);
        pending.emplace_back(due, song);
    }
    notify();
}

void StreamEncoder::MetadataSender::clear() {
    const juce::ScopedLock sl(lock);
    pending.clear();
}

void StreamEncoder::MetadataSender::run() {
    wait(0);
    while (!threadShouldExit()) {
        juce::String song;
        bool due = false;
        int waitMs = -1;  // sin pendientes: hasta que llegue uno
        {
            const juce::ScopedLock sl(lock);
            const auto now = juce::Time::getMillisecondCounter();
            // Si se acumularon varios que ya tocaban (una ráfaga de cambios), solo cuenta el último.
            while (!pending.empty() && (juce::int32) (pending.front().first - now) <= 0) {
                song = pending.front().second;
                pending.pop_front();
                due = true;
            }
            if (!due && !pending.empty()) waitMs = (int) (pending.front().first - now);
        }
        if (!due) wait(waitMs);
        else if (song.isNotEmpty()) send(song);
    }
}

void StreamEncoder::MetadataSender::send(const juce::String& song) {
    juce::StreamingSocket socket;
    {
        const juce::ScopedLock sl(lock);
        activeSocket = &socket;
    }
    struct Release {
        MetadataSender& owner;
        ~Release() { const juce::ScopedLock sl(owner.lock); owner.activeSocket = nullptr; }
    } release { *this };

    juce::String statusLine;
    if (socket.connect(server.host, server.port, 5000)) {
        const auto request = buildMetadataRequest(server, song);
        if (socket.write(request.toRawUTF8(), (int) request.getNumBytesAsUTF8()) >= 0)
            statusLine = readStatusLine(socket, 5000);
    }
    socket.close();

    // Un título que no llegó no corta la transmisión: se anota y sigue el audio.
    if (isAccepted(statusLine))
        juce::Logger::writeToLog("StreamEncoder: title sent: " + song);
    else if (!threadShouldExit())
        juce::Logger::writeToLog("StreamEncoder: title not accepted (" + (statusLine.isEmpty() ? juce::String("no response") : statusLine) + ")");
}
