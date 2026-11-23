#pragma once
#include <JuceHeader.h>

// Lo que suena, para programas del mismo equipo (una pantalla en cabina, OBS, un script):
//   GET /nowplaying       texto: "Artista - Título", o el nombre de la emisora
//   GET /nowplaying.json  anterior, actual y siguientes (ver NowPlaying.h)
// Solo escucha en 127.0.0.1. Para una página web, NowPlayingPublisher envía el mismo JSON.
class ApiServer : public juce::Thread {
public:
    ApiServer();
    ~ApiServer() override;

    void start(int port);
    void stop();
    int getPort() const { return serverSocket.getBoundPort(); }  // con start(0), el que eligió el sistema

    void update(const juce::String& text, const juce::String& json);

    void run() override;

private:
    juce::StreamingSocket serverSocket;
    juce::String currentText = "RAAADIO";
    juce::String currentJson = "{}";
    std::atomic<bool> isRunning { false };
    juce::CriticalSection lock;
    
    void handleConnection(juce::StreamingSocket* client);
};
