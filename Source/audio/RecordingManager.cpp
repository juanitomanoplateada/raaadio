#include "RecordingManager.h"
#include "../helpers/PersistenceManager.h"

RecordingManager::RecordingManager() : juce::Thread("Recording Thread") {
    fifoBuffer.setSize(2, 131072); 
    
    recordingsFolder = PersistenceManager::getRecordingsFolder();
    if (!recordingsFolder.exists()) {
        recordingsFolder.createDirectory();
    }
}

RecordingManager::~RecordingManager() {
    stopThread(5000);
}

void RecordingManager::initialise(juce::AudioFormatManager& fmtMgr) {
    formatManager = &fmtMgr;
}

void RecordingManager::setRecordingsFolder(const juce::File& folder) {
    recordingsFolder = folder;
    if (!recordingsFolder.exists()) {
        recordingsFolder.createDirectory();
    }
}

void RecordingManager::openRecordingsFolder() {
    recordingsFolder.revealToUser();
}

juce::String RecordingManager::startRecording(bool deckA_Air, bool deckB_Air) {
    if (currentState == Recording) return "Already recording";
    
    if (currentState == Paused) {
        resumeRecording();
        return {};
    }
    
    if (!recordingsFolder.exists()) {
        auto res = recordingsFolder.createDirectory();
        if (res.failed()) return "Could not create folder: " + res.getErrorMessage();
    }
    
    auto now = juce::Time::getCurrentTime();
    
    auto year = now.formatted("%Y");
    auto month = now.formatted("%m");
    auto day = now.formatted("%d");
    
    auto targetDir = recordingsFolder.getChildFile(year).getChildFile(month).getChildFile(day);
    
    if (!targetDir.exists()) {
        auto res = targetDir.createDirectory();
        if (res.failed()) return "Could not create date folders: " + res.getErrorMessage();
    }
    
    if (!formatManager) return "Format Manager not initialized";

    auto* wavFmt = formatManager->findFormatForFileExtension("wav");
    if (!wavFmt) return "WAV Format not found";

    juce::String filename = now.formatted("%Y_%m_%d_%H_%M_%S") + ".wav";
    currentFile = targetDir.getChildFile(filename);

    auto stream = currentFile.createOutputStream();
    if (!stream) return "Could not create file stream: " + currentFile.getFullPathName();

    // createWriterFor() solo toma posesión del stream si tiene éxito.
    writer.reset(wavFmt->createWriterFor(stream.get(), sampleRate, 2, 16, {}, 0));
    if (!writer) return "Failed to initialize WAV Writer";
    stream.release();

    // El hilo de escritura arranca cuando el writer ya existe.
    samplesRecorded = 0;
    currentState = Recording;
    startThread();
    sendChangeMessage();
    return {};
}

void RecordingManager::pauseRecording() {
    if (currentState == Recording) {
        currentState = Paused;
        sendChangeMessage();
    }
}

void RecordingManager::resumeRecording() {
    if (currentState == Paused) {
        currentState = Recording;
        sendChangeMessage();
    }
}

void RecordingManager::stopRecording() {
    if (currentState == Idle) return;
    
    currentState = Saving;
    sendChangeMessage();
    
    stopThread(5000); 
    
    writer.reset(); 
    
    samplesRecorded = 0; 
    currentState = Idle;
    sendChangeMessage();
}

void RecordingManager::processBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) {
    if (currentState != Recording) return;
    
    int start1, size1, start2, size2;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);
    
    if (size1 > 0) {
        for (int ch=0; ch<buffer.getNumChannels(); ++ch) 
            fifoBuffer.copyFrom(ch, start1, buffer, ch, startSample, size1);
    }
    if (size2 > 0) {
        for (int ch=0; ch<buffer.getNumChannels(); ++ch)
            fifoBuffer.copyFrom(ch, start2, buffer, ch, startSample + size1, size2);
    }
    
    fifo.finishedWrite(size1 + size2);
    samplesRecorded += numSamples;
}

void RecordingManager::run() {
    while (!threadShouldExit()) {
        int ready = fifo.getNumReady();
        if (ready < 512) { 
            wait(10);
            continue;
        }
        
        int start1, size1, start2, size2;
        fifo.prepareToRead(ready, start1, size1, start2, size2);
        
        if (writer) {
            if (size1 > 0) {
                writer->writeFromAudioSampleBuffer(fifoBuffer, start1, size1);
            }
            if (size2 > 0) {
                writer->writeFromAudioSampleBuffer(fifoBuffer, start2, size2);
            }
        }
        
        fifo.finishedRead(size1 + size2);
    }
    
    int ready = fifo.getNumReady();
    if (ready > 0 && writer) {
         int s1, sz1, s2, sz2;
         fifo.prepareToRead(ready, s1, sz1, s2, sz2);
         if (sz1 > 0) writer->writeFromAudioSampleBuffer(fifoBuffer, s1, sz1);
         if (sz2 > 0) writer->writeFromAudioSampleBuffer(fifoBuffer, s2, sz2);
         fifo.finishedRead(sz1+sz2);
    }
}

juce::String RecordingManager::getDurationString() const {
    double seconds = (double)samplesRecorded / sampleRate;
    
    int hrs = (int)(seconds / 3600);
    int mins = (int)((seconds - (hrs * 3600)) / 60);
    int secs = (int)((seconds - (hrs * 3600) - (mins * 60)));
    
    return juce::String::formatted("%02d:%02d:%02d", hrs, mins, secs);
}

juce::File RecordingManager::getLastRecording() const {
    return currentFile;
}
