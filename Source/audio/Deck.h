#pragma once
#include <JuceHeader.h>
#include "../database/Database.h"

class MusicLibrary; 

class Deck : public juce::AudioSource, 
             public juce::ChangeBroadcaster,
             public juce::Timer
{
public:
    Deck(int id, MusicLibrary& library, juce::AudioFormatManager& formatManager, juce::AudioThumbnailCache& cache);
    ~Deck() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;

    // AutoDJ lanza la siguiente pista en el punto de mezcla de la que suena (mixPointFor) y
    // lo que queda de esta baja con un fundido de hasta mixFadeSeconds.
    static constexpr double minMixSeconds = 10.0;
    static constexpr double mixFadeSeconds = 3.0;
    static constexpr double skipFadeSeconds = 2.0;

    // Dónde entra la siguiente pista. Identificaciones, cuñas y separadores se oyen enteros,
    // igual que lo corto (hasta minMixSeconds): la siguiente entra en su punto de salida. La
    // música, en su punto de mezcla, cuando ya solo queda su final que se desvanece; sin ese
    // análisis, también en el punto de salida.
    static double mixPointFor(const TrackInfo& track, double cueIn, double endPosition);

    bool loadTrack(const juce::File& file);  // false si el archivo no se puede leer
    void unload();
    void play();
    void stop();
    void startSkip();     // fundido de skipFadeSeconds; AutoDJ lanza la siguiente enseguida
    void startMixFade();  // ya entró la siguiente: lo que queda hasta el punto de salida se desvanece
    bool isFadingOut() const { return fadingOut; }
    void pause();
    void setGain(float gain);
    
    bool isPlaying() const;
    bool hasFinished() const; 
    bool isReadyToMix() const; 
    bool isLoaded() const; 
    double getLengthInSeconds() const;
    double getCurrentPosition() const;
    void setPosition(double pos); 
    void setSpeed(double ratio); 
    void setLooping(bool shouldLoop); 
    bool isLooping() const { return loopingState; } 
    double getSpeed() const;
    float getNormalizationGain() const { return normalizationGain; }
    void resetSpeed();

    // Puntos de entrada y salida de la pista cargada (analizados al importar): la pista
    // arranca en el de entrada y, al aire, termina en el de salida.
    double getCueIn() const { return cueIn; }
    double getEndPosition() const;
    double getRemainingSeconds() const;  // hasta el punto de salida
    double getMixPoint() const { return mixPoint; }

    // Una pista que llega sin análisis (de una versión anterior, o de fuera de la
    // biblioteca) suena de inmediato y se analiza en otro hilo. Esto aplica el resultado:
    // lo llama el temporizador del deck (es público para las pruebas). Devuelve true si
    // había un análisis terminado para la pista cargada.
    bool applyPendingAnalysis();

    // El archivo se lee en otro hilo, varios segundos por delante: un disco lento (o un
    // USB que se demora) no deja al aire sin sonido. Esto espera, hasta timeoutMs, a que
    // estén en memoria las próximas numSamples muestras. False si no llegaron a tiempo.
    bool waitUntilBuffered(int numSamples, int timeoutMs);
    static constexpr int readAheadSamples = 1 << 17;  // ~3 s a 44.1 kHz

    juce::AudioThumbnail& getThumbnail() { return thumbnail; }
    TrackInfo getCurrentTrack() const { return currentTrack; }
    int getId() const { return deckId; }
    MusicLibrary& getLibrary(); 

    std::function<void()> onTrackStart; 
    std::function<void()> onStateChange; 
    std::function<void(const TrackInfo&)> onTrackLog; 

    juce::Time getLastPlayTime() const { return lastPlayTime; }

    void setRouting(bool toMain, bool toCue);
    bool isRoutedToMain() const { return routeToMain; }
    bool isRoutedToCue() const { return routeToCue; }

private:
    int deckId;
    MusicLibrary& library;
    juce::AudioFormatManager& formatManager;

    juce::TimeSliceThread readAheadThread { "Deck read-ahead" };
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
    std::unique_ptr<juce::BufferingAudioSource> bufferedSource;  // lee readerSource por adelantado
    std::unique_ptr<juce::AudioTransportSource> transportSource;
    std::unique_ptr<juce::ResamplingAudioSource> resamplerSource;
    
    float normalizationGain = 1.0f;
    std::atomic<double> targetSpeed { 1.0 };
    std::atomic<bool> loopingState { false }; 
    std::atomic<bool> fadingOut { false };
    std::atomic<float> fadeSeconds { (float) skipFadeSeconds };
    std::atomic<float> fadeProgress { 0.0f };  // 0 = empieza, 1 = terminó (silencio)
    void startFadeOut(double seconds);
    double currentSampleRate = 44100.0;
    std::atomic<bool> prepared { false };
    
    double currentRatio = 1.0;
    
    juce::AudioThumbnail thumbnail;
    TrackInfo currentTrack;
    bool playCounted = false;
    juce::Time lastPlayTime;    
    std::atomic<bool> routeToMain { true };
    std::atomic<bool> routeToCue { false };

    double cueIn = 0.0;
    double cueOut = 0.0;  // 0 = hasta el final del archivo
    double mixPoint = 0.0;

    class AnalysisJob;
    std::unique_ptr<AnalysisJob> analysisJob;
    juce::CriticalSection analysisLock;
    TrackInfo analysedTrack;  // lo escribe AnalysisJob, lo lee applyPendingAnalysis()
    std::atomic<bool> analysisReady { false };
    void startAnalysis(const TrackInfo& track, const juce::File& file);
    void cancelAnalysis();

    void timerCallback() override;
    void logAiringIfNeeded();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Deck)
};
