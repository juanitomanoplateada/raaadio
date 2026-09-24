#include "AirLogRecorder.h"

AirLogRecorder::AirLogRecorder(juce::File f) : juce::Thread("Air Log"), folder(std::move(f)) {}

AirLogRecorder::~AirLogRecorder() {
    setEnabled(false);
}

void AirLogRecorder::setFolder(const juce::File& f) {
    const juce::ScopedLock sl(fileLock);
    folder = f;
}

juce::File AirLogRecorder::getCurrentFile() const {
    const juce::ScopedLock sl(fileLock);
    return currentFile;
}

void AirLogRecorder::setEnabled(bool shouldRecord) {
    if (shouldRecord == isThreadRunning()) {
        enabled = shouldRecord;
        return;
    }
    if (shouldRecord) {
        fifo.reset();
        lastOpenAttemptMs = 0;
        enabled = true;
        startThread();
    } else {
        enabled = false;
        stopThread(5000);  // escribe lo que quedaba y cierra el archivo
    }
}

void AirLogRecorder::pushBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) {
    if (!enabled.load()) return;

    int start1, size1, start2, size2;
    fifo.prepareToWrite(numSamples, start1, size1, start2, size2);  // si el disco se atrasa, se pierde lo que no cabe
    const int channels = juce::jmin(2, buffer.getNumChannels());
    for (int ch = 0; ch < 2; ++ch) {
        const int source = juce::jmin(ch, channels - 1);
        if (size1 > 0) fifoBuffer.copyFrom(ch, start1, buffer, source, startSample, size1);
        if (size2 > 0) fifoBuffer.copyFrom(ch, start2, buffer, source, startSample + size1, size2);
    }
    fifo.finishedWrite(size1 + size2);
}

void AirLogRecorder::run() {
    while (!threadShouldExit()) {
        if (fifo.getNumReady() < 2048) {
            wait(20);
            continue;
        }
        writeAvailable(fifo.getNumReady());
    }
    writeAvailable(fifo.getNumReady());

    writer.reset();
    const juce::ScopedLock sl(fileLock);
    currentFile = juce::File();
}

void AirLogRecorder::writeAvailable(int maxSamples) {
    if (maxSamples <= 0) return;

    // Un archivo por hora: al cambiar la hora (o la frecuencia de muestreo) se cierra el
    // actual y empieza otro.
    const auto now = clock();
    const juce::Time hour(now.getYear(), now.getMonth(), now.getDayOfMonth(), now.getHours(), 0, 0, 0, true);
    const double rate = preparedRate.load();
    const bool rotating = writer != nullptr && (hour != segmentHour || rate != writerRate);
    const auto nowMs = juce::Time::getMillisecondCounter();
    const bool retry = writer == nullptr && (lastOpenAttemptMs == 0 || nowMs - lastOpenAttemptMs > 10000);
    if (rotating || retry) {
        lastOpenAttemptMs = nowMs;
        writer.reset();
        segmentHour = hour;
        openSegment(rotating ? hour : now);

        juce::File logFolder;
        {
            const juce::ScopedLock sl(fileLock);
            logFolder = folder;
        }
        const int deleted = deleteOlderThan(logFolder, retentionDays.load(), now);
        if (deleted > 0)
            juce::Logger::writeToLog("AirLog: deleted " + juce::String(deleted) + " files older than "
                                     + juce::String(retentionDays.load()) + " days");
    }

    int start1, size1, start2, size2;
    fifo.prepareToRead(maxSamples, start1, size1, start2, size2);
    for (auto [start, size] : { std::make_pair(start1, size1), std::make_pair(start2, size2) }) {
        if (size <= 0 || writer == nullptr) continue;
        // Mono: (L + R) / 2.
        monoBuffer.copyFrom(0, 0, fifoBuffer, 0, start, size);
        monoBuffer.addFrom(0, 0, fifoBuffer, 1, start, size);
        monoBuffer.applyGain(0, 0, size, 0.5f);
        writer->writeFromAudioSampleBuffer(monoBuffer, 0, size);
    }
    fifo.finishedRead(size1 + size2);
}

void AirLogRecorder::openSegment(juce::Time start) {
    juce::File file;
    {
        const juce::ScopedLock sl(fileLock);
        file = fileForSegment(folder, start);
    }
    file.getParentDirectory().createDirectory();
    if (file.exists())  // se reinició la emisora en el mismo minuto
        file = file.getSiblingFile(file.getFileNameWithoutExtension() + "-" + start.formatted("%S") + ".ogg");

    juce::StringPairArray metadata;
    metadata.set(juce::OggVorbisAudioFormat::encoderName, "RAAADIO");
    metadata.set(juce::OggVorbisAudioFormat::id3title, "Registro " + start.formatted("%Y-%m-%d %H:%M"));

    juce::OggVorbisAudioFormat ogg;
    auto stream = file.createOutputStream();
    if (stream != nullptr)
        writer.reset(ogg.createWriterFor(stream.get(), preparedRate.load(), 1, 16, metadata, 0));
    if (writer == nullptr) {
        juce::Logger::writeToLog("AirLog: could not create " + file.getFullPathName());
        return;
    }
    stream.release();
    writerRate = preparedRate.load();

    const juce::ScopedLock sl(fileLock);
    currentFile = file;
}

juce::File AirLogRecorder::fileForSegment(const juce::File& folder, juce::Time start) {
    return folder.getChildFile(start.formatted("%Y")).getChildFile(start.formatted("%m")).getChildFile(start.formatted("%d"))
                 .getChildFile(start.formatted("%Y-%m-%d_%H-%M") + ".ogg");
}

int AirLogRecorder::deleteOlderThan(const juce::File& folder, int days, juce::Time now) {
    if (!folder.isDirectory() || days <= 0) return 0;

    const auto cutoff = now - juce::RelativeTime::days(days);
    int deleted = 0;
    for (const auto& file : folder.findChildFiles(juce::File::findFiles, true, "*.ogg"))
        if (file.getLastModificationTime() < cutoff && file.deleteFile()) ++deleted;

    // Carpetas de días (y meses, años) que quedaron vacías, de la más profunda a la más alta.
    auto dirs = folder.findChildFiles(juce::File::findDirectories, true);
    std::sort(dirs.begin(), dirs.end(), [](const juce::File& a, const juce::File& b) {
        return a.getFullPathName().length() > b.getFullPathName().length();
    });
    for (const auto& dir : dirs)
        if (dir.getNumberOfChildFiles(juce::File::findFilesAndDirectories) == 0) dir.deleteFile();

    return deleted;
}
