#include "StreamEncoder.h"
#include "Mp3Encoder.h"

namespace {
    // El escritor Ogg escribe aquí; los bytes van directo al servidor. No es dueño del
    // socket: cada flujo encadenado crea uno nuevo sobre la misma conexión.
    class SocketOutputStream : public juce::OutputStream {
    public:
        SocketOutputStream(StreamSocket& s, std::atomic<bool>& failedFlag, juce::int64& totalSent)
            : socket(s), failed(failedFlag), total(totalSent) {}

        bool write(const void* data, size_t numBytes) override {
            if (failed) return false;
            if (!socket.writeAll(data, (int) numBytes)) {
                failed = true;
                return false;
            }
            total += (juce::int64) numBytes;
            position += (juce::int64) numBytes;
            return true;
        }
        void flush() override {}
        juce::int64 getPosition() override { return position; }
        bool setPosition(juce::int64) override { return false; }

    private:
        StreamSocket& socket;
        std::atomic<bool>& failed;
        juce::int64& total;  // todo lo enviado en la conexión, entre todos los flujos Ogg
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

    // Lee la respuesta del servidor hasta la línea en blanco, de a un byte para no tomar nada
    // de lo que venga después.
    juce::String readResponse(StreamSocket& socket, int timeoutMs) {
        juce::String response;
        while (response.length() < 4096 && !response.endsWith("\r\n\r\n")) {
            char c = 0;
            if (socket.readSome(&c, 1, timeoutMs) != 1) break;
            response += juce::String::charToString((juce::juce_wchar) (juce::uint8) c);
        }
        return response;
    }

    juce::String statusLineOf(const juce::String& response) {
        return response.upToFirstOccurrenceOf("\r\n", false, false).trim();
    }

    // Las cabeceras de la respuesta en una línea, para el registro (no llevan la clave).
    juce::String headersOf(const juce::String& response) {
        juce::StringArray lines;
        lines.addLines(response.fromFirstOccurrenceOf("\r\n", false, false));
        lines.removeEmptyStrings();
        return lines.joinIntoString(" | ").substring(0, 400);
    }

    const char* methodName(StreamEncoder::Method m) {
        return m == StreamEncoder::Method::Put ? "PUT" : "SOURCE";
    }

    // Lo que manda el servidor al echar al emisor (lo primero ya leído en data), hasta que
    // cierra o se calla, con un tope.
    juce::String readUntilClosed(StreamSocket& socket, juce::MemoryBlock data) {
        char buffer[4096];
        while (data.getSize() < 256 * 1024) {
            const int n = socket.readSome(buffer, (int) sizeof(buffer), 2000);
            if (n <= 0) break;
            data.append(buffer, (size_t) n);
        }
        return juce::String::fromUTF8(static_cast<const char*>(data.getData()), (int) data.getSize());
    }

    // El texto visible de un trozo de HTML, en una línea.
    juce::String plainText(const juce::String& html) {
        juce::String text;
        bool inTag = false;
        for (auto c : html) {
            if (c == '<') inTag = true;
            else if (c == '>') { inTag = false; text << ' '; }
            else if (!inTag) text << juce::String::charToString(c);
        }
        juce::StringArray words;
        words.addTokens(text, " \t\r\n", {});
        words.removeEmptyStrings();
        return words.joinIntoString(" ");
    }

    // El texto del primer <tag> o <tag ...> (no el de <tagotra>: <p> no es <path>).
    juce::String firstElement(const juce::String& html, const juce::String& tag) {
        for (int i = html.indexOfIgnoreCase("<" + tag); i >= 0; i = html.indexOfIgnoreCase(i + 1, "<" + tag)) {
            const auto next = html[i + 1 + tag.length()];
            if (next != '>' && !juce::CharacterFunctions::isWhitespace(next)) continue;
            const auto inner = html.substring(i).fromFirstOccurrenceOf(">", false, false);
            return plainText(inner.upToFirstOccurrenceOf("</" + tag, false, true)).substring(0, 200);
        }
        return {};
    }

    // Para el registro: la línea de estado y, si es una página, su título y lo primero que dice.
    juce::String describeServerMessage(const juce::String& message) {
        auto description = statusLineOf(message);
        const auto body = message.fromFirstOccurrenceOf("\r\n\r\n", false, false);
        for (const char* tag : { "title", "h1", "h2", "p" }) {
            const auto text = firstElement(body, tag);
            if (text.isNotEmpty()) description << " | <" << tag << "> " << text;
        }
        return description;
    }

    // El registro general no lleva hora; la transmisión sí, para saber cuánto duró cada cosa.
    void logLine(const juce::String& text) {
        const auto now = juce::Time::getCurrentTime();
        juce::Logger::writeToLog("StreamEncoder [" + now.formatted("%H:%M:%S") + "."
                                 + juce::String(now.getMilliseconds()).paddedLeft('0', 3) + "]: " + text);
    }
}

StreamEncoder::StreamEncoder() : juce::Thread("Stream Encoder") {}

StreamEncoder::~StreamEncoder() {
    stop();
}

void StreamEncoder::start(const Server& newServer) {
    stop();
    server = newServer;
    method = Method::Put;
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
    samplesIn += size1 + size2;
    samplesDropped += numSamples - (size1 + size2);
}

juce::String StreamEncoder::buildSourceRequest(const Server& s, double sampleRate, Method m) {
    const bool mp3 = s.format == Format::Mp3;
    const int rate = mp3 ? Mp3Encoder::outputRateFor(sampleRate) : juce::roundToInt(sampleRate);

    juce::String request = juce::String(methodName(m)) + " " + withSlash(s.mount)
                           + (m == Method::Put ? " HTTP/1.1\r\n" : " HTTP/1.0\r\n")
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
    logLine(message);
}

void StreamEncoder::run() {
    wait(0);  // descarta un aviso que haya quedado de una detención anterior
    int backoffSeconds = 2;
    while (!threadShouldExit()) {
        // Tras una conexión que duró, se reintenta pronto. Si el servidor corta a los pocos
        // segundos (un límite del plan, un dato que no le gusta), insistir cada 2 s no lo
        // arregla, y algunos servicios bloquean al que insiste: la espera sigue creciendo.
        if (connectAndStream() >= 60) backoffSeconds = 2;
        if (threadShouldExit()) break;
        wait(backoffSeconds * 1000);
        backoffSeconds = juce::jmin(backoffSeconds * 2, 30);
    }
}

// Abre la conexión y se presenta. Si el servidor no acepta PUT, se prueba SOURCE en otra
// conexión (algunos cortan después de rechazar), y el método que funcionó queda para las
// reconexiones.
bool StreamEncoder::openStream(StreamSocket& socket, double rate) {
    juce::String rejection;
    const auto first = method;
    for (const auto m : { first, Method::Source }) {
        if (threadShouldExit()) return false;
        if (!socket.connect(server.host, server.port, server.tls, 5000)) {
            const auto tlsProblem = socket.getLastError();
            fail(tlsProblem.isNotEmpty() ? TRANS("stream_err_tls") + ": " + tlsProblem
                                         : TRANS("stream_err_connect") + " " + server.host + ":" + juce::String(server.port));
            return false;
        }
        const auto request = buildSourceRequest(server, rate, m);
        juce::String response;
        if (socket.writeAll(request.toRawUTF8(), (int) request.getNumBytesAsUTF8()))
            response = readResponse(socket, 5000);
        const auto statusLine = statusLineOf(response);
        logLine(juce::String(methodName(m)) + " " + withSlash(server.mount) + ": "
            + (statusLine.isEmpty() ? juce::String("no response") : statusLine + " | " + headersOf(response)));
        if (isAccepted(statusLine)) {
            method = m;
            return true;
        }
        socket.close();
        rejection = statusLine.isEmpty() ? TRANS("stream_err_no_response") : TRANS("stream_err_rejected") + ": " + statusLine;
        if (m == Method::Source) break;  // ya era SOURCE: no hay otro que probar
    }
    if (!threadShouldExit()) fail(rejection);
    return false;
}

int StreamEncoder::connectAndStream() {
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
        return -1;
    }

    StreamSocket socket;
    {
        const juce::ScopedLock sl(lock);
        activeSocket = &socket;
    }
    struct Release {
        StreamEncoder& owner;
        ~Release() { const juce::ScopedLock sl(owner.lock); owner.activeSocket = nullptr; }
    } release { *this };

    if (!openStream(socket, rate)) return -1;

    std::atomic<bool> failed { false };
    juce::int64 bytesSent = 0;
    SocketOutputStream mp3Out(socket, failed, bytesSent);
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
        auto* out = new SocketOutputStream(socket, failed, bytesSent);
        writer.reset(ogg.createWriterFor(out, rate, 2, 16, metadata, server.quality));
        if (writer == nullptr) delete out;  // si no crea el escritor, el flujo sigue siendo nuestro
        return writer != nullptr;
    };

    fifo.reset();
    limiter.prepare(rate);
    if (!startTrack(true)) {
        fail(TRANS("stream_err_encoder"));
        return -1;
    }
    samplesIn = 0;
    samplesDropped = 0;
    sending = true;
    status = Status::Connected;
    {
        const juce::ScopedLock sl(lock);
        lastError = {};
    }
    const auto connectedAt = juce::Time::getMillisecondCounter();
    logLine("connected to " + server.host + ":" + juce::String(server.port) + withSlash(server.mount) + (server.tls ? " over TLS" : "") + " with "
        + methodName(method) + (mp3 ? " (MP3 " + juce::String(server.bitrate) + " kbps, " : juce::String(" (Ogg Vorbis, "))
        + juce::String(mp3 ? Mp3Encoder::outputRateFor(rate) : juce::roundToInt(rate)) + " Hz)");

    bool encoderFailed = false, serverClosed = false;
    juce::String serverSaid;  // lo que mande el servidor después de aceptar: a veces, el motivo del corte
    juce::MemoryBlock encoded;
    auto encodeRegion = [&](int start, int size) {
        if (size <= 0) return;
        limiter.process(fifoBuffer.getArrayOfWritePointers(), 2, start, size);
        if (mp3) {
            if (!mp3Encoder.encode(fifoBuffer.getReadPointer(0, start), fifoBuffer.getReadPointer(1, start), size, encoded))
                encoderFailed = true;
        } else {
            writer->writeFromAudioSampleBuffer(fifoBuffer, start, size);
        }
    };

    while (!threadShouldExit() && !failed && !encoderFailed) {
        // Icecast no le escribe al emisor después de aceptarlo: si llega algo, es el servidor
        // echándolo (a veces con una página que dice por qué) o cerrando. Se nota aquí,
        // enseguida, y no cuando por fin falla un envío: hay servidores que siguen aceptando
        // datos unos segundos sin pasarlos a nadie.
        char first[4096];
        const int incoming = socket.readSome(first, (int) sizeof(first), 0);
        if (incoming != 0) {
            if (incoming > 0) serverSaid = readUntilClosed(socket, juce::MemoryBlock(first, (size_t) incoming));
            serverClosed = true;
            break;
        }

        bool newTrack;
        {
            const juce::ScopedLock sl(lock);
            newTrack = metadataChanged;
        }
        if (newTrack && !startTrack(false)) break;

        const int available = fifo.getNumReady();
        if (available < 1024) {
            wait(10);
            continue;
        }
        int start1, size1, start2, size2;
        fifo.prepareToRead(available, start1, size1, start2, size2);
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
    if (mp3 && !failed && !encoderFailed && !serverClosed) {
        mp3Encoder.flush(encoded);
        if (encoded.getSize() > 0) mp3Out.write(encoded.getData(), encoded.getSize());
    }
    writer.reset();
    socket.close();

    // Lo que hace falta para saber quién cortó y si el audio salía: cuánto duró, cuánto se
    // envió y cuánto audio entregó el mezclador.
    const double seconds = (juce::Time::getMillisecondCounter() - connectedAt) / 1000.0;
    const double audioRate = juce::jmax(1.0, rate);
    const juce::String reason = threadShouldExit() ? "stopped"
                              : serverClosed      ? "the server closed the connection"
                              : encoderFailed     ? "the encoder failed"
                                                  : "sending failed";
    logLine(reason + " after " + juce::String(seconds, 1) + " s: sent " + juce::String(bytesSent / 1024) + " KB ("
        + juce::String(seconds > 0.0 ? (double) bytesSent * 8.0 / 1000.0 / seconds : 0.0, 0) + " kbps), "
        + juce::String((double) samplesIn.load() / audioRate, 1) + " s of audio from the mixer, "
        + juce::String((double) samplesDropped.load() / audioRate, 1) + " s dropped");

    // Lo que dijo el servidor al echarnos, entero en un archivo (puede ser una página web
    // con el motivo) y resumido en el registro y en el error que ve el usuario.
    juce::String serverStatus;
    if (serverSaid.isNotEmpty()) {
        const auto saved = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("raaadio_stream_server_message.html");
        saved.replaceWithText(serverSaid);
        logLine("the server said: " + describeServerMessage(serverSaid) + " (full text: " + saved.getFullPathName() + ")");
        const auto line = statusLineOf(serverSaid);
        if (line.startsWith("HTTP/")) serverStatus = line.fromFirstOccurrenceOf(" ", false, false);  // "403 Forbidden"
    }

    if (!threadShouldExit())
        fail(serverClosed    ? TRANS("stream_err_closed") + " " + juce::String(juce::roundToInt(seconds)) + " s"
                                   + (serverStatus.isNotEmpty() ? " (" + serverStatus + ")" : juce::String())
             : encoderFailed ? TRANS("stream_err_encoder")
                             : TRANS("stream_err_lost"));
    return (int) seconds;
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
    StreamSocket socket;
    {
        const juce::ScopedLock sl(lock);
        activeSocket = &socket;
    }
    struct Release {
        MetadataSender& owner;
        ~Release() { const juce::ScopedLock sl(owner.lock); owner.activeSocket = nullptr; }
    } release { *this };

    // Por la misma vía que el audio: si va cifrado, la clave del título también.
    juce::String statusLine;
    if (socket.connect(server.host, server.port, server.tls, 5000)) {
        const auto request = buildMetadataRequest(server, song);
        if (socket.writeAll(request.toRawUTF8(), (int) request.getNumBytesAsUTF8()))
            statusLine = statusLineOf(readResponse(socket, 5000));
    }
    socket.close();

    // Un título que no llegó no corta la transmisión: se anota y sigue el audio.
    if (isAccepted(statusLine))
        logLine("title sent: " + song);
    else if (!threadShouldExit())
        logLine("title not accepted (" + (statusLine.isEmpty() ? juce::String("no response") : statusLine) + ")");
}
