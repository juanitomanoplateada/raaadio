#include "ApiServer.h"

ApiServer::ApiServer() : Thread("ApiServerThread") {}

ApiServer::~ApiServer() {
    stop();
}

void ApiServer::start(int port) {
    if (serverSocket.createListener(port, "127.0.0.1")) {
        isRunning = true;
        startThread();
        juce::Logger::writeToLog("API Server started on port " + juce::String(port));
    } else {
        juce::Logger::writeToLog("Failed to start API Server on port " + juce::String(port));
    }
}

void ApiServer::stop() {
    isRunning = false;
    serverSocket.close();
    stopThread(2000);
}

void ApiServer::update(const juce::String& text, const juce::String& json) {
    juce::ScopedLock sl(lock);
    currentText = text;
    currentJson = json;
}

void ApiServer::run() {
    while (isRunning && !threadShouldExit()) {
        auto* client = serverSocket.waitForNextConnection();
        if (client) {
            handleConnection(client);
            delete client;
        }
    }
}

void ApiServer::handleConnection(juce::StreamingSocket* client) {
    if (!client->isConnected()) return;

    if (client->waitUntilReady(true, 2000)) {
        char buffer[1024];
        int bytesRead = client->read(buffer, 1024, false);
        
        if (bytesRead > 0) {
            // "GET /ruta?consulta HTTP/1.1": solo cuenta la ruta.
            const juce::String request(buffer, (size_t) bytesRead);
            const auto tokens = juce::StringArray::fromTokens(request.upToFirstOccurrenceOf("\r\n", false, false), " ", "");
            const auto path = tokens.size() >= 2 && tokens[0] == "GET" ? tokens[1].upToFirstOccurrenceOf("?", false, false) : juce::String();

            int status = 200;
            juce::String type = "text/plain", body;
            if (path == "/nowplaying.json") {
                juce::ScopedLock sl(lock);
                type = "application/json";
                body = currentJson;
            } else if (path == "/nowplaying") {
                juce::ScopedLock sl(lock);
                body = currentText;
            } else {
                status = 404;
                body = "Endpoint not found. Use /nowplaying or /nowplaying.json";
            }

            juce::String response =
                juce::String("HTTP/1.1 ") + (status == 200 ? "200 OK" : "404 Not Found") + "\r\n"
                "Content-Type: " + type + "; charset=utf-8\r\n"
                "Access-Control-Allow-Origin: *\r\n"
                "Cache-Control: no-store\r\n"
                "Content-Length: " + juce::String(body.getNumBytesAsUTF8()) + "\r\n"
                "Connection: close\r\n"
                "\r\n" +
                body;

            client->write(response.toRawUTF8(), (int) response.getNumBytesAsUTF8());
        }
    }
    
    client->close();
}
