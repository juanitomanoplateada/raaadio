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

void ApiServer::updateNowPlaying(const juce::String& artist, const juce::String& title) {
    juce::ScopedLock sl(lock);
    currentInfo = artist + " - " + title;
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
            juce::String request(buffer, bytesRead);
            juce::String firstLine = request.upToFirstOccurrenceOf("\r\n", false, false);
            
            if (firstLine.containsIgnoreCase("GET /nowplaying")) {
                 
                 juce::String responseBody;
                {
                    juce::ScopedLock sl(lock);
                    responseBody = currentInfo;
                }

                juce::String response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: text/plain; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " + juce::String(responseBody.getNumBytesAsUTF8()) + "\r\n"
                    "Connection: close\r\n"
                    "\r\n" + 
                    responseBody;
                    
                client->write(response.toRawUTF8(), response.getNumBytesAsUTF8());
            } else {
                 
                juce::String responseBody = "Endpoint not found. Use /nowplaying";
                juce::String response = 
                    "HTTP/1.1 404 Not Found\r\n"
                    "Content-Type: text/plain; charset=utf-8\r\n"
                    "Access-Control-Allow-Origin: *\r\n"
                    "Content-Length: " + juce::String(responseBody.getNumBytesAsUTF8()) + "\r\n"
                    "Connection: close\r\n"
                    "\r\n" + 
                    responseBody;
                client->write(response.toRawUTF8(), response.getNumBytesAsUTF8());
            }
        }
    }
    
    client->close();
}
