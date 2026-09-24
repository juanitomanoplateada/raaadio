#pragma once
#include <JuceHeader.h>
#include "OutputTap.h"

// Registro de lo emitido: graba la salida al aire sin parar, en archivos de una hora
// (Registro/AAAA/MM/DD/AAAA-MM-DD_HH-MM.ogg), y borra los que superan los días de
// retención. Ogg Vorbis mono de baja tasa: unos 20 MB por hora, suficiente para
// comprobar qué salió al aire (un WAV ocuparía más de 600 MB por hora).
// Es independiente de la grabación manual (botón REC).
class AirLogRecorder : public OutputTap, private juce::Thread {
public:
    explicit AirLogRecorder(juce::File folder = {});
    ~AirLogRecorder() override;

    void setFolder(const juce::File& folder);
    void setRetentionDays(int days) { retentionDays = days; }
    void setEnabled(bool shouldRecord);
    bool isEnabled() const { return enabled.load(); }
    juce::File getCurrentFile() const;

    // Para las pruebas: reloj que decide cuándo empieza cada hora.
    void setClock(std::function<juce::Time()> clockFunction) { clock = std::move(clockFunction); }

    // OutputTap (hilo de audio)
    void prepare(double sampleRate) override { preparedRate = sampleRate; }
    void pushBlock(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples) override;

    // Archivo de un tramo que empieza en start.
    static juce::File fileForSegment(const juce::File& folder, juce::Time start);
    // Borra los registros modificados hace más de days días (y las carpetas que quedan
    // vacías). Devuelve cuántos archivos borró.
    static int deleteOlderThan(const juce::File& folder, int days, juce::Time now);

private:
    void run() override;
    void writeAvailable(int maxSamples);
    void openSegment(juce::Time start);

    juce::File folder;
    std::atomic<int> retentionDays { 30 };
    std::atomic<bool> enabled { false };
    std::atomic<double> preparedRate { 44100.0 };
    std::function<juce::Time()> clock = [] { return juce::Time::getCurrentTime(); };

    juce::AbstractFifo fifo { 262144 };
    juce::AudioBuffer<float> fifoBuffer { 2, 262144 };
    juce::AudioBuffer<float> monoBuffer { 1, 262144 };

    std::unique_ptr<juce::AudioFormatWriter> writer;
    double writerRate = 0.0;
    juce::Time segmentHour;
    juce::uint32 lastOpenAttemptMs = 0;  // si no se pudo crear el archivo, se reintenta cada 10 s
    mutable juce::CriticalSection fileLock;
    juce::File currentFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AirLogRecorder)
};
