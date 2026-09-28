#include "TrackNaming.h"
#include <atomic>

namespace TrackNaming {

namespace {
    std::atomic<bool> displayUppercase { false };

    bool isUpperLetter(juce::juce_wchar c) {
        return (c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7);
    }
    bool isLowerLetter(juce::juce_wchar c) {
        return (c >= 'a' && c <= 'z') || (c >= 0xDF && c <= 0xFF && c != 0xF7);
    }
    bool isDigit(juce::juce_wchar c) { return c >= '0' && c <= '9'; }

    juce::juce_wchar toLowerChar(juce::juce_wchar c) {
        if ((c >= 'A' && c <= 'Z') || (c >= 0xC0 && c <= 0xDE && c != 0xD7)) return c + 0x20;
        return juce::CharacterFunctions::toLowerCase(c);
    }
    juce::juce_wchar toUpperChar(juce::juce_wchar c) {
        if ((c >= 'a' && c <= 'z') || (c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 0x20;
        return juce::CharacterFunctions::toUpperCase(c);
    }

    // Letra en minúscula sin tilde (para las claves).
    juce::juce_wchar foldChar(juce::juce_wchar c) {
        c = toLowerChar(c);
        switch (c) {
            case 0xE0: case 0xE1: case 0xE2: case 0xE3: case 0xE4: case 0xE5: return 'a';
            case 0xE7: return 'c';
            case 0xE8: case 0xE9: case 0xEA: case 0xEB: return 'e';
            case 0xEC: case 0xED: case 0xEE: case 0xEF: return 'i';
            case 0xF1: return 'n';
            case 0xF2: case 0xF3: case 0xF4: case 0xF5: case 0xF6: case 0xF8: return 'o';
            case 0xF9: case 0xFA: case 0xFB: case 0xFC: return 'u';
            case 0xFD: case 0xFF: return 'y';
            default: return c;
        }
    }

    juce::String collapseSpaces(const juce::String& s) {
        juce::String out;
        out.preallocateBytes((size_t) s.getNumBytesAsUTF8());
        bool pendingSpace = false;
        for (auto c : s) {
            if (c == ' ' || c == '\t' || c == 0xA0) { pendingSpace = out.isNotEmpty(); continue; }
            if (pendingSpace) { out += ' '; pendingSpace = false; }
            out += c;
        }
        return out;
    }

    // Quita separadores sueltos en los bordes: " - ", comas, puntos y guiones bajos.
    juce::String trimSeparators(const juce::String& s) {
        return s.trim().trimCharactersAtStart(" -_,.;:").trimCharactersAtEnd(" -_,;:").trim();
    }

    const char* const smallWords[] = {
        "a", "al", "con", "de", "del", "e", "el", "en", "la", "las", "lo", "los", "o", "para", "por",
        "sin", "su", "sus", "u", "y", "of", "the", "and", "in", "on", "to", "vs", "vs.", "feat.", "ft.", "feat", "ft",
    };

    bool isSmallWord(const juce::String& lowered) {
        for (auto* w : smallWords)
            if (lowered == w) return true;
        return false;
    }

    bool isRomanNumeral(const juce::String& upperWord) {
        if (upperWord.length() < 2) return false;
        for (auto* r : { "II", "III", "IV", "VII", "VIII", "IX", "XII", "XIII", "XIV", "XV", "XX" })
            if (upperWord == r) return true;
        return false;
    }

    // "D.R.A.", "T.L.F.": letras sueltas separadas por puntos.
    bool isDottedAcronym(const juce::String& word) {
        const auto parts = juce::StringArray::fromTokens(word.trimCharactersAtStart("(").trimCharactersAtEnd(")"), ".", "");
        int letters = 0;
        for (const auto& part : parts) {
            if (part.isEmpty()) continue;
            if (part.length() != 1) return false;
            ++letters;
        }
        return letters >= 2;
    }

    bool hasVowel(const juce::String& s) {
        for (auto c : s) {
            const auto f = foldChar(c);
            if (f == 'a' || f == 'e' || f == 'i' || f == 'o' || f == 'u') return true;
        }
        return false;
    }

    // Una palabra con mayúscula inicial, respetando la puntuación de adelante ("¿Qué", "(En").
    juce::String capitaliseWord(const juce::String& word) {
        juce::String out;
        bool capitalised = false;
        juce::juce_wchar previous = 0;
        int letters = 0;
        for (auto c : word) {
            const bool letter = isUpperLetter(c) || isLowerLetter(c) || juce::CharacterFunctions::isLetter(c);
            if (letter) {
                // Mayúscula al inicio, tras un guion ("Hip-Hop") y tras "D'" ("D'León").
                const bool afterApostrophe = (previous == '\'' || previous == 0x2019) && letters == 1;
                out += (!capitalised || previous == '-' || afterApostrophe) ? toUpperChar(c) : toLowerChar(c);
                capitalised = true;
                ++letters;
            } else {
                out += c;
            }
            previous = c;
        }
        return out;
    }

    const char* const builtInGenres[] = {
        "antologia", "bachata", "balada", "bambuco", "blues", "bolero", "boleros", "carranga", "carrilera",
        "champeta", "chirimia", "corrido", "corridos", "cristiana", "cumbia", "currulao", "dance", "decembrina",
        "despecho", "disco", "electronica", "guaracha", "guasca", "house", "instrumental", "jazz", "joropo",
        "mapale", "merengue", "mexicana", "nortena", "pasillo", "pop", "popular", "porro", "ranchera", "reggae",
        "reggaeton", "rock", "salsa", "son", "tango", "techno", "tecnomerengue", "tropical", "urbana",
        "vallenato", "viejoteca", "zumba", "cumbion", "ballenato",
    };

    const char* const genreModifiers[] = {
        "tecno", "techno", "electro", "nuevo", "nueva", "clasico", "clasica", "romantico", "romantica",
        "brava", "urbano", "tropical", "sensual", "pop",
    };

    // Grupos entre paréntesis que no son parte del nombre ("Video Oficial", "HD"...).
    bool isJunkGroup(const juce::String& groupKey) {
        static const char* const junk[] = {
            "official video", "video oficial", "official audio", "audio oficial", "audio official", "audio",
            "video", "videoclip", "video clip", "clip oficial", "official music video", "official lyric video",
            "lyric video", "lyrics video", "video lyric", "video lyrics", "lyrics", "lyric", "letra", "con letra",
            "hd", "hq", "4k", "1080p", "720p", "mp3", "320 kbps", "320kbps", "128kbps", "official", "oficial",
            "visualizer", "visualizador", "video official", "videoclip oficial", "musica", "audio hd",
        };
        for (auto* j : junk)
            if (groupKey == j) return true;
        return groupKey.contains("www ") || groupKey.startsWith("www") || groupKey.endsWith(" com");
    }

    bool isVersionText(const juce::String& textKey) {
        static const char* const words[] = {
            "en vivo", "live", "remix", "remasterizado", "remasterizada", "remaster", "remastered", "version",
            "acustico", "acustica", "acoustic", "radio edit", "edit", "extended", "mix", "instrumental",
            "karaoke", "cover", "unplugged", "desenchufado", "demo", "directo", "en directo", "original",
        };
        const auto padded = " " + textKey + " ";
        for (auto* w : words)
            if (padded.contains(" " + juce::String(w) + " ")) return true;
        return false;
    }

    // Marcadores de invitado: ", canta X", "feat. X", "ft X"...
    const char* const featuringMarkers[] = {
        " featuring ", " feat. ", " feat ", " ft. ", " ft ", ", canta ", " canta ", "(canta ", ", voz ", " voz: ",
        "(feat. ", "(feat ", "(ft. ", "(ft ", " con la voz de ",
    };

    // Posición del primer marcador de invitado en s (en minúsculas, con espacio al inicio
    // para encontrarlo al comienzo), o -1.
    int findFeaturing(const juce::String& s, int& markerLength) {
        const auto lowered = " " + lower(s) + " ";
        int best = -1;
        for (auto* m : featuringMarkers) {
            const int at = lowered.indexOf(m);
            if (at >= 0 && (best < 0 || at < best)) {
                best = at;
                markerLength = (int) strlen(m);
            }
        }
        return best < 0 ? -1 : juce::jmax(0, best - 1);  // -1 por el espacio agregado
    }

    // Grupos al final "(...)" o "[...]": devuelve el de más afuera y deja en rest lo de antes.
    bool takeTrailingGroup(const juce::String& s, juce::String& rest, juce::String& group) {
        const auto trimmed = s.trimEnd();
        const auto close = trimmed.getLastCharacter();
        if (close != ')' && close != ']') return false;
        const juce::juce_wchar open = close == ')' ? '(' : '[';
        int depth = 0;
        for (int i = trimmed.length() - 1; i >= 0; --i) {
            const auto c = trimmed[i];
            if (c == close) ++depth;
            else if (c == open && --depth == 0) {
                group = trimmed.substring(i + 1, trimmed.length() - 1).trim();
                rest = trimmed.substring(0, i).trimEnd();
                return true;
            }
        }
        return false;
    }

    bool isYear(const juce::String& s) {
        if (s.length() != 4 || !s.containsOnly("0123456789")) return false;
        const int y = s.getIntValue();
        return y >= 1930 && y <= 2049;
    }

    // Año al final: "... 1997", "... - 1997", "... (1997)".
    int takeTrailingYear(juce::String& s) {
        auto t = s.trimEnd();
        juce::String candidate;
        juce::String before;
        if ((t.endsWithChar(')') || t.endsWithChar(']')) && t.length() >= 6) {
            candidate = t.substring(t.length() - 5, t.length() - 1);
            const auto open = t[t.length() - 6];
            if ((open == '(' || open == '[') && isYear(candidate)) before = t.substring(0, t.length() - 6);
            else candidate = {};
        }
        if (candidate.isEmpty() && t.length() >= 5) {
            candidate = t.getLastCharacters(4);
            const auto sep = t[t.length() - 5];
            if (isYear(candidate) && (sep == ' ' || sep == '-' || sep == '_')) before = t.substring(0, t.length() - 4);
            else candidate = {};
        }
        if (candidate.isEmpty() || trimSeparators(before).isEmpty()) return 0;
        s = trimSeparators(before);
        return candidate.getIntValue();
    }

    // Partes separadas por guiones con espacio (" - ") fuera de paréntesis. Sin ninguno,
    // un solo guion sin espacios separa dos partes ("Artista-Titulo").
    juce::StringArray splitParts(const juce::String& s) {
        juce::StringArray parts;
        int depth = 0, start = 0;
        const int n = s.length();
        for (int i = 0; i < n; ++i) {
            const auto c = s[i];
            if (c == '(' || c == '[') ++depth;
            else if ((c == ')' || c == ']') && depth > 0) --depth;
            else if (depth == 0 && (c == '-' || c == 0x2013 || c == 0x2014)) {
                const bool spaceBefore = i > 0 && s[i - 1] == ' ';
                int end = i;
                while (end + 1 < n && (s[end + 1] == '-')) ++end;  // "--"
                const bool spaceAfter = end + 1 < n && s[end + 1] == ' ';
                if (spaceBefore || spaceAfter) {
                    parts.add(s.substring(start, i));
                    start = end + 1;
                    i = end;
                }
            }
        }
        parts.add(s.substring(start));
        for (auto& p : parts) p = trimSeparators(p);
        parts.removeEmptyStrings();

        if (parts.size() == 1) {
            const auto only = parts[0];
            const int dash = only.indexOfChar('-');
            if (dash > 1 && dash == only.lastIndexOfChar('-') && dash < only.length() - 2
                && !only.containsChar('(')) {
                parts.clear();
                parts.add(trimSeparators(only.substring(0, dash)));
                parts.add(trimSeparators(only.substring(dash + 1)));
                parts.removeEmptyStrings();
            }
        }
        return parts;
    }

    // Lo principal de un lado (para contar repeticiones): sin paréntesis ni invitados.
    // Lo principal de un lado del nombre, sin paréntesis ni invitados.
    juce::String primaryText(const juce::String& side) {
        auto s = side;
        juce::String rest, group;
        while (takeTrailingGroup(s, rest, group)) s = rest;
        int markerLength = 0;
        const int at = findFeaturing(s, markerLength);
        if (at > 0) s = s.substring(0, at);
        return trimSeparators(s);
    }

    juce::String primaryKey(const juce::String& side) {
        return key(primaryText(side));
    }

    struct SplitName {
        juce::StringArray parts;
        juce::String endGroup;
        int year = 0;
        juce::String genreRemoved;
    };

    SplitName splitName(const juce::String& cleaned, const Context& context) {
        SplitName r;
        auto s = cleaned;

        // Nada de lo que se quita puede dejar el nombre sin sus dos partes: en "Prince - 1999"
        // el año es el título, y en "Grupo - Salsa" (con una categoría SALSA) también.
        const int partsBefore = splitParts(s).size();
        auto keepsParts = [partsBefore](const juce::String& candidate) {
            return partsBefore < 2 || splitParts(candidate).size() >= 2;
        };
        auto takeYear = [&] {
            auto candidate = s;
            const int year = takeTrailingYear(candidate);
            if (year != 0 && r.year == 0 && keepsParts(candidate)) {
                r.year = year;
                s = candidate;
            }
        };

        takeYear();
        juce::String rest, group;
        if (takeTrailingGroup(s, rest, group) && rest.isNotEmpty() && keepsParts(rest)) {
            r.endGroup = group;
            s = trimSeparators(rest);
            takeYear();
        }

        const auto all = splitParts(s);
        juce::StringArray kept, removed;
        for (const auto& part : all) {
            if ((isYear(part) && r.year == 0) || context.isGenre(part)) removed.add(part);
            else kept.add(part);
        }
        if (kept.size() < juce::jmin(2, all.size())) {
            kept = all;
            removed.clear();
        }
        for (const auto& part : removed) {
            if (isYear(part)) r.year = part.getIntValue();
            else if (r.genreRemoved.isEmpty()) r.genreRemoved = part;
        }
        r.parts = kept;
        if (r.parts.isEmpty() && s.isNotEmpty()) r.parts.add(s);
        return r;
    }

    juce::String canonical(const juce::String& name, const Context& context) {
        if (const auto* known = context.findKnown(key(name))) return known->name;
        return smartCase(name);
    }

    // Grupo de invitados "feat. X", "canta X": devuelve los nombres o vacío. ("Con X" no:
    // también empieza así un título como "Te Quiero (Con Todo Mi Corazón)".)
    juce::StringArray featuringGroup(const juce::String& group) {
        const auto k = key(group);
        for (auto* marker : { "featuring ", "feat ", "ft ", "canta ", "voz " }) {
            if (k.startsWith(marker)) {
                const auto lowered = lower(group);
                const int at = lowered.indexOf(juce::String(marker).trim());
                auto names = group.substring(at + (int) juce::String(marker).trim().length()).trim();
                names = names.trimCharactersAtStart(".: ").trim();
                return splitNames(names);
            }
        }
        return {};
    }

    void parseArtistSide(const juce::String& side, const Context& context, Proposal& p) {
        auto s = trimSeparators(side);
        if (s.isEmpty()) return;

        // Invitados explícitos: "Frenesí Orquesta, canta El Fantasma", "X feat. Y".
        int markerLength = 0;
        const int at = findFeaturing(s, markerLength);
        auto tail = at > 0 ? s.substring(at + markerLength - 1).trim().trimCharactersAtEnd(")").trim() : juce::String();
        if (tail.isNotEmpty()) {
            s = trimSeparators(s.substring(0, at).trimCharactersAtEnd("(").trimEnd());
            // "canta Eder Ortega MERENGUE": el género pegado al final del invitado.
            auto words = juce::StringArray::fromTokens(tail, " ", "");
            if (words.size() >= 3 && context.isGenre(words[words.size() - 1])) {
                if (p.genreRemoved.isEmpty()) p.genreRemoved = words[words.size() - 1];
                words.remove(words.size() - 1);
                tail = words.joinIntoString(" ");
            }
            p.featured.addArray(splitNames(tail));
        }

        // Grupo entre paréntesis pegado al artista: invitados o se deja.
        juce::String rest, group;
        if (takeTrailingGroup(s, rest, group) && rest.isNotEmpty()) {
            const auto guests = featuringGroup(group);
            if (!guests.isEmpty()) {
                p.featured.addArray(guests);
                s = trimSeparators(rest);
            }
        }

        // Género al final del artista ("Los Ocho de Colombia MERENGUE").
        auto words = juce::StringArray::fromTokens(s, " ", "");
        if (words.size() >= 3 && context.isGenre(words[words.size() - 1])) {
            if (p.genreRemoved.isEmpty()) p.genreRemoved = words[words.size() - 1];
            words.remove(words.size() - 1);
            s = words.joinIntoString(" ");
        }

        // "Eddy Herrera y Liz": se separa solo si una de las dos partes es un artista que ya
        // se conoce (o que aparece solo en otros archivos). "Sandy y Papo" queda entero.
        if (p.featured.isEmpty() && context.findKnown(key(s)) == nullptr && context.artistness(key(s)) < 2) {
            for (auto* sep : { " & ", " y ", " Y ", " and ", " x ", " X ", " con ", " Con ", " CON ", ", " }) {
                const int pos = s.indexOf(sep);
                if (pos <= 0) continue;
                const auto left = s.substring(0, pos).trim();
                const auto right = s.substring(pos + (int) strlen(sep)).trim();
                if (left.isEmpty() || right.isEmpty()) continue;
                if (context.artistness(key(left)) >= 1 || context.findKnown(key(right)) != nullptr) {
                    p.joinedArtist = s;
                    p.featuredGuessed = true;
                    p.featured.addArray(splitNames(right));
                    s = left;
                }
                break;
            }
        }
        // Un punto suelto al final ("Monchi Capricho."), pero no el de unas siglas ("T.L.F.").
        auto lastWord = s.fromLastOccurrenceOf(" ", false, false);
        if (lastWord.isEmpty()) lastWord = s;
        if (s.endsWithChar('.') && lastWord.length() >= 4 && lastWord.indexOfChar('.') == lastWord.length() - 1)
            s = s.dropLastCharacters(1).trimEnd();
        p.artist = s;
    }

    void parseTitleSide(const juce::String& side, Proposal& p) {
        auto s = trimSeparators(side);
        // Grupos al final: versión, invitados; lo demás es parte del título.
        juce::StringArray kept;
        for (int guard = 0; guard < 4; ++guard) {
            juce::String rest, group;
            if (!takeTrailingGroup(s, rest, group) || rest.isEmpty()) break;
            const auto guests = featuringGroup(group);
            if (!guests.isEmpty()) p.featured.addArray(guests);
            else if (isVersionText(key(group)) && p.version.isEmpty()) p.version = group;
            else kept.insert(0, "(" + group + ")");
            s = trimSeparators(rest);
        }
        // "Título feat. X" sin paréntesis.
        int markerLength = 0;
        const int at = findFeaturing(s, markerLength);
        if (at > 0 && s.substring(at, at + markerLength).containsIgnoreCase("feat")) {
            p.featured.addArray(splitNames(s.substring(at + markerLength - 1)));
            s = trimSeparators(s.substring(0, at));
        }
        if (!kept.isEmpty()) s << " " << kept.joinIntoString(" ");
        p.title = s.trim();
    }
}

//==============================================================================

juce::String lower(const juce::String& text) {
    juce::String out;
    out.preallocateBytes((size_t) text.getNumBytesAsUTF8());
    for (auto c : text) out += toLowerChar(c);
    return out;
}

juce::String upper(const juce::String& text) {
    juce::String out;
    out.preallocateBytes((size_t) text.getNumBytesAsUTF8());
    for (auto c : text) out += toUpperChar(c);
    return out;
}

juce::String key(const juce::String& text) {
    juce::String out;
    out.preallocateBytes((size_t) text.getNumBytesAsUTF8());
    bool pendingSpace = false;
    auto push = [&](juce::juce_wchar c) {
        if (pendingSpace && out.isNotEmpty()) out += ' ';
        pendingSpace = false;
        out += c;
    };
    for (auto c : text) {
        if (c == '\'' || c == 0x2019 || c == '`' || c == 0xB4) continue;  // "D'León" = "DLeón"
        if (c == '&') { pendingSpace = true; push('y'); pendingSpace = true; continue; }
        const auto f = foldChar(c);
        if (isDigit(f) || (f >= 'a' && f <= 'z') || (f > 0x7F && juce::CharacterFunctions::isLetterOrDigit(f)))
            push(f);
        else
            pendingSpace = true;
    }
    return out;
}

bool isShouting(const juce::String& text) {
    int upperCount = 0, lowerCount = 0;
    for (auto c : text) {
        if (isUpperLetter(c)) ++upperCount;
        else if (isLowerLetter(c)) ++lowerCount;
    }
    return upperCount + lowerCount >= 2 && (upperCount == 0 || lowerCount == 0);
}

juce::String titleCase(const juce::String& text) {
    const auto words = juce::StringArray::fromTokens(collapseSpaces(text), " ", "");
    juce::StringArray out;
    bool first = true;
    for (const auto& word : words) {
        const auto lowered = lower(word);
        const auto bare = word.retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
                                               + juce::String::fromUTF8("ÁÉÍÓÚÜÑáéíóúüñ"));
        const bool startsGroup = word.startsWithChar('(') || word.startsWithChar('[');

        if (word.containsAnyOf("0123456789")) {
            out.add(word);  // "UB40", "2x1"
        } else if (isRomanNumeral(upper(bare))) {
            out.add(upper(word));
        } else if ((bare.length() == 1 && word.endsWithChar('.')) || isDottedAcronym(word)) {
            out.add(upper(word));  // iniciales: "J. Wilson", "D.R.A."
        } else if (bare.length() >= 2 && bare.length() <= 4 && !hasVowel(bare) && !isSmallWord(lower(bare))) {
            out.add(upper(word));  // siglas sin vocales: "DJ", "MC", "NTVG"
        } else if (!first && !startsGroup && isSmallWord(lowered)) {
            out.add(lowered);
        } else {
            out.add(capitaliseWord(word));
        }
        first = word.endsWithChar(':') || word == "-";
    }
    return out.joinIntoString(" ");
}

juce::String smartCase(const juce::String& text) {
    return isShouting(text) ? titleCase(text) : collapseSpaces(text);
}

juce::String withAccentsFrom(const juce::String& ours, const juce::String& theirs) {
    if (ours.length() != theirs.length()) return ours;
    juce::String out;
    for (int i = 0; i < ours.length(); ++i) {
        const auto a = ours[i], b = theirs[i];
        if (foldChar(a) != foldChar(b)) return ours;  // no es el mismo texto
        // La letra de theirs (con su tilde) en la mayúscula o minúscula de ours.
        out += isUpperLetter(a) ? toUpperChar(b) : isLowerLetter(a) ? toLowerChar(b) : a;
    }
    return out;
}

void setDisplayUppercase(bool uppercase) { displayUppercase = uppercase; }
bool isDisplayUppercase() { return displayUppercase.load(); }

juce::String display(const juce::String& text) {
    return displayUppercase.load() ? upper(text) : text;
}

juce::String display(const std::string& text) {
    return display(juce::String::fromUTF8(text.c_str()));
}

//==============================================================================

juce::String creditLine(const juce::String& artist, const juce::StringArray& featured) {
    juce::StringArray guests;
    for (const auto& f : featured)
        if (f.trim().isNotEmpty()) guests.add(f.trim());
    if (guests.isEmpty()) return artist.trim();

    juce::String names;
    for (int i = 0; i < guests.size(); ++i) {
        if (i > 0) names << (i == guests.size() - 1 ? " & " : ", ");
        names << guests[i];
    }
    return artist.trim().isEmpty() ? names : artist.trim() + " feat. " + names;
}

juce::String displayTitle(const juce::String& title, const juce::String& version) {
    return version.trim().isEmpty() ? title.trim() : title.trim() + " (" + version.trim() + ")";
}

juce::String matchKey(const juce::String& artist, const juce::String& title) {
    const auto a = key(artist), t = key(title);
    return a.isEmpty() && t.isEmpty() ? juce::String() : a + "|" + t;
}

juce::StringArray splitNames(const juce::String& text) {
    auto s = " " + text.trim() + " ";
    for (auto* sep : { " & ", " / ", " - ", " y ", " Y ", " and ", ", ", "; " })
        s = s.replace(sep, "\n");
    auto names = juce::StringArray::fromLines(s);
    for (auto& n : names) n = trimSeparators(n);
    names.removeEmptyStrings();
    return names;
}

//==============================================================================

Context::Context(std::map<juce::String, KnownArtist> knownArtists, std::map<juce::String, juce::String> aliasKeys,
                 const juce::StringArray& categoryNames, const std::vector<Source>& batch)
    : known(std::move(knownArtists)), aliases(std::move(aliasKeys))
{
    for (auto* g : builtInGenres) genres.insert(g);
    for (const auto& name : categoryNames) {
        const auto k = key(name);
        if (k.isNotEmpty()) genres.insert(k);
    }

    for (const auto& source : batch) {
        juce::String leftText, rightText;
        if (source.hasTags()) {
            leftText = primaryText(source.tagArtist);
            rightText = source.tagTitle;
        } else {
            const auto split = splitName(cleanFileName(source.fileName), *this);
            if (split.parts.size() < 2) continue;
            leftText = primaryText(split.parts[0]);
            rightText = primaryText(split.parts[split.parts.size() - 1]);
        }
        const auto l = key(leftText), r = key(rightText);
        if (l.isEmpty() || r.isEmpty() || l == r) continue;
        partners[l].insert(r);
        partners[r].insert(l);
        batchNames.emplace(l, leftText);
        batchNames.emplace(r, rightText);
    }
}

const KnownArtist* Context::findKnown(const juce::String& nameKey) const {
    if (nameKey.isEmpty()) return nullptr;
    auto it = known.find(nameKey);
    if (it != known.end()) return &it->second;
    const auto alias = aliases.find(nameKey);
    if (alias != aliases.end()) {
        it = known.find(alias->second);
        if (it != known.end()) return &it->second;
    }
    return nullptr;
}

int Context::artistness(const juce::String& nameKey) const {
    if (nameKey.isEmpty()) return 0;
    int score = findKnown(nameKey) != nullptr ? 3 : 0;
    const auto it = partners.find(nameKey);
    if (it != partners.end()) score += juce::jmax(0, (int) it->second.size() - 1);
    return score;
}

bool Context::isGenre(const juce::String& text) const {
    const auto k = key(text);
    if (k.isEmpty()) return false;
    if (genres.count(k) > 0) return true;
    const auto words = juce::StringArray::fromTokens(k, " ", "");
    if (words.size() != 2) return false;
    auto isModifier = [](const juce::String& w) {
        for (auto* m : genreModifiers)
            if (w == m) return true;
        return false;
    };
    return (genres.count(words[0]) > 0 && isModifier(words[1])) || (genres.count(words[1]) > 0 && isModifier(words[0]));
}

namespace {
    int levenshtein(const std::string& a, const std::string& b) {
        std::vector<int> row(b.size() + 1);
        for (size_t j = 0; j <= b.size(); ++j) row[j] = (int) j;
        for (size_t i = 1; i <= a.size(); ++i) {
            int diagonal = row[0];
            row[0] = (int) i;
            for (size_t j = 1; j <= b.size(); ++j) {
                const int above = row[j];
                row[j] = std::min({ row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1) });
                diagonal = above;
            }
        }
        return row[b.size()];
    }

    juce::String withoutLeadingArticle(const juce::String& k) {
        for (auto* article : { "los ", "las ", "la ", "el ", "the ", "grupo ", "orquesta ", "banda ", "conjunto " })
            if (k.startsWith(article)) return k.substring((int) strlen(article));
        return k;
    }
}

bool similarKeys(const juce::String& a, const juce::String& b) {
    if (a == b || a.length() < 4 || b.length() < 4) return false;
    // Sin el artículo ("Hermanos Rosario"), sin espacios ("WILFRIDOVARGAS", "K.V.33") o con
    // una o dos letras de diferencia en un nombre largo.
    if (withoutLeadingArticle(a) == withoutLeadingArticle(b) || a.removeCharacters(" ") == b.removeCharacters(" ")) return true;
    const int length = juce::jmin(a.length(), b.length());
    const int maxDistance = length >= 12 ? 2 : length >= 6 ? 1 : 0;
    return maxDistance > 0 && std::abs(a.length() - b.length()) <= maxDistance
        && levenshtein(a.toStdString(), b.toStdString()) <= maxDistance;
}

juce::String Context::closestKnown(const juce::String& name) const {
    const auto k = key(name);
    if (k.length() < 4 || findKnown(k) != nullptr) return {};

    auto similar = [&k](const juce::String& other) { return similarKeys(k, other); };

    const KnownArtist* best = nullptr;
    for (const auto& [knownKey, artist] : known)
        if (similar(knownKey) && (best == nullptr || artist.tracks > best->tracks)) best = &artist;
    if (best != nullptr) return best->name;

    // En el mismo lote: la forma que más se repite con títulos distintos.
    const int own = artistness(k);
    juce::String bestName;
    int bestScore = 0;
    for (const auto& [otherKey, text] : batchNames) {
        if (otherKey == k) continue;
        const int score = artistness(otherKey);
        if (score < 1 || score <= own || score <= bestScore || !similar(otherKey)) continue;
        bestScore = score;
        bestName = smartCase(text);
    }
    return bestName;
}

std::vector<KnownArtist> Context::suggestions(const juce::String& typed, int maxResults) const {
    const auto k = key(typed);
    std::vector<KnownArtist> result;
    if (k.isEmpty()) return result;
    for (const auto& [knownKey, artist] : known)
        if (knownKey.startsWith(k) || knownKey.contains(" " + k) || withoutLeadingArticle(knownKey).startsWith(k))
            result.push_back(artist);
    std::stable_sort(result.begin(), result.end(), [](const KnownArtist& a, const KnownArtist& b) { return a.tracks > b.tracks; });
    if ((int) result.size() > maxResults) result.resize((size_t) maxResults);
    return result;
}

//==============================================================================

juce::String cleanFileName(const juce::String& fileName) {
    auto s = fileName.trim();

    // Sufijos de copias: "_521167018" (importaciones repetidas), " - copia", " (2)".
    for (int guard = 0; guard < 3; ++guard) {
        const int underscore = s.lastIndexOfChar('_');
        const auto digits = underscore >= 0 ? s.substring(underscore + 1) : juce::String();
        if (underscore > 0 && digits.length() >= 5 && digits.containsOnly("0123456789")) {
            s = s.substring(0, underscore).trimEnd();
            continue;
        }
        const auto lowered = lower(s);
        bool changed = false;
        for (auto* suffix : { " - copia", " - copy", " copia", " (copia)", " (copy)" }) {
            if (lowered.endsWith(suffix)) {
                s = s.dropLastCharacters((int) strlen(suffix)).trimEnd();
                changed = true;
                break;
            }
        }
        if (!changed && s.length() > 4 && s.endsWithChar(')') && s[s.length() - 4] == ' ' && s[s.length() - 3] == '('
            && isDigit(s[s.length() - 2])) {
            s = s.dropLastCharacters(4).trimEnd();
            changed = true;
        }
        if (!changed) break;
    }

    // Guiones bajos como espacios, si el nombre los usa en lugar de espacios.
    if (s.containsChar('_') && s.indexOfChar(' ') < 0) s = s.replaceCharacter('_', ' ');
    else s = s.replace("_-_", " - ");

    // Número de pista: "01 - ", "01. ", "01_", "07 ".
    {
        int i = 0;
        while (i < s.length() && i < 3 && isDigit(s[i])) ++i;
        if (i > 0 && i < s.length()) {
            int j = i;
            while (j < s.length() && s[j] == ' ') ++j;
            const auto sep = j < s.length() ? s[j] : 0;
            if (sep == '.' || sep == ')' || sep == '-' || sep == '_') {
                auto rest = s.substring(j + 1).trimStart();
                if (sep == '-' || rest.isNotEmpty()) s = rest;
            } else if (i == 2 && s[0] == '0' && j > i) {
                s = s.substring(j);
            }
        }
    }

    // Grupos que no son del nombre: "(Video Oficial)", "[HD]", direcciones web.
    juce::String cleaned;
    int depth = 0, groupStart = -1;
    for (int i = 0; i < s.length(); ++i) {
        const auto c = s[i];
        if ((c == '(' || c == '[' || c == '{') && depth++ == 0) groupStart = i;
        if (depth == 0) cleaned += c;
        if ((c == ')' || c == ']' || c == '}') && depth > 0 && --depth == 0) {
            const auto group = s.substring(groupStart, i + 1);
            if (!isJunkGroup(key(group))) cleaned += group;
        }
    }
    if (depth > 0) cleaned += s.substring(groupStart);  // paréntesis sin cerrar: se deja

    juce::StringArray words;
    for (const auto& w : juce::StringArray::fromTokens(cleaned, " ", "")) {
        const auto lw = lower(w);
        if (lw.startsWith("www.") || lw.endsWith(".com") || lw.endsWith(".net") || lw.endsWith(".org")
            || lw.contains(".com.") || lw.startsWith("http"))
            continue;
        words.add(w);
    }
    return trimSeparators(collapseSpaces(words.joinIntoString(" ")));
}

namespace {
    // Qué tanto parece un artista un lado del nombre. "Eddy Herrera y Liz" cuenta también lo
    // que se sepa de "Eddy Herrera".
    int sideArtistness(const juce::String& side, const Context& context) {
        const auto primary = primaryKey(side);
        int best = context.artistness(primary);
        for (auto* sep : { " y ", " x ", " and ", " con " }) {
            const int at = primary.indexOf(sep);
            if (at > 0) {
                best = juce::jmax(best, context.artistness(primary.substring(0, at)));
                break;
            }
        }
        return best;
    }
}

int orientationEvidence(const Source& source, const Context& context) {
    if (source.hasTags()) return 0;
    const auto split = splitName(cleanFileName(source.fileName), context);
    if (split.parts.size() < 2) return 0;
    return sideArtistness(split.parts[split.parts.size() - 1], context) - sideArtistness(split.parts[0], context);
}

std::map<juce::String, GroupFormat> detectFormats(const std::vector<Source>& batch, const Context& context) {
    std::map<juce::String, std::pair<int, int>> votes;  // grupo → (título–artista, artista–título)
    std::map<juce::String, int> files;
    for (const auto& source : batch) {
        if (source.hasTags()) { files[source.group]; continue; }
        ++files[source.group];
        const int evidence = orientationEvidence(source, context);
        auto& v = votes[source.group];
        if (evidence > 0) ++v.first;
        else if (evidence < 0) ++v.second;
    }

    std::map<juce::String, GroupFormat> formats;
    for (const auto& [group, count] : files) {
        const auto v = votes[group];
        GroupFormat f;
        f.files = count;
        f.orientation = v.first > v.second ? Orientation::TitleArtist : Orientation::ArtistTitle;
        f.confirming = juce::jmax(v.first, v.second);
        f.contradicting = juce::jmin(v.first, v.second);
        formats[group] = f;
    }
    return formats;
}

Proposal propose(const Source& source, Orientation orientation, const Context& context) {
    Proposal p;
    juce::String artistSide, titleSide;

    if (source.hasTags()) {
        artistSide = source.tagArtist;
        titleSide = source.tagTitle;
    } else {
        const auto split = splitName(cleanFileName(source.fileName), context);
        p.year = split.year;
        p.genreRemoved = split.genreRemoved;

        const auto& parts = split.parts;
        if (parts.size() == 1) {
            titleSide = parts[0];
        } else if (parts.size() >= 2) {
            juce::StringArray titleParts;
            if (orientation == Orientation::ArtistTitle) {
                artistSide = parts[0];
                for (int i = 1; i < parts.size(); ++i) titleParts.add(parts[i]);
            } else {
                artistSide = parts[parts.size() - 1];
                for (int i = 0; i < parts.size() - 1; ++i) titleParts.add(parts[i]);
            }
            titleSide = titleParts.joinIntoString(" - ");
        }

        // El paréntesis del final: versión, invitados o, si sigue al artista, compositores
        // ("La Flaca - Pochy y su Cocoband (Alfonso Vásquez)").
        if (split.endGroup.isNotEmpty()) {
            const auto guests = featuringGroup(split.endGroup);
            if (isVersionText(key(split.endGroup))) p.version = split.endGroup;
            else if (!guests.isEmpty()) p.featured.addArray(guests);
            else if (orientation == Orientation::TitleArtist && artistSide.isNotEmpty())
                p.composers = splitNames(split.endGroup);
            else titleSide << " (" << split.endGroup << ")";
        }
    }

    parseArtistSide(artistSide, context, p);
    parseTitleSide(titleSide, p);

    // Mayúsculas: el nombre como ya está en la biblioteca, o "Tipo Título" si viene gritado.
    p.artist = canonical(p.artist, context);
    for (auto& f : p.featured) f = canonical(f, context);
    for (auto& c : p.composers) c = canonical(c, context);
    p.featured.removeDuplicates(true);
    p.composers.removeDuplicates(true);
    p.title = smartCase(p.title);
    p.version = smartCase(p.version);
    if (p.joinedArtist.isNotEmpty()) p.joinedArtist = smartCase(p.joinedArtist);
    return p;
}

}
