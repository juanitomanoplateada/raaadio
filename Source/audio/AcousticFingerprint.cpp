#include "AcousticFingerprint.h"
#include <array>

namespace AcousticFingerprint {

namespace {
    constexpr double targetRate = 5512.5;   // lo que importa está por debajo de 2 kHz
    constexpr int fftOrder = 11;
    constexpr int frameSize = 1 << fftOrder; // 0,37 s
    constexpr int hop = 128;                 // 23 ms
    constexpr int bands = 33;
    constexpr double lowHz = 300.0, highHz = 2000.0;
    constexpr double windowSeconds = 10.0;
    constexpr int maxOffset = 44;            // ± 1 s en subhuellas

    // Mono, filtrado por debajo de 2,4 kHz (lo que los formatos con pérdida conservan igual)
    // y llevado a 5512,5 Hz.
    std::vector<float> readMono(juce::AudioFormatReader& reader, juce::int64 start, juce::int64 length,
                                const std::function<bool()>& shouldAbort) {
        std::vector<float> out;
        const double sourceRate = reader.sampleRate;
        if (sourceRate <= 0.0 || length <= 0) return out;

        juce::IIRFilter lowPass[2];
        for (auto& f : lowPass) f.setCoefficients(juce::IIRCoefficients::makeLowPass(sourceRate, 2400.0, 0.7071));
        juce::LagrangeInterpolator resampler;
        const double ratio = sourceRate / targetRate;
        out.reserve((size_t) ((double) length / ratio) + 16);

        constexpr int block = 16384;
        const int channels = (int) juce::jmax(1u, juce::jmin(2u, reader.numChannels));
        juce::AudioBuffer<float> buffer(channels, block);
        std::vector<float> mono((size_t) block), resampled;
        std::vector<float> pending;  // lo filtrado que el remuestreo todavía no usó

        for (juce::int64 pos = 0; pos < length; pos += block) {
            if (shouldAbort && shouldAbort()) return {};
            const int n = (int) juce::jmin((juce::int64) block, length - pos);
            buffer.clear();
            reader.read(&buffer, 0, n, start + pos, true, channels > 1);
            for (int i = 0; i < n; ++i) {
                float v = buffer.getSample(0, i);
                if (channels > 1) v = 0.5f * (v + buffer.getSample(1, i));
                mono[(size_t) i] = v;
            }
            for (auto& f : lowPass) f.processSamples(mono.data(), n);
            pending.insert(pending.end(), mono.begin(), mono.begin() + n);

            // Sin cortes entre bloques: lo que no se usó queda para el siguiente (un corte
            // sería un clic cada bloque, y un tono parecería música).
            const int produced = (int) ((double) ((int) pending.size() - 8) / ratio);
            if (produced <= 0) continue;
            resampled.resize((size_t) produced);
            const int used = resampler.process(ratio, pending.data(), resampled.data(), produced);
            pending.erase(pending.begin(), pending.begin() + juce::jmin(used, (int) pending.size()));
            out.insert(out.end(), resampled.begin(), resampled.end());
        }
        return out;
    }

    int countBits(uint32_t v) { return juce::countNumberOfBits(v); }
}

std::vector<uint32_t> compute(juce::AudioFormatReader& reader, double cueIn, double airDuration,
                              const std::function<bool()>& shouldAbort) {
    const double length = (double) reader.lengthInSamples / juce::jmax(1.0, reader.sampleRate);
    if (length <= 0.0) return {};

    // Diez segundos a partir del punto de entrada más 30 s (el 30 % si la canción es corta):
    // la misma música en cualquier formato, lejos de intros y finales que cambian entre versiones.
    cueIn = juce::jlimit(0.0, length, cueIn);
    if (airDuration <= 0.0) airDuration = length - cueIn;
    double start = cueIn + juce::jmin(30.0, 0.3 * airDuration);
    const double frameSeconds = frameSize / targetRate;
    double seconds = windowSeconds + frameSeconds;
    if (start + seconds > length) start = juce::jmax(0.0, length - seconds);
    seconds = juce::jmin(seconds, length - start);

    const auto samples = readMono(reader, (juce::int64) (start * reader.sampleRate),
                                  (juce::int64) (seconds * reader.sampleRate), shouldAbort);
    if ((int) samples.size() < frameSize + hop) return {};

    // Bordes de las bandas, repartidas en escala logarítmica.
    int edges[bands + 1];
    for (int b = 0; b <= bands; ++b) {
        const double hz = lowHz * std::pow(highHz / lowHz, (double) b / bands);
        edges[b] = juce::jlimit(1, frameSize / 2 - 1, (int) std::lround(hz * frameSize / targetRate));
    }

    juce::dsp::FFT fft(fftOrder);
    juce::dsp::WindowingFunction<float> window((size_t) frameSize, juce::dsp::WindowingFunction<float>::hann, false);
    std::vector<float> data((size_t) frameSize * 2);

    // Energía de cada banda en cada instante.
    const int frames = ((int) samples.size() - frameSize) / hop + 1;
    std::vector<std::array<double, bands>> energy((size_t) frames);
    for (int f = 0; f < frames; ++f) {
        if (shouldAbort && shouldAbort()) return {};
        std::fill(data.begin(), data.end(), 0.0f);
        std::copy_n(samples.begin() + (ptrdiff_t) f * hop, frameSize, data.begin());
        window.multiplyWithWindowingTable(data.data(), (size_t) frameSize);
        fft.performFrequencyOnlyForwardTransform(data.data(), true);
        for (int b = 0; b < bands; ++b) {
            double e = 0.0;
            for (int k = edges[b]; k < juce::jmax(edges[b] + 1, edges[b + 1]); ++k) e += (double) data[(size_t) k] * data[(size_t) k];
            energy[(size_t) f][(size_t) b] = e;
        }
    }

    // Un sonido quieto (un tono, un silencio) no distingue nada: dos distintos coincidirían.
    // Se mide cuánto cambia el reparto de la energía entre instantes separados unos 185 ms.
    auto share = [&energy](int f, int b) {
        double total = 1.0e-12;
        for (double e : energy[(size_t) f]) total += e;
        return energy[(size_t) f][(size_t) b] / total;
    };
    constexpr int fluxStep = 8;
    double flux = 0.0;
    int fluxFrames = 0;
    for (int f = fluxStep; f < frames; f += fluxStep, ++fluxFrames)
        for (int b = 0; b < bands; ++b) flux += std::abs(share(f, b) - share(f - fluxStep, b));
    if (fluxFrames == 0 || flux / fluxFrames < 0.1) return {};

    // Un bit por par de bandas: si la diferencia entre ellas creció de un instante al
    // siguiente. Un cambio que no pesa frente a la energía del momento no cuenta: en las bandas
    // casi vacías sería ruido, justo lo que los formatos con pérdida cambian.
    std::vector<uint32_t> fingerprint;
    fingerprint.reserve((size_t) frames);
    for (int f = 1; f < frames; ++f) {
        const auto& now = energy[(size_t) f];
        const auto& before = energy[(size_t) f - 1];
        double total = 1.0e-12;
        for (int b = 0; b < bands; ++b) total += now[(size_t) b] + before[(size_t) b];
        uint32_t bits = 0;
        for (size_t b = 0; b < 32; ++b) {
            const double change = (now[b] - now[b + 1]) - (before[b] - before[b + 1]);
            if (change > 0.002 * total) bits |= (uint32_t) 1 << b;
        }
        fingerprint.push_back(bits);
    }
    return fingerprint;
}

double similarity(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
    if (a.empty() || b.empty()) return 0.0;
    const int na = (int) a.size(), nb = (int) b.size();
    const int minOverlap = juce::jmax(50, juce::jmin(na, nb) / 2);

    // Coeficiente de Dice sobre los bits en 1 (los cambios): la mayoría de los bits son 0, y
    // contar los 0 que coinciden haría parecidas a dos canciones cualesquiera.
    double best = 0.0;
    for (int offset = -maxOffset; offset <= maxOffset; ++offset) {
        const int from = juce::jmax(0, -offset);
        const int to = juce::jmin(na, nb - offset);
        if (to - from < minOverlap) continue;
        long onesA = 0, onesB = 0, both = 0;
        for (int i = from; i < to; ++i) {
            const int j = i + offset;  // de 0 a nb - 1, por cómo se eligieron from y to
            const auto x = a[(size_t) i], y = b[(size_t) j];
            onesA += countBits(x);
            onesB += countBits(y);
            both += countBits(x & y);
        }
        if (onesA + onesB > 0) best = juce::jmax(best, 2.0 * (double) both / (double) (onesA + onesB));
    }
    return best;
}

}
