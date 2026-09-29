#include "TagWriter.h"

namespace TagWriter {

namespace {
    // Texto en UTF-16 little endian, con su marca (FF FE) y, si se pide, el cero final.
    void appendUtf16(juce::MemoryOutputStream& out, const juce::String& text, bool terminate) {
        out.writeByte((char) 0xff);
        out.writeByte((char) 0xfe);
        for (auto p = text.getCharPointer(); !p.isEmpty(); ++p) {
            auto c = (juce::uint32) *p;
            if (c >= 0x10000) {  // fuera del plano básico: par sustituto
                c -= 0x10000;
                out.writeShort((short) (0xd800 + (c >> 10)));
                out.writeShort((short) (0xdc00 + (c & 0x3ff)));
            } else {
                out.writeShort((short) c);
            }
        }
        if (terminate) out.writeShort(0);
    }

    void appendFrame(juce::MemoryOutputStream& tag, const char* id, const juce::MemoryBlock& body) {
        tag.write(id, 4);
        tag.writeIntBigEndian((int) body.getSize());  // en 2.3 el tamaño del cuadro no es syncsafe
        tag.writeShort(0);                            // sin banderas
        tag.write(body.getData(), body.getSize());
    }

    void appendText(juce::MemoryOutputStream& tag, const char* id, const juce::String& text) {
        if (text.trim().isEmpty()) return;
        juce::MemoryOutputStream body;
        body.writeByte(1);  // UTF-16 con marca
        appendUtf16(body, text.trim(), false);
        appendFrame(tag, id, body.getMemoryBlock());
    }

    // Tamaño de la etiqueta ID3v2 al principio de data (0 si no tiene).
    size_t existingTagSize(const juce::uint8* data, size_t size) {
        if (size < 10 || data[0] != 'I' || data[1] != 'D' || data[2] != '3') return 0;
        if ((data[6] | data[7] | data[8] | data[9]) & 0x80) return 0;  // no es syncsafe: no es ID3
        const size_t body = ((size_t) data[6] << 21) | ((size_t) data[7] << 14) | ((size_t) data[8] << 7) | data[9];
        const size_t footer = (data[5] & 0x10) != 0 ? 10 : 0;
        return juce::jmin(size, 10 + body + footer);
    }
}

juce::MemoryBlock buildId3v2(const Tags& tags) {
    juce::MemoryOutputStream frames;
    appendText(frames, "TIT2", tags.title);
    appendText(frames, "TPE1", tags.artist);
    appendText(frames, "TALB", tags.album);
    if (tags.year > 0) appendText(frames, "TYER", juce::String(tags.year));
    appendText(frames, "TSRC", tags.isrc);
    if (tags.source.isNotEmpty()) {
        juce::MemoryOutputStream body;
        body.writeByte(1);
        appendUtf16(body, "SOURCE", true);
        appendUtf16(body, tags.source, false);
        appendFrame(frames, "TXXX", body.getMemoryBlock());
    }
    // Relleno: otros programas pueden editar las etiquetas sin reescribir todo el archivo.
    for (int i = 0; i < 256; ++i) frames.writeByte(0);

    const auto size = (juce::uint32) frames.getDataSize();
    juce::MemoryOutputStream tag;
    tag.write("ID3", 3);
    tag.writeByte(3);  // versión 2.3.0
    tag.writeByte(0);
    tag.writeByte(0);  // sin banderas
    for (int shift : { 21, 14, 7, 0 }) tag.writeByte((char) ((size >> shift) & 0x7f));  // syncsafe
    tag.write(frames.getData(), frames.getDataSize());
    return tag.getMemoryBlock();
}

bool writeMp3(const juce::File& file, const Tags& tags) {
    juce::MemoryBlock original;
    if (!file.loadFileAsData(original) || original.getSize() == 0) return false;
    const auto* data = static_cast<const juce::uint8*>(original.getData());
    const auto skip = existingTagSize(data, original.getSize());

    // Se escribe al lado y se reemplaza: si algo falla, el archivo original queda intacto.
    const auto temp = file.getSiblingFile(file.getFileName() + ".tagging");
    temp.deleteFile();
    {
        juce::FileOutputStream out(temp);
        if (!out.openedOk()) return false;
        const auto tag = buildId3v2(tags);
        if (!out.write(tag.getData(), tag.getSize()) || !out.write(data + skip, original.getSize() - skip)) {
            out.flush();
            temp.deleteFile();
            return false;
        }
        out.flush();
        if (out.getStatus().failed()) {
            temp.deleteFile();
            return false;
        }
    }
    if (!temp.moveFileTo(file)) {
        temp.deleteFile();
        return false;
    }
    return true;
}

}
