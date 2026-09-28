#include "Chromaprint.h"

namespace Chromaprint {

namespace {
    // Lo que se usa de chromaprint.h, sin incluirlo: la biblioteca se carga en ejecución.
    struct Library {
        using NewFn = void* (*)(int);
        using FreeFn = void (*)(void*);
        using StartFn = int (*)(void*, int, int);
        using FeedFn = int (*)(void*, const int16_t*, int);
        using FinishFn = int (*)(void*);
        using GetFingerprintFn = int (*)(void*, char**);
        using DeallocFn = void (*)(void*);
        using VersionFn = const char* (*)();

        NewFn create = nullptr;
        FreeFn free = nullptr;
        StartFn start = nullptr;
        FeedFn feed = nullptr;
        FinishFn finish = nullptr;
        GetFingerprintFn getFingerprint = nullptr;
        DeallocFn dealloc = nullptr;
        VersionFn version = nullptr;
        bool loaded = false;

        static Library& get() {
            static Library instance;  // se carga una sola vez, la primera vez que se pide
            return instance;
        }

    private:
        Library() {
            // Solo desde la carpeta del ejecutable, no desde la ruta de búsqueda del sistema.
           #if JUCE_WINDOWS
            const auto file = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("chromaprint.dll");
            if (!library.open(file.getFullPathName())) return;
           #elif JUCE_MAC
            if (!library.open("libchromaprint.dylib")) return;
           #else
            if (!library.open("libchromaprint.so.1")) return;
           #endif

            auto find = [this](const char* name, auto& fn) {
                fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(library.getFunction(name));
                return fn != nullptr;
            };
            loaded = find("chromaprint_new", create) && find("chromaprint_free", free) && find("chromaprint_start", start)
                  && find("chromaprint_feed", feed) && find("chromaprint_finish", finish)
                  && find("chromaprint_get_fingerprint", getFingerprint) && find("chromaprint_dealloc", dealloc)
                  && find("chromaprint_get_version", version);
            if (!loaded) juce::Logger::writeToLog("Chromaprint: chromaprint.dll is missing functions");
        }

        juce::DynamicLibrary library;
    };

    constexpr int defaultAlgorithm = 1;  // CHROMAPRINT_ALGORITHM_DEFAULT (TEST2), el de AcoustID
    constexpr double maxSeconds = 120.0;
}

bool isAvailable() {
    return Library::get().loaded;
}

juce::String libraryVersion() {
    auto& lib = Library::get();
    return lib.loaded ? juce::String(lib.version()) : juce::String();
}

Result compute(juce::AudioFormatReader& reader, const std::function<bool()>& shouldAbort) {
    Result result;
    auto& lib = Library::get();
    if (!lib.loaded || reader.sampleRate <= 0.0 || reader.lengthInSamples <= 0) return result;

    result.duration = (int) std::lround((double) reader.lengthInSamples / reader.sampleRate);
    void* context = lib.create(defaultAlgorithm);
    if (context == nullptr) return result;

    const int channels = (int) juce::jmax(1u, juce::jmin(2u, reader.numChannels));
    bool ok = lib.start(context, (int) std::lround(reader.sampleRate), channels) == 1;

    // Los dos primeros minutos, en enteros de 16 bits intercalados.
    const auto total = juce::jmin(reader.lengthInSamples, (juce::int64) (maxSeconds * reader.sampleRate));
    constexpr int block = 16384;
    juce::AudioBuffer<float> buffer(channels, block);
    std::vector<int16_t> interleaved((size_t) block * (size_t) channels);
    for (juce::int64 pos = 0; ok && pos < total; pos += block) {
        if (shouldAbort && shouldAbort()) { ok = false; break; }
        const int n = (int) juce::jmin((juce::int64) block, total - pos);
        buffer.clear();
        reader.read(&buffer, 0, n, pos, true, channels > 1);
        for (int i = 0; i < n; ++i)
            for (int ch = 0; ch < channels; ++ch)
                interleaved[(size_t) (i * channels + ch)] = (int16_t) juce::jlimit(-32768, 32767, (int) std::lround(buffer.getSample(ch, i) * 32767.0f));
        ok = lib.feed(context, interleaved.data(), n * channels) == 1;
    }

    char* fingerprint = nullptr;
    if (ok && lib.finish(context) == 1 && lib.getFingerprint(context, &fingerprint) == 1 && fingerprint != nullptr) {
        result.fingerprint = juce::String(fingerprint);
        lib.dealloc(fingerprint);
    }
    lib.free(context);
    return result;
}

}
