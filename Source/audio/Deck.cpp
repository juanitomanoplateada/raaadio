#include "Deck.h"
#include "../library/MusicLibrary.h"
#include "../database/Database.h"

// Analiza en otro hilo la pista que se cargó sin análisis; si es de la biblioteca, el
// resultado queda guardado para la próxima vez.
class Deck::AnalysisJob : public juce::Thread {
public:
    AnalysisJob(Deck& d, TrackInfo t, juce::File f)
        : juce::Thread("Deck Analysis"), deck(d), track(std::move(t)), file(std::move(f)) {}

    void run() override {
        auto shouldAbort = [this] { return threadShouldExit(); };
        TrackInfo result = track;
        bool ok;
        if (track.id != -1) {
            result = deck.library.ensureAnalysed(track, shouldAbort);
            ok = !MusicLibrary::needsAnalysis(result);
        } else {
            ok = deck.library.analyseFile(file, result, shouldAbort);
        }
        if (!ok || threadShouldExit()) return;

        const juce::ScopedLock sl(deck.analysisLock);
        deck.analysedTrack = result;
        deck.analysisReady = true;
    }

private:
    Deck& deck;
    const TrackInfo track;
    const juce::File file;
};

Deck::Deck(int id, MusicLibrary& lib, juce::AudioFormatManager& fm, juce::AudioThumbnailCache& cache)
    : deckId(id), library(lib), formatManager(fm), thumbnail(1000, fm, cache)
{
    transportSource = std::make_unique<juce::AudioTransportSource>();
    resamplerSource = std::make_unique<juce::ResamplingAudioSource>(transportSource.get(), false, 2);

    currentRatio = 1.0;

    readAheadThread.startThread(juce::Thread::Priority::high);
    startTimer(100);
}

Deck::~Deck() {
    stopTimer();
    cancelAnalysis();
    resamplerSource.reset();
    transportSource->setSource(nullptr);
    bufferedSource.reset();
    readerSource.reset();
    readAheadThread.stopThread(2000);
}

void Deck::prepareToPlay(int samplesPerBlockExpected, double sampleRate) {
    transportSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
    resamplerSource->prepareToPlay(samplesPerBlockExpected, sampleRate);
    currentSampleRate = sampleRate;
    prepared = true;

    currentRatio = 1.0;
}

void Deck::releaseResources() {
    prepared = false;
    transportSource->releaseResources();
    resamplerSource->releaseResources();
}

bool Deck::waitUntilBuffered(int numSamples, int timeoutMs) {
    if (bufferedSource == nullptr || !prepared) return false;
    const juce::AudioSourceChannelInfo next(nullptr, 0, numSamples);
    return bufferedSource->waitForNextAudioBlockReady(next, (juce::uint32) timeoutMs);
}

void Deck::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    double target = targetSpeed.load();
    
    if (std::abs(currentRatio - target) > 0.0001) {
        currentRatio += (target - currentRatio) * 0.2;
    } else {
        currentRatio = target;
    }
    
    if (resamplerSource) {
        resamplerSource->setResamplingRatio(currentRatio);
        resamplerSource->getNextAudioBlock(bufferToFill);
        
        if (skipRequested) {
            if (currentFadeLevel > 0.0f) {
                float numSamples = (float)bufferToFill.numSamples;
                float rate = (float)currentSampleRate; 
                if (rate <= 0) rate = 44100.0f; 
                
                float decrement = (numSamples / rate) * 0.5f; 
                
                currentFadeLevel -= decrement;
                if (currentFadeLevel < 0.0f) currentFadeLevel = 0.0f;
                
                bufferToFill.buffer->applyGainRamp(bufferToFill.startSample, bufferToFill.numSamples, 
                                                   currentFadeLevel + decrement, currentFadeLevel);
            } else {
                bufferToFill.clearActiveBufferRegion();
            }
        }
    } else {
        bufferToFill.clearActiveBufferRegion();
    }
}

bool Deck::loadTrack(const juce::File& file) {
    auto* reader = formatManager.createReaderFor(file);
    if (reader != nullptr) {
        std::unique_ptr<juce::AudioFormatReaderSource> newSource = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
        newSource->setLooping(loopingState);
        // El hilo de audio nunca espera al disco: lee de lo que este búfer trajo antes.
        auto newBuffered = std::make_unique<juce::BufferingAudioSource>(newSource.get(), readAheadThread, false,
                                                                        readAheadSamples, 2);

        cancelAnalysis();

        // Ganancia y puntos de entrada/salida salen del análisis guardado al importar. Si
        // falta (pista de una versión anterior que el análisis en segundo plano aún no
        // alcanzó, o un archivo fuera de la biblioteca), la pista suena ya con lo que se
        // sabe y se analiza en otro hilo: cargarla nunca congela la interfaz ni AutoDJ.
        TrackInfo info = library.getTrackByPath(file.getFullPathName());

        if (info.id == -1) {
            juce::Logger::writeToLog("Deck " + juce::String(deckId) + ": Track not in DB");
            info = TrackInfo {};
            info.id = -1;
            info.path = file.getFullPathName().toStdString();
            info.title = file.getFileNameWithoutExtension().toStdString();
            info.duration = (double) reader->lengthInSamples / reader->sampleRate;
            info.normalizationGain = 1.0;
        }

        currentTrack = info;
        normalizationGain = info.normalizationGain > 0.0 ? (float) info.normalizationGain : 1.0f;

        transportSource->setSource(newBuffered.get(), 0, nullptr, reader->sampleRate);
        transportSource->setGain(normalizationGain);

        const double length = transportSource->getLengthInSeconds();
        cueIn = juce::jlimit(0.0, length, info.cueIn);
        cueOut = (info.cueOut > cueIn && info.cueOut < length) ? info.cueOut : 0.0;
        transportSource->setPosition(cueIn);

        juce::Logger::writeToLog("Deck " + juce::String(deckId) + ": gain " + juce::String(normalizationGain, 3)
                                 + " (" + juce::String(info.loudnessLufs, 1) + " LUFS), cue "
                                 + juce::String(cueIn, 2) + "s - " + juce::String(getEndPosition(), 2) + "s");

        resamplerSource->setResamplingRatio(1.0);

        // El transporte ya usa la fuente nueva: se sueltan las viejas, primero la que lee
        // por adelantado (usa al lector).
        bufferedSource = std::move(newBuffered);
        readerSource = std::move(newSource);

        // Que el comienzo ya esté en memoria: AutoDJ la pone a sonar enseguida.
        waitUntilBuffered(4096, 1000);

        thumbnail.setSource(new juce::FileInputSource(file));

        playCounted = false;

        // Una cuña o identificación corta no hereda el bucle de la pista anterior.
        if (currentTrack.duration <= 30.0 && currentTrack.duration > 0.0)
            setLooping(false);

        if (MusicLibrary::needsAnalysis(info)) {
            juce::Logger::writeToLog("Deck " + juce::String(deckId) + ": no analysis yet, analysing in background");
            startAnalysis(info, file);
        }

        sendChangeMessage();
        return true;
    }
    return false;
}

void Deck::startAnalysis(const TrackInfo& track, const juce::File& file) {
    analysisReady = false;
    analysisJob = std::make_unique<AnalysisJob>(*this, track, file);
    analysisJob->startThread();
}

void Deck::cancelAnalysis() {
    if (analysisJob != nullptr) {
        analysisJob->stopThread(4000);  // el análisis revisa la señal de salida en cada bloque
        analysisJob.reset();
    }
    analysisReady = false;
}

bool Deck::applyPendingAnalysis() {
    if (!analysisReady.exchange(false)) return false;

    TrackInfo result;
    {
        const juce::ScopedLock sl(analysisLock);
        result = analysedTrack;
    }
    if (!isLoaded() || result.path != currentTrack.path) return false;

    const double length = transportSource->getLengthInSeconds();
    cueIn = juce::jlimit(0.0, length, result.cueIn);
    cueOut = (result.cueOut > cueIn && result.cueOut < length) ? result.cueOut : 0.0;

    // El silencio inicial se salta si todavía no pasó. La ganancia solo cambia si la pista
    // no está sonando: un salto de volumen a mitad de canción se oiría.
    if (transportSource->getCurrentPosition() < cueIn)
        transportSource->setPosition(cueIn);
    if (!isPlaying()) {
        normalizationGain = result.normalizationGain > 0.0 ? (float) result.normalizationGain : 1.0f;
        transportSource->setGain(normalizationGain);
    }

    currentTrack = result;

    juce::Logger::writeToLog("Deck " + juce::String(deckId) + ": analysis applied, cue "
                             + juce::String(cueIn, 2) + "s - " + juce::String(getEndPosition(), 2) + "s");
    sendChangeMessage();
    return true;
}

void Deck::unload() {
    cancelAnalysis();
    stop();
    transportSource->setSource(nullptr);
    bufferedSource.reset();
    readerSource.reset();
    currentTrack = TrackInfo();
    thumbnail.clear();
    
    skipRequested = false;
    currentFadeLevel = 1.0f;
    cueIn = 0.0;
    cueOut = 0.0;

    sendChangeMessage();
    if (onStateChange) onStateChange();
}

void Deck::startSkip() {
    if (isLoaded() && isPlaying() && !skipRequested) {
        juce::Logger::writeToLog("Deck " + juce::String(deckId) + ": Skip Requested (Fade Out)");
        skipRequested = true;
        currentFadeLevel = 1.0f;
    }
}

void Deck::play() {
    waitUntilBuffered(2048, 250);  // tras un salto (detener, buscar) el búfer se rellena
    lastPlayTime = juce::Time::getCurrentTime();
    transportSource->start();
    logAiringIfNeeded();

    if (onTrackStart) onTrackStart();
    if (onStateChange) onStateChange();
}

// Una pista cuenta como emitida (contador de reproducciones e historial) la primera vez
// que suena por la salida principal. Preescucharla en CUE no la registra.
void Deck::logAiringIfNeeded() {
    if (playCounted || !isLoaded() || !isPlaying() || !routeToMain) return;

    playCounted = true;
    if (currentTrack.id != -1) {
        library.incrementPlayCount(currentTrack.id);
        juce::Logger::writeToLog("Deck " + juce::String(deckId) + ": Play Count Incremented for ID " + juce::String(currentTrack.id));
    }
    if (onTrackLog) onTrackLog(currentTrack);
}

void Deck::stop() {
    transportSource->stop();
    transportSource->setPosition(cueIn);
    if (onStateChange) onStateChange();
}

void Deck::pause() {
    transportSource->stop();
    if (onStateChange) onStateChange();
}

void Deck::setGain(float gain) {
    transportSource->setGain(gain);
}

bool Deck::isPlaying() const {
    return transportSource != nullptr && transportSource->isPlaying();
}

void Deck::setLooping(bool shouldLoop) {
    loopingState = shouldLoop;
    if (readerSource) {
        readerSource->setLooping(shouldLoop);
    }
}

bool Deck::hasFinished() const {
    if (skipRequested && currentFadeLevel <= 0.001f) {
        return true; 
    }

    if (loopingState) return false;
    
    // Sin pista cargada no hay nada que termine (y hasStreamFinished de JUCE no lo revisa).
    if (transportSource == nullptr || !isLoaded()) return false;
    
    if (transportSource->hasStreamFinished()) return true;

    double len = transportSource->getLengthInSeconds();
    double pos = transportSource->getCurrentPosition();

    // Al aire, la pista termina en su punto de salida: lo que sigue es silencio (o una
    // pista oculta tras un silencio largo).
    if (cueOut > 0.0 && isPlaying() && pos >= cueOut) {
        return true;
    }

    if (len > 0.5 && pos >= (len - 0.2)) {
        return true;
    }

    return false;
}

bool Deck::isReadyToMix() const {
    if (loopingState) return false;

    if (skipRequested) return true;

    // La siguiente pista entra mixLeadSeconds antes del punto de salida.
    if (transportSource && getEndPosition() - cueIn > minMixSeconds) {
        if (getRemainingSeconds() <= mixLeadSeconds) {
            return true;
        }
    }

    return false;
}

double Deck::getEndPosition() const {
    return cueOut > 0.0 ? cueOut : transportSource->getLengthInSeconds();
}

double Deck::getRemainingSeconds() const {
    return juce::jmax(0.0, getEndPosition() - transportSource->getCurrentPosition());
}

bool Deck::isLoaded() const {
    return readerSource != nullptr;
}

double Deck::getLengthInSeconds() const {
    return transportSource->getLengthInSeconds();
}

double Deck::getCurrentPosition() const {
    return transportSource->getCurrentPosition();
}

void Deck::setPosition(double pos) {
    transportSource->setPosition(pos);
}

void Deck::setSpeed(double ratio) {
    targetSpeed.store(ratio);
}

double Deck::getSpeed() const {
    return targetSpeed.load();
}

void Deck::resetSpeed() {
    setSpeed(1.0);
}

void Deck::setRouting(bool toMain, bool toCue) {
    const bool mainChanged = routeToMain.exchange(toMain) != toMain;
    routeToCue = toCue;
    logAiringIfNeeded();  // pasar de preescucha al aire registra la emisión

    // Cambió lo que oyen los oyentes (el título del stream depende de esto).
    if (mainChanged && onStateChange) onStateChange();
}

void Deck::timerCallback() {
    applyPendingAnalysis();

    if (isPlaying()) {
        sendChangeMessage(); 
    }
}

MusicLibrary& Deck::getLibrary() {
    return library;
}
