#include "TagReader.h"

namespace TagReader {

namespace {
    using juce::uint8;

    juce::uint32 readBE32(const uint8* p) {
        return (juce::uint32) p[0] << 24 | (juce::uint32) p[1] << 16 | (juce::uint32) p[2] << 8 | p[3];
    }

    juce::uint32 readLE32(const uint8* p) {
        return (juce::uint32) p[3] << 24 | (juce::uint32) p[2] << 16 | (juce::uint32) p[1] << 8 | p[0];
    }

    size_t readSyncsafe(const uint8* p) {
        return (size_t) (p[0] & 0x7f) << 21 | (size_t) (p[1] & 0x7f) << 14 | (size_t) (p[2] & 0x7f) << 7 | (p[3] & 0x7f);
    }

    // "ISO-8859-1" según la etiqueta, aunque casi todos los programas escriben Windows-1252
    // (comillas tipográficas, €). Si los bytes son UTF-8 válido con acentos, el
    // etiquetador no marcó bien la codificación y se leen como UTF-8.
    juce::String decodeLatin1(const uint8* p, size_t n) {
        bool hasHighBytes = false;
        for (size_t i = 0; i < n; ++i) hasHighBytes |= p[i] >= 0x80;
        if (!hasHighBytes) return juce::String((const char*) p, n);

        if (juce::CharPointer_UTF8::isValidString((const char*) p, (int) n))
            return juce::String::fromUTF8((const char*) p, (int) n);

        static const juce::juce_wchar cp1252[32] = {
            0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017D, 0,
            0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178
        };
        juce::String s;
        s.preallocateBytes(n * 3);
        for (size_t i = 0; i < n; ++i) {
            const uint8 b = p[i];
            const juce::juce_wchar mapped = (b >= 0x80 && b < 0xA0) ? cp1252[b - 0x80] : 0;
            s += mapped != 0 ? mapped : (juce::juce_wchar) b;
        }
        return s;
    }

    juce::String decodeUtf16(const uint8* p, size_t n, bool bigEndian) {
        auto unitAt = [p, bigEndian](size_t i) {
            return bigEndian ? (juce::uint32) (p[i] << 8 | p[i + 1]) : (juce::uint32) (p[i + 1] << 8 | p[i]);
        };
        juce::String s;
        for (size_t i = 0; i + 1 < n; i += 2) {
            juce::uint32 unit = unitAt(i);
            if (unit >= 0xD800 && unit < 0xDC00 && i + 3 < n) {
                const juce::uint32 low = unitAt(i + 2);
                if (low >= 0xDC00 && low < 0xE000) {
                    unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
                    i += 2;
                }
            }
            if (unit != 0) s += (juce::juce_wchar) unit;
        }
        return s;
    }

    juce::String joinValues(juce::StringArray values) {
        values.trim();
        values.removeEmptyStrings();
        values.removeDuplicates(true);
        return values.joinIntoString(", ");
    }

    // Marco de texto ID3: un byte de codificación y uno o más valores separados por nulos.
    juce::String decodeTextFrame(const uint8* p, size_t n) {
        if (n < 2) return {};
        const uint8 encoding = p[0];
        ++p;
        --n;

        juce::StringArray values;
        if (encoding == 1 || encoding == 2) {
            size_t start = 0;
            for (size_t i = 0;; i += 2) {
                const bool atEnd = i + 1 >= n;
                if (atEnd || (p[i] == 0 && p[i + 1] == 0)) {
                    const uint8* segment = p + start;
                    size_t length = (atEnd ? n : i) - start;
                    bool bigEndian = encoding == 2;  // sin BOM se asume el orden de Windows
                    if (length >= 2 && segment[0] == 0xFF && segment[1] == 0xFE) { bigEndian = false; segment += 2; length -= 2; }
                    else if (length >= 2 && segment[0] == 0xFE && segment[1] == 0xFF) { bigEndian = true; segment += 2; length -= 2; }
                    values.add(decodeUtf16(segment, length, bigEndian));
                    if (atEnd) break;
                    start = i + 2;
                }
            }
        } else {
            size_t start = 0;
            for (size_t i = 0; i <= n; ++i) {
                if (i == n || p[i] == 0) {
                    values.add(encoding == 3 ? juce::String::fromUTF8((const char*) p + start, (int) (i - start))
                                             : decodeLatin1(p + start, i - start));
                    start = i + 1;
                }
            }
        }
        return joinValues(values);
    }

    std::vector<uint8> removeUnsynchronisation(const uint8* p, size_t n) {
        std::vector<uint8> out;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            out.push_back(p[i]);
            if (p[i] == 0xFF && i + 1 < n && p[i + 1] == 0x00) ++i;
        }
        return out;
    }

    bool isFrameId(const uint8* p, size_t length) {
        for (size_t i = 0; i < length; ++i)
            if (!((p[i] >= 'A' && p[i] <= 'Z') || (p[i] >= '0' && p[i] <= '9'))) return false;
        return true;
    }

    void fillMissing(Tags& into, const Tags& from) {
        if (into.artist.isEmpty()) into.artist = from.artist;
        if (into.title.isEmpty()) into.title = from.title;
    }

    // Textos que los programas de copia de CD ponen cuando no saben el dato.
    Tags withoutPlaceholders(Tags tags) {
        auto clean = [](const juce::String& s) {
            juce::String out;
            for (auto c : s)
                if (c >= 0x20 && c != 0x7f) out += c;
            return out.trim();
        };
        tags.artist = clean(tags.artist);
        tags.title = clean(tags.title);

        static const char* const unknownArtists[] = { "unknown", "unknown artist", "<unknown>", "artist",
                                                      "desconocido", "artista desconocido", "various artists" };
        for (auto* placeholder : unknownArtists)
            if (tags.artist.equalsIgnoreCase(placeholder)) tags.artist = {};

        const auto title = tags.title.toLowerCase();
        for (auto* prefix : { "track", "pista", "audiotrack" }) {
            const auto rest = title.fromFirstOccurrenceOf(prefix, false, false).trim();
            if (title.startsWith(prefix) && (rest.isEmpty() || rest.containsOnly("0123456789")))
                tags.title = {};
        }
        if (title == "untitled" || title == "sin titulo" || title == juce::String::fromUTF8("sin título"))
            tags.title = {};
        return tags;
    }

    Tags readFlac(juce::InputStream& in) {
        char magic[4] = {};
        if (in.read(magic, 4) != 4 || memcmp(magic, "fLaC", 4) != 0) return {};

        // Los bloques de metadatos se recorren saltando los que no interesan (la carátula
        // puede ocupar varios megabytes).
        while (!in.isExhausted()) {
            uint8 header[4];
            if (in.read(header, 4) != 4) break;
            const size_t length = (size_t) header[1] << 16 | (size_t) header[2] << 8 | header[3];
            if ((header[0] & 0x7f) == 4) {
                juce::MemoryBlock block;
                in.readIntoMemoryBlock(block, (juce::ssize_t) length);
                return parseVorbisComment((const uint8*) block.getData(), block.getSize());
            }
            if (header[0] & 0x80) break;  // era el último bloque
            in.setPosition(in.getPosition() + (juce::int64) length);
        }
        return {};
    }

    Tags parseVorbisPacket(const std::vector<uint8>& packet) {
        if (packet.size() >= 7 && packet[0] == 3 && memcmp(packet.data() + 1, "vorbis", 6) == 0)
            return parseVorbisComment(packet.data() + 7, packet.size() - 7);
        if (packet.size() >= 8 && memcmp(packet.data(), "OpusTags", 8) == 0)
            return parseVorbisComment(packet.data() + 8, packet.size() - 8);
        return {};
    }
}

Tags parseId3v2(const uint8* data, size_t size, size_t* tagLength) {
    Tags tags;
    if (tagLength != nullptr) *tagLength = 0;
    if (size < 10 || memcmp(data, "ID3", 3) != 0) return tags;

    const int major = data[3];
    const uint8 flags = data[5];
    const size_t bodySize = readSyncsafe(data + 6);
    const bool hasFooter = major >= 4 && (flags & 0x10) != 0;
    if (tagLength != nullptr) *tagLength = 10 + bodySize + (hasFooter ? 10 : 0);

    if (major < 2 || major > 4) return tags;
    if (major == 2 && (flags & 0x40)) return tags;  // compresión de 2.2: nunca se definió

    const size_t available = juce::jmin(bodySize, size - 10);
    // En 2.2 y 2.3 la desincronización se aplica a toda la etiqueta; en 2.4, por marco.
    std::vector<uint8> body = (flags & 0x80) && major < 4 ? removeUnsynchronisation(data + 10, available)
                                                          : std::vector<uint8>(data + 10, data + 10 + available);

    size_t pos = 0;
    if ((flags & 0x40) && major >= 3) {  // cabecera extendida
        if (body.size() < 4) return tags;
        pos = major == 3 ? readBE32(body.data()) + 4 : readSyncsafe(body.data());
    }

    const size_t idLength = major == 2 ? 3 : 4;
    const size_t headerLength = major == 2 ? 6 : 10;

    auto frameSizeAt = [&](size_t at, bool syncsafe) -> size_t {
        const uint8* h = body.data() + at;
        if (major == 2) return (size_t) h[3] << 16 | (size_t) h[4] << 8 | h[5];
        return syncsafe ? readSyncsafe(h + 4) : readBE32(h + 4);
    };
    auto isValidNext = [&](size_t next) {
        return next == body.size()
            || (next + headerLength <= body.size() && (body[next] == 0 || isFrameId(body.data() + next, idLength)));
    };

    juce::String albumArtist;
    while (pos + headerLength <= body.size()) {
        const uint8* header = body.data() + pos;
        if (header[0] == 0 || !isFrameId(header, idLength)) break;  // relleno o basura

        size_t frameSize = frameSizeAt(pos, major == 4);
        // Algunas versiones de iTunes escribían tamaños sin "syncsafe" en etiquetas 2.4.
        if (major == 4 && !isValidNext(pos + headerLength + frameSize)) {
            const size_t plain = frameSizeAt(pos, false);
            if (isValidNext(pos + headerLength + plain)) frameSize = plain;
        }

        const juce::String id((const char*) header, idLength);
        const uint8 formatFlags = major >= 3 ? header[9] : 0;

        pos += headerLength;
        if (frameSize == 0 || pos + frameSize > body.size()) break;
        const uint8* frame = body.data() + pos;
        pos += frameSize;

        const bool isTitle = id == "TIT2" || id == "TT2";
        const bool isArtist = id == "TPE1" || id == "TP1";
        const bool isAlbumArtist = id == "TPE2" || id == "TP2";
        if (!isTitle && !isArtist && !isAlbumArtist) continue;

        std::vector<uint8> content(frame, frame + frameSize);
        if (major == 3) {
            if (formatFlags & 0xC0) continue;  // comprimido o cifrado
            if ((formatFlags & 0x20) && !content.empty()) content.erase(content.begin());  // grupo
        } else if (major == 4) {
            if (formatFlags & 0x0C) continue;  // comprimido o cifrado
            size_t skip = ((formatFlags & 0x40) ? 1 : 0) + ((formatFlags & 0x01) ? 4 : 0);
            if (skip > content.size()) continue;
            content.erase(content.begin(), content.begin() + (std::ptrdiff_t) skip);
            if ((formatFlags & 0x02) || (flags & 0x80))
                content = removeUnsynchronisation(content.data(), content.size());
        }

        const auto text = decodeTextFrame(content.data(), content.size());
        if (isTitle) tags.title = text;
        else if (isArtist) tags.artist = text;
        else albumArtist = text;
    }

    if (tags.artist.isEmpty()) tags.artist = albumArtist;
    return tags;
}

Tags parseId3v1(const uint8* data, size_t size) {
    if (size < 128) return {};
    const uint8* tag = data + size - 128;
    if (memcmp(tag, "TAG", 3) != 0) return {};

    auto field = [](const uint8* p, size_t n) {
        size_t length = 0;
        while (length < n && p[length] != 0) ++length;
        return decodeLatin1(p, length).trim();
    };
    Tags tags;
    tags.title = field(tag + 3, 30);
    tags.artist = field(tag + 33, 30);
    return tags;
}

Tags parseVorbisComment(const uint8* data, size_t size) {
    size_t pos = 0;
    auto next32 = [&](juce::uint32& value) {
        if (pos + 4 > size) return false;
        value = readLE32(data + pos);
        pos += 4;
        return true;
    };

    juce::uint32 vendorLength = 0, count = 0;
    if (!next32(vendorLength) || pos + vendorLength > size) return {};
    pos += vendorLength;
    if (!next32(count)) return {};

    juce::StringArray titles, artists, albumArtists;
    for (juce::uint32 i = 0; i < count; ++i) {
        juce::uint32 length = 0;
        if (!next32(length) || pos + length > size) break;  // comentario truncado: lo leído vale
        const auto entry = juce::String::fromUTF8((const char*) data + pos, (int) length);
        pos += length;

        const int equals = entry.indexOfChar('=');
        if (equals <= 0) continue;
        const auto key = entry.substring(0, equals).toUpperCase();
        const auto value = entry.substring(equals + 1);
        if (key == "TITLE") titles.add(value);
        else if (key == "ARTIST") artists.add(value);
        else if (key == "ALBUMARTIST" || key == "ALBUM ARTIST") albumArtists.add(value);
    }

    Tags tags;
    tags.title = joinValues(titles);
    tags.artist = joinValues(artists);
    if (tags.artist.isEmpty()) tags.artist = joinValues(albumArtists);
    return tags;
}

Tags parseFlac(const uint8* data, size_t size) {
    juce::MemoryInputStream in(data, size, false);
    return readFlac(in);
}

Tags parseOgg(const uint8* data, size_t size) {
    // El segundo paquete del primer flujo lógico es el comentario; puede ocupar varias
    // páginas. Si los datos se acaban antes, se lee lo que haya llegado.
    std::vector<uint8> packet;
    int packetIndex = 0;
    juce::uint32 serial = 0;
    bool haveSerial = false;
    size_t pos = 0;

    while (pos + 27 <= size && memcmp(data + pos, "OggS", 4) == 0) {
        const uint8* page = data + pos;
        const juce::uint32 pageSerial = readLE32(page + 14);
        const int segments = page[26];
        if (pos + 27 + (size_t) segments > size) break;

        const uint8* lacing = page + 27;
        size_t dataPos = pos + 27 + (size_t) segments;
        size_t pageDataLength = 0;
        for (int s = 0; s < segments; ++s) pageDataLength += lacing[s];

        if (!haveSerial) { serial = pageSerial; haveSerial = true; }
        if (pageSerial == serial) {
            for (int s = 0; s < segments; ++s) {
                const size_t segmentLength = lacing[s];
                if (packetIndex == 1 && dataPos < size)
                    packet.insert(packet.end(), data + dataPos, data + juce::jmin(size, dataPos + segmentLength));
                dataPos += segmentLength;
                if (segmentLength < 255) {  // fin de paquete
                    if (packetIndex == 1) return parseVorbisPacket(packet);
                    ++packetIndex;
                }
            }
        }
        pos += 27 + (size_t) segments + pageDataLength;
    }
    return packetIndex == 1 ? parseVorbisPacket(packet) : Tags {};
}

Tags read(const juce::File& file, const juce::StringPairArray& readerMetadata) {
    Tags tags;
    juce::FileInputStream in(file);
    if (!in.openedOk()) return tags;

    // ID3v2 al inicio (MP3; a veces también delante de un FLAC). Se lee completa, salvo
    // carátulas enormes: título y artista suelen ir antes.
    constexpr size_t maxTagBytes = 16 * 1024 * 1024;
    uint8 head[10] = {};
    size_t afterId3 = 0;
    if (in.read(head, 10) == 10 && memcmp(head, "ID3", 3) == 0) {
        const size_t length = juce::jmin(maxTagBytes, 10 + readSyncsafe(head + 6));
        juce::MemoryBlock block;
        in.setPosition(0);
        in.readIntoMemoryBlock(block, (juce::ssize_t) length);
        fillMissing(tags, withoutPlaceholders(parseId3v2((const uint8*) block.getData(), block.getSize(), &afterId3)));
    }

    in.setPosition((juce::int64) afterId3);
    char magic[4] = {};
    if (in.read(magic, 4) == 4) {
        in.setPosition((juce::int64) afterId3);
        if (memcmp(magic, "fLaC", 4) == 0) {
            fillMissing(tags, withoutPlaceholders(readFlac(in)));
        } else if (memcmp(magic, "OggS", 4) == 0) {
            juce::MemoryBlock block;
            in.readIntoMemoryBlock(block, 512 * 1024);
            fillMissing(tags, withoutPlaceholders(parseOgg((const uint8*) block.getData(), block.getSize())));
        }
    }

    // ID3v1 al final del archivo.
    if ((tags.artist.isEmpty() || tags.title.isEmpty()) && in.getTotalLength() >= 128) {
        uint8 tail[128];
        in.setPosition(in.getTotalLength() - 128);
        if (in.read(tail, 128) == 128)
            fillMissing(tags, withoutPlaceholders(parseId3v1(tail, 128)));
    }

    // WAV: bloque LIST/INFO que ya leyó JUCE.
    Tags info;
    info.title = readerMetadata.getValue("INAM", {});
    info.artist = readerMetadata.getValue("IART", {});
    fillMissing(tags, withoutPlaceholders(info));

    return tags;
}

}
