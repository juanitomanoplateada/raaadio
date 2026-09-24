#pragma once
#include <JuceHeader.h>

class ApiServer : public juce::Thread {
public:
    ApiServer();
    ~ApiServer();

    void start(int port);
    void stop();
    
     
    void updateNowPlaying(const juce::String& artist, const juce::String& title);

    void run() override;

private:
    juce::StreamingSocket serverSocket;
    juce::String currentInfo = "Radio Station - Ready";
    std::atomic<bool> isRunning { false };
    juce::CriticalSection lock;
    
    void handleConnection(juce::StreamingSocket* client);
};
