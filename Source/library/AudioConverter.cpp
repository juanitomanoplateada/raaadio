#include "AudioConverter.h"
#include "../network/DownloadTools.h"

AudioConverter::AudioConverter(Services s) : services(std::move(s)) {
    if (!services.findFfmpeg) services.findFfmpeg = [] {
        const auto tools = DownloadTools::find(DownloadTools::defaultFolder(), juce::SystemStats::getEnvironmentVariable("PATH", {}));
        if (tools.ffmpegFolder == juce::File()) return juce::File();
        const auto ffmpeg = tools.ffmpegFolder.getChildFile(DownloadTools::Paths::exe("ffmpeg"));
        return ffmpeg.existsAsFile() ? ffmpeg : juce::File();
    };
    if (!services.run) services.run = [](const juce::StringArray& c, const ProcessRunner::LineCallback& l, const ProcessRunner::StopCheck& s) {
        return ProcessRunner::run(c, l, s);
    };
}

const juce::StringArray& AudioConverter::extensions() {
    // .opus: notas de voz guardadas en el teléfono; .mpeg/.mpga: audios de WhatsApp Web;
    // .m4a/.aac: música y audios de iPhone; .amr: notas de voz de teléfonos viejos.
    static const juce::StringArray list { ".opus", ".m4a", ".aac", ".mpeg", ".mpga", ".amr" };
    return list;
}

bool AudioConverter::isConvertible(const juce::File& file) {
    return extensions().contains(file.getFileExtension(), true);
}

juce::String AudioConverter::fileWildcard() {
    juce::StringArray patterns;
    for (const auto& e : extensions()) patterns.add("*" + e);
    return patterns.joinIntoString(";");
}

bool AudioConverter::looksLikeMp3(const juce::File& file) {
    juce::FileInputStream in(file);
    if (!in.openedOk()) return false;

    // Etiquetas ID3v2 al principio (puede haber más de una): su tamaño va en 4 bytes de 7 bits.
    juce::uint8 head[10];
    for (int tags = 0; tags < 4; ++tags) {
        const auto start = in.getPosition();
        if (in.read(head, 10) != 10) return false;
        if (memcmp(head, "ID3", 3) != 0) {
            in.setPosition(start);
            break;
        }
        const juce::int64 size = ((head[6] & 0x7f) << 21) | ((head[7] & 0x7f) << 14) | ((head[8] & 0x7f) << 7) | (head[9] & 0x7f);
        in.setPosition(start + 10 + size + ((head[5] & 0x10) != 0 ? 10 : 0));
    }

    // Algunos codificadores dejan ceros antes de la primera trama.
    juce::uint8 b = 0;
    for (int zeros = 0; zeros < 4096; ++zeros) {
        if (in.read(&b, 1) != 1) return false;
        if (b != 0) break;
    }
    juce::uint8 frame[3];
    if (b != 0xff || in.read(frame, 3) != 3) return false;
    const int layer = (frame[0] >> 1) & 3;          // 1: capa III
    const int bitrate = (frame[1] >> 4) & 0x0f;     // 0: libre, 15: inválido
    const int sampleRate = (frame[1] >> 2) & 3;     // 3: inválido
    return (frame[0] & 0xe0) == 0xe0 && ((frame[0] >> 3) & 3) != 1 && layer == 1
        && bitrate != 0 && bitrate != 15 && sampleRate != 3;
}

bool AudioConverter::isOggOpus(const juce::File& file) {
    // La primera página de un Ogg: "OggS", 27 bytes de cabecera, la tabla de segmentos y el
    // primer paquete, que en un Opus es "OpusHead".
    juce::uint8 data[300] = {};
    juce::FileInputStream in(file);
    if (!in.openedOk()) return false;
    const int size = in.read(data, (int) sizeof(data));
    if (size < 36 || memcmp(data, "OggS", 4) != 0) return false;
    const int packet = 27 + data[26];
    return packet + 8 <= size && memcmp(data + packet, "OpusHead", 8) == 0;
}

juce::StringArray AudioConverter::arguments(const juce::File& ffmpeg, const juce::File& source, const juce::File& target,
                                            Format format) {
    juce::StringArray args { ffmpeg.getFullPathName(), "-nostdin", "-hide_banner", "-loglevel", "error", "-y",
             // "file:": un nombre con ":" o que empiece con "-" no se toma por otra cosa.
             "-i", "file:" + source.getFullPathName(),
             // Las etiquetas del archivo y las del audio (las de un Opus van en el audio). Solo
             // el primer audio: de un video no queda la imagen.
             "-map", "0:a:0", "-map_metadata", "0", "-map_metadata", "0:s:a:0" };
    if (format == Format::Mp3) {
        args.addArray({ "-af", "aformat=channel_layouts=mono|stereo:sample_rates=44100|48000",
                        "-c:a", "libmp3lame", "-q:a", "0", "-id3v2_version", "3",
                        "-fflags", "+bitexact", "-flags:a", "+bitexact", "-f", "mp3" });
    } else {
        // FFmpeg elige el formato más cercano: un 5.1 queda en estéreo, 8 kHz en 44,1.
        args.addArray({ "-af", "aformat=sample_fmts=s16:channel_layouts=mono|stereo:sample_rates=44100|48000",
                        "-c:a", "flac", "-fflags", "+bitexact", "-flags:a", "+bitexact", "-f", "flac" });
    }
    args.add("file:" + target.getFullPathName());
    return args;
}

bool AudioConverter::isAvailable() const {
    return services.findFfmpeg().existsAsFile();
}

AudioConverter::Result AudioConverter::toFlac(const juce::File& source, const juce::File& target,
                                              const ProcessRunner::StopCheck& shouldStop) const {
    return convert(source, target, Format::Flac, shouldStop);
}

AudioConverter::Result AudioConverter::toMp3(const juce::File& source, const juce::File& target,
                                             const ProcessRunner::StopCheck& shouldStop) const {
    return convert(source, target, Format::Mp3, shouldStop);
}

AudioConverter::Result AudioConverter::convert(const juce::File& source, const juce::File& target, Format format,
                                               const ProcessRunner::StopCheck& shouldStop) const {
    const auto ffmpeg = services.findFfmpeg();
    if (!ffmpeg.existsAsFile()) return Result::Unavailable;

    juce::StringArray output;  // las últimas líneas, para el registro si falla
    const int code = services.run(arguments(ffmpeg, source, target, format), [&output](const juce::String& line) {
        output.add(line);
        if (output.size() > 20) output.remove(0);
    }, shouldStop);

    if (code == ProcessRunner::stopped) {
        target.deleteFile();
        return Result::Cancelled;
    }
    if (code != 0 || target.getSize() == 0) {
        juce::Logger::writeToLog("AudioConverter: FFmpeg could not convert " + source.getFullPathName() + " (code "
                                 + juce::String(code) + "): " + output.joinIntoString(" | "));
        target.deleteFile();
        return Result::Failed;
    }
    return Result::Converted;
}
