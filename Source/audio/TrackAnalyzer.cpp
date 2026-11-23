#include "TrackAnalyzer.h"
#include <ebur128.h>

namespace TrackAnalyzer {

double gainFor(double loudnessLufs, double truePeakDb) {
    if (loudnessLufs <= -69.9) return 1.0;  // silencio o casi: no hay nada que igualar

    double db = targetLufs - loudnessLufs;

    // Solo se sube lo que el techo de pico permite; una pista que no puede subir se deja
    // como está en lugar de bajarla por sus picos.
    if (db > 0.0)
        db = juce::jlimit(0.0, maxBoostDb, std::min(db, peakCeilingDb - truePeakDb));

    return juce::Decibels::decibelsToGain(db, -200.0);
}

void findCuePoints(const std::vector<float>& levels, double windowSeconds,
                   double lengthSeconds, double& cueIn, double& cueOut) {
    cueIn = 0.0;
    cueOut = lengthSeconds;
    if (levels.empty() || windowSeconds <= 0.0) return;

    const float trim = juce::Decibels::decibelsToGain((float) trimThresholdDb);
    const float gap = juce::Decibels::decibelsToGain((float) gapThresholdDb);
    const int count = (int) levels.size();

    int first = -1, last = -1;
    for (int i = 0; i < count; ++i) {
        if (levels[(size_t) i] >= trim) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0) return;  // todo es silencio: no se recorta nada

    // Hueco largo de silencio digital después de la parte protegida: la pista termina
    // con el último sonido anterior al hueco.
    const int protectedEnd = first + (int) std::ceil(protectedSeconds / windowSeconds);
    const int minGapWindows = (int) std::ceil(minGapSeconds / windowSeconds);
    int run = 0;
    for (int i = protectedEnd; i <= last; ++i) {
        if (levels[(size_t) i] < gap) {
            ++run;
            continue;
        }
        if (run >= minGapWindows) {
            int lastBeforeGap = i - run - 1;
            while (lastBeforeGap > first && levels[(size_t) lastBeforeGap] < trim) --lastBeforeGap;
            last = lastBeforeGap;
            break;
        }
        run = 0;
    }

    // Una ventana de margen a cada lado para no morder el ataque ni la última cola.
    cueIn = juce::jmax(0.0, (first - 1) * windowSeconds);
    cueOut = juce::jmin(lengthSeconds, (last + 2) * windowSeconds);

    if (cueOut <= cueIn) {
        cueIn = 0.0;
        cueOut = lengthSeconds;
    }
}

double findMixPoint(const std::vector<float>& levels, double windowSeconds,
                    double loudnessLufs, double cueIn, double cueOut) {
    if (levels.empty() || windowSeconds <= 0.0 || loudnessLufs <= -69.9 || cueOut <= cueIn) return cueOut;

    const double threshold = juce::Decibels::decibelsToGain(loudnessLufs - mixDropDb);
    const double thresholdPower = threshold * threshold;
    const int span = juce::jmax(1, (int) std::lround(mixWindowSeconds / windowSeconds));

    // Del punto de salida hacia atrás. El promedio de cada ventana incluye las anteriores:
    // donde baja del umbral, el sonido que lo sostenía ya terminó.
    const double earliest = juce::jmax(cueIn, cueOut - maxMixLeadSeconds);
    const int last = juce::jmin((int) levels.size(), (int) std::ceil(cueOut / windowSeconds)) - 1;
    const int first = juce::jmax(0, (int) std::floor(earliest / windowSeconds));

    for (int i = last; i >= first; --i) {
        const int from = juce::jmax(0, i - span + 1);
        double power = 0.0;
        for (int j = from; j <= i; ++j)
            power += (double) levels[(size_t) j] * levels[(size_t) j];
        if (power / (i - from + 1) >= thresholdPower)
            return juce::jlimit(cueIn, cueOut, (i + 1) * windowSeconds);
    }

    // Todo lo revisado ya es final que se desvanece (una salida larga y suave): la siguiente
    // entra en el tope. Si se revisó la pista entera, algo no cuadra y no se adelanta nada.
    return earliest > cueIn ? earliest : cueOut;
}

TrackAnalysis analyse(juce::AudioFormatReader& reader, const std::function<bool()>& shouldAbort) {
    TrackAnalysis result;
    const double sampleRate = reader.sampleRate;
    const juce::int64 totalSamples = reader.lengthInSamples;
    if (sampleRate < 8000.0 || totalSamples <= 0) return result;

    auto destroy = [](ebur128_state* s) { ebur128_destroy(&s); };
    std::unique_ptr<ebur128_state, decltype(destroy)> meter(
        ebur128_init(2, (unsigned long) std::lround(sampleRate), EBUR128_MODE_I), destroy);
    if (meter == nullptr) return result;

    constexpr int blockSize = 16384;
    juce::AudioBuffer<float> buffer(2, blockSize);
    std::vector<float> interleaved((size_t) blockSize * 2);

    // Pico real: sobremuestreo x4 (BS.1770 anexo 2) con los filtros IIR de media banda de
    // juce::dsp. Con MP3 reales da hasta 0.2 dB más que el FIR de libebur128 (del lado
    // seguro para el techo) y el análisis completo tarda la mitad.
    juce::dsp::Oversampling<float> oversampling(2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
    oversampling.initProcessing((size_t) blockSize);
    float truePeak = 0.0f;

    const int window = juce::jmax(1, juce::roundToInt(sampleRate * 0.02));
    std::vector<float> levels;
    levels.reserve((size_t) (totalSamples / window) + 1);
    double sumLeft = 0.0, sumRight = 0.0;
    int inWindow = 0;

    for (juce::int64 position = 0; position < totalSamples; position += blockSize) {
        if (shouldAbort && shouldAbort()) return {};

        const int numSamples = (int) std::min<juce::int64>(blockSize, totalSamples - position);
        reader.read(&buffer, 0, numSamples, position, true, true);

        const float* left = buffer.getReadPointer(0);
        const float* right = buffer.getReadPointer(1);
        for (int i = 0; i < numSamples; ++i) {
            interleaved[2 * (size_t) i] = left[i];
            interleaved[2 * (size_t) i + 1] = right[i];

            sumLeft += (double) left[i] * left[i];
            sumRight += (double) right[i] * right[i];
            if (++inWindow == window) {
                levels.push_back((float) std::sqrt(std::max(sumLeft, sumRight) / window));
                sumLeft = sumRight = 0.0;
                inWindow = 0;
            }
        }
        ebur128_add_frames_float(meter.get(), interleaved.data(), (size_t) numSamples);

        const auto upsampled = oversampling.processSamplesUp(
            juce::dsp::AudioBlock<const float>(buffer).getSubBlock(0, (size_t) numSamples));
        const auto range = upsampled.findMinAndMax();
        truePeak = juce::jmax(truePeak, -range.getStart(), range.getEnd());
    }
    if (inWindow > 0)
        levels.push_back((float) std::sqrt(std::max(sumLeft, sumRight) / inWindow));

    double loudness = -70.0;
    if (ebur128_loudness_global(meter.get(), &loudness) != EBUR128_SUCCESS || !std::isfinite(loudness))
        loudness = -70.0;
    result.loudnessLufs = juce::jmax(-70.0, loudness);

    result.truePeakDb = juce::Decibels::gainToDecibels((double) truePeak, -70.0);

    result.lengthSeconds = (double) totalSamples / sampleRate;
    findCuePoints(levels, window / sampleRate, result.lengthSeconds, result.cueIn, result.cueOut);
    result.mixPoint = findMixPoint(levels, window / sampleRate, result.loudnessLufs, result.cueIn, result.cueOut);
    result.valid = true;
    return result;
}

}
