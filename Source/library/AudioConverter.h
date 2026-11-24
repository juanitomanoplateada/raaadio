#pragma once
#include <JuceHeader.h>
#include "../helpers/ProcessRunner.h"

// Lo que JUCE no sabe leer y llega seguido a una emisora (las notas de voz y los audios de
// WhatsApp, música en M4A o AAC) se convierte a FLAC con FFmpeg al importarlo: el FLAC guarda
// sin perder nada lo que FFmpeg decodifica y el deck lo lee por su cuenta. FFmpeg es el mismo
// que instala la descarga de canciones (DownloadTools); sin él, esos archivos no se importan.
//
// Los .mpeg de WhatsApp suelen ser MP3 con otro nombre: esos se guardan como .mp3, tal cual,
// sin FFmpeg (looksLikeMp3).
class AudioConverter {
public:
    using Runner = std::function<int(const juce::StringArray& command, const ProcessRunner::LineCallback& onLine,
                                     const ProcessRunner::StopCheck& shouldStop)>;
    // Lo que se puede reemplazar en las pruebas (por omisión, lo de verdad).
    struct Services {
        std::function<juce::File()> findFfmpeg;  // ffmpeg.exe, o File() si no está
        Runner run;
    };

    explicit AudioConverter(Services services = {});

    // Extensiones que solo se importan convirtiéndolas: ".opus", ".m4a", ".aac", ".mpeg"...
    static const juce::StringArray& extensions();
    static bool isConvertible(const juce::File& file);
    // "*.opus;*.m4a;..." para el selector de archivos.
    static juce::String fileWildcard();

    // ¿Es un MP3 aunque se llame .mpeg? Después de la etiqueta ID3, si la hay, empieza con
    // una trama MPEG de capa III (un video .mpeg no).
    static bool looksLikeMp3(const juce::File& file);
    // ¿Es un Ogg con Opus (las notas de voz de WhatsApp)? El lector de Ogg Vorbis no lo entiende.
    static bool isOggOpus(const juce::File& file);

    // FFmpeg: el primer audio de source (no la carátula) a FLAC de 16 bits, mono o estéreo, a
    // 44,1 o 48 kHz, con sus etiquetas. Sin la versión de FFmpeg en el archivo: el mismo
    // original da siempre el mismo FLAC, y así se reconoce si se importa otra vez.
    // Mp3: lo mismo a MP3 de calidad variable máxima (V0), para el audio de un video bajado de
    // una red social (ya venía con pérdida: un FLAC solo ocuparía más).
    enum class Format { Flac, Mp3 };
    static juce::StringArray arguments(const juce::File& ffmpeg, const juce::File& source, const juce::File& target,
                                       Format format = Format::Flac);

    bool isAvailable() const;

    enum class Result { Converted, Unavailable, Failed, Cancelled };
    // Convierte source a FLAC o MP3 en target (quien lo pide lo borra). Bloquea mientras FFmpeg trabaja.
    Result toFlac(const juce::File& source, const juce::File& target, const ProcessRunner::StopCheck& shouldStop) const;
    Result toMp3(const juce::File& source, const juce::File& target, const ProcessRunner::StopCheck& shouldStop) const;

private:
    Services services;
    Result convert(const juce::File& source, const juce::File& target, Format format, const ProcessRunner::StopCheck& shouldStop) const;
};
