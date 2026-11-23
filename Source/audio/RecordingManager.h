#pragma once
#include <JuceHeader.h>
#include "OutputTap.h"

class RecordingManager : public juce::Thread, public juce::ChangeBroadcaster, public OutputTap {
public:
    RecordingManager();
    ~RecordingManager() override;

    enum State {
        Idle,
        Recording,
        Paused,
        Saving,
        Error
    };

    void initialise(juce::AudioFormatManager& formatManager);
    
    void processBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    // OutputTap
    void prepare(double rate) override { setSampleRate(rate); }
    void pushBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override {
        processBlock(buffer, startSample, numSamples);
    }

    juce::String startRecording(bool deckA_Air, bool deckB_Air); 
    void pauseRecording();
    void resumeRecording();
    void stopRecording(); 
    
    State getState() const { return currentState; }
    juce::String getDurationString() const;
    juce::File getLastRecording() const;
    void openRecordingsFolder();
    void setRecordingsFolder(const juce::File& folder);
    void setSampleRate(double rate) { sampleRate = rate; }

private:
    juce::AudioFormatManager* formatManager = nullptr;
    std::atomic<State> currentState { Idle };
    
    juce::AbstractFifo fifo { 131072 }; 
    juce::AudioBuffer<float> fifoBuffer;
    
    void run() override;
    
    std::unique_ptr<juce::AudioFormatWriter> writer;
    juce::File currentFile;
    juce::File recordingsFolder;
    
    juce::int64 samplesRecorded = 0;
    double sampleRate = 44100.0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordingManager)
};
