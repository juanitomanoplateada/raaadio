#include "Mp3Encoder.h"

namespace {
    // Las funciones de LAME que usa RAAADIO (lame.h), buscadas por nombre en la DLL: sirve
    // cualquier libmp3lame compilada de la versión 3.99 en adelante.
    struct LameLibrary {
        using InitFn = void* (*)();
        using SetIntFn = int (*)(void*, int);
        using InitParamsFn = int (*)(void*);
        using EncodeFn = int (*)(void*, const float*, const float*, int, unsigned char*, int);
        using FlushFn = int (*)(void*, unsigned char*, int);
        using CloseFn = int (*)(void*);
        using VersionFn = const char* (*)();

        InitFn init = nullptr;
        SetIntFn setInSampleRate = nullptr, setOutSampleRate = nullptr, setNumChannels = nullptr,
                 setBitrate = nullptr, setMode = nullptr, setQuality = nullptr, setVbr = nullptr,
                 setWriteVbrTag = nullptr;
        InitParamsFn initParams = nullptr;
        EncodeFn encodeFloat = nullptr;
        FlushFn flush = nullptr;
        CloseFn close = nullptr;
        VersionFn version = nullptr;
        bool loaded = false;

        static LameLibrary& get() {
            static LameLibrary instance;  // se carga una sola vez, la primera vez que se pide
            return instance;
        }

    private:
        LameLibrary() {
            // Solo desde la carpeta del ejecutable, no desde la ruta de búsqueda del sistema.
           #if JUCE_WINDOWS
            const auto file = juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile("libmp3lame.dll");
            if (!library.open(file.getFullPathName())) return;
           #elif JUCE_MAC
            if (!library.open("libmp3lame.dylib")) return;
           #else
            if (!library.open("libmp3lame.so.0")) return;
           #endif

            auto find = [this](const char* name, auto& fn) {
                fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(library.getFunction(name));
                return fn != nullptr;
            };
            loaded = find("lame_init", init) && find("lame_set_in_samplerate", setInSampleRate)
                  && find("lame_set_out_samplerate", setOutSampleRate) && find("lame_set_num_channels", setNumChannels)
                  && find("lame_set_brate", setBitrate) && find("lame_set_mode", setMode)
                  && find("lame_set_quality", setQuality) && find("lame_set_VBR", setVbr)
                  && find("lame_set_bWriteVbrTag", setWriteVbrTag) && find("lame_init_params", initParams)
                  && find("lame_encode_buffer_ieee_float", encodeFloat) && find("lame_encode_flush", flush)
                  && find("lame_close", close) && find("get_lame_short_version", version);
            if (!loaded) juce::Logger::writeToLog("Mp3Encoder: libmp3lame is missing functions (older than 3.99?)");
        }

        juce::DynamicLibrary library;
    };

    constexpr int jointStereo = 1;  // MPEG_mode::JOINT_STEREO
    constexpr int cbr = 0;          // vbr_mode::vbr_off
    constexpr int highQuality = 2;  // algoritmo de LAME: 0 el mejor y más lento, 5 el normal
    constexpr int chunkSamples = 4096;
}

const juce::Array<int>& Mp3Encoder::bitrates() {
    static const juce::Array<int> values { 64, 96, 112, 128, 160, 192, 256, 320 };
    return values;
}

bool Mp3Encoder::isAvailable() {
    return LameLibrary::get().loaded;
}

juce::String Mp3Encoder::libraryVersion() {
    auto& lib = LameLibrary::get();
    return lib.loaded ? juce::String(lib.version()) : juce::String();
}

int Mp3Encoder::outputRateFor(double sampleRate) {
    const int rate = juce::roundToInt(sampleRate);
    return (rate == 32000 || rate == 44100 || rate == 48000) ? rate : 44100;
}

Mp3Encoder::~Mp3Encoder() {
    close();
}

bool Mp3Encoder::open(double sampleRate, int bitrateKbps) {
    close();
    auto& lib = LameLibrary::get();
    if (!lib.loaded) return false;

    lame = lib.init();
    if (lame == nullptr) return false;

    outputRate = outputRateFor(sampleRate);
    lib.setInSampleRate(lame, juce::roundToInt(sampleRate));
    lib.setOutSampleRate(lame, outputRate);
    lib.setNumChannels(lame, 2);
    lib.setMode(lame, jointStereo);
    lib.setVbr(lame, cbr);
    lib.setBitrate(lame, bitrateKbps);
    lib.setQuality(lame, highQuality);
    lib.setWriteVbrTag(lame, 0);  // la etiqueta Xing es para archivos, no para una transmisión
    if (lib.initParams(lame) < 0) {
        close();
        return false;
    }

    // El peor caso según lame.h es 1,25 × muestras + 7200 bytes; el doble, por si LAME
    // además sube la tasa de muestreo.
    bufferSize = 2 * (chunkSamples + chunkSamples / 4) + 7200;
    buffer.malloc((size_t) bufferSize);
    return true;
}

bool Mp3Encoder::encode(const float* left, const float* right, int numSamples, juce::MemoryBlock& out) {
    if (lame == nullptr) return false;
    auto& lib = LameLibrary::get();
    for (int done = 0; done < numSamples;) {
        const int n = juce::jmin(chunkSamples, numSamples - done);
        const int bytes = lib.encodeFloat(lame, left + done, right + done, n, buffer.get(), bufferSize);
        if (bytes < 0) return false;
        out.append(buffer.get(), (size_t) bytes);
        done += n;
    }
    return true;
}

void Mp3Encoder::flush(juce::MemoryBlock& out) {
    if (lame == nullptr) return;
    const int bytes = LameLibrary::get().flush(lame, buffer.get(), bufferSize);
    if (bytes > 0) out.append(buffer.get(), (size_t) bytes);
}

void Mp3Encoder::close() {
    if (lame != nullptr) LameLibrary::get().close(lame);
    lame = nullptr;
}
