#include "PlaylistGenerator.h"

PlaylistGenerator::PlaylistGenerator(MusicLibrary& lib) : library(lib) {}

PlaylistGenerator::GenerationResult PlaylistGenerator::generate(const GeneratorSettings& settings, Playlist& currentPlaylist) {
    GenerationResult result;

    juce::String error;
    Report report;
    fileChecked.clear();  // un disco pudo desconectarse desde que se abrió la ventana
    const auto pool = loadPool(settings, &currentPlaylist, true, &result.missingFiles);
    const auto generatedTracks = buildSequence(settings, pool, random, error, &report);
    if (generatedTracks.empty()) {
        result.message = error.isNotEmpty() && !(pool.candidates.empty() && result.missingFiles > 0)
                       ? error
                       : result.missingFiles > 0 ? TRANS("gen_err_missing_files") : TRANS("gen_err_no_tracks");
        return result;
    }

    if (settings.insertionMode == InsertionMode::Replace) {
        currentPlaylist.clear();
        currentPlaylist.insertTracks(generatedTracks, 0);
    }
    else if (settings.insertionMode == InsertionMode::AddToStart) {
        currentPlaylist.insertTracks(generatedTracks, 0);
    }
    else {
        // insertTracks() limita el índice al tamaño de la cola: esto agrega al final.
        currentPlaylist.insertTracks(generatedTracks, std::numeric_limits<int>::max());
    }

    result.success = true;
    result.tracksGenerated = (int) generatedTracks.size();
    result.separators = report.separators;
    result.songs = result.tracksGenerated - report.separators;
    for (const auto& t : generatedTracks) result.seconds += t.getAirDuration();
    result.reachedTarget = report.reachedTarget;
    result.repeatedSongs = report.repeatedSongs;
    result.relaxedRules = report.relaxed();
    result.tracks = generatedTracks;
    if (!report.reachedTarget) result.message = error;
    return result;
}

PlaylistGenerator::Availability PlaylistGenerator::check(const GeneratorSettings& settings, const Playlist& currentPlaylist) {
    Availability a;
    const auto pool = loadPool(settings, &currentPlaylist, true, &a.missingFiles);
    a.songs = (int) pool.candidates.size();
    for (const auto& t : pool.candidates) {
        a.seconds += t.getAirDuration();
        a.alreadyQueued += pool.queued.count(t.id) > 0 ? 1 : 0;
    }
    a.separators = (int) pool.separators.size();

    if (settings.lastPlayedSeparation > 0) {
        auto anyTime = settings;
        anyTime.lastPlayedSeparation = 0;
        a.recentlyPlayed = (int) loadPool(anyTime, nullptr, true).candidates.size() - a.songs;
    }
    return a;
}

bool PlaylistGenerator::fileExists(const TrackInfo& track) {
    const auto it = fileChecked.find(track.id);
    if (it != fileChecked.end()) return it->second;
    const bool exists = juce::File(juce::String::fromUTF8(track.path.c_str())).existsAsFile();
    fileChecked[track.id] = exists;
    return exists;
}

PlaylistGenerator::Pool PlaylistGenerator::loadPool(const GeneratorSettings& settings, const Playlist* queue, bool checkFiles,
                                                    int* missingFiles) {
    Pool pool;

    // Sin categorías elegidas vale toda la música: las identificaciones, cuñas y
    // separadores entran solo si se incluyen a propósito.
    std::vector<int> allCategoryIds;
    for (const auto& c : library.getCategories())
        if (!c.isSystem()) allCategoryIds.push_back(c.id);

    pool.categoriesOf = library.getAllTrackCategories();
    pool.performersOf = library.getPerformerKeys();
    const auto allTracks = library.search("", "");
    pool.candidates = selectCandidates(allTracks, pool.categoriesOf, allCategoryIds, settings, juce::Time::getCurrentTime());

    // Una canción cuyo archivo ya no está dejaría un hueco al aire: no se usa.
    if (checkFiles) {
        const auto before = pool.candidates.size();
        pool.candidates.erase(std::remove_if(pool.candidates.begin(), pool.candidates.end(),
                                             [this](const TrackInfo& t) { return !fileExists(t); }),
                              pool.candidates.end());
        if (missingFiles != nullptr) *missingFiles = (int) (before - pool.candidates.size());
    }

    if (settings.separatorCategoryId != -1) {
        for (const auto& t : allTracks) {
            const auto it = pool.categoriesOf.find(t.id);
            if (it != pool.categoriesOf.end()
                && std::find(it->second.begin(), it->second.end(), settings.separatorCategoryId) != it->second.end())
                pool.separators.push_back(t);
        }
    }

    // Lo que ya está en la cola: no se repite y, si el bloque va al final, las reglas de
    // separación siguen desde ahí. Al reemplazar la cola no cuenta.
    if (queue != nullptr && settings.insertionMode != InsertionMode::Replace) {
        const auto systemRoles = library.getSystemRolesByTrack();
        for (const auto& t : queue->getQueue()) {
            if (t.id == -1 || systemRoles.count(t.id) > 0) continue;  // cuñas, identificaciones, separadores
            pool.queued.insert(t.id);
            if (settings.insertionMode == InsertionMode::AddToEnd) pool.preceding.push_back(t);
        }
    }
    return pool;
}

std::vector<TrackInfo> PlaylistGenerator::selectCandidates(const std::vector<TrackInfo>& allTracks,
                                                           const std::unordered_map<int, std::vector<int>>& categoriesOf,
                                                           const std::vector<int>& allCategoryIds,
                                                           const GeneratorSettings& settings, juce::Time now) {
    // Sin categorías elegidas vale toda la música: la de allCategoryIds y la que no tiene
    // categoría (lo importado como GENERAL). Si se eligen categorías, solo esas.
    const bool allMusic = settings.includedCategories.empty();
    const auto& included = allMusic ? allCategoryIds : settings.includedCategories;

    auto uncategorized = [&categoriesOf](int trackId) {
        const auto it = categoriesOf.find(trackId);
        return it == categoriesOf.end() || it->second.empty();
    };
    auto inAny = [&categoriesOf](int trackId, const std::vector<int>& categories) {
        const auto it = categoriesOf.find(trackId);
        if (it == categoriesOf.end()) return false;
        for (int c : it->second)
            if (std::find(categories.begin(), categories.end(), c) != categories.end()) return true;
        return false;
    };

    std::vector<TrackInfo> result;
    for (const auto& t : allTracks) {
        // Lo importado que nadie ha revisado no sale al aire solo (sí a mano).
        if (t.isPending()) continue;
        // Sin duración no se puede programar (y el bloque nunca llegaría a su duración).
        if (t.getAirDuration() <= 0.0) continue;
        if (!inAny(t.id, included) && !(allMusic && uncategorized(t.id))) continue;
        if (inAny(t.id, settings.excludedCategories)) continue;

        // La última reproducción está en hora local (ver parseLocalTimestamp).
        if (settings.lastPlayedSeparation > 0 && !t.lastPlayed.empty()) {
            const auto lastPlayed = parseLocalTimestamp(t.lastPlayed);
            if (lastPlayed.toMilliseconds() != 0 && (now - lastPlayed).inMinutes() < settings.lastPlayedSeparation)
                continue;
        }
        result.push_back(t);
    }
    return result;
}

std::vector<juce::String> PlaylistGenerator::performerKeys(const TrackInfo& track, const Pool& pool) {
    std::vector<juce::String> keys;
    const auto it = pool.performersOf.find(track.id);
    if (it != pool.performersOf.end() && !it->second.empty()) {
        for (const auto& k : it->second) keys.push_back(juce::String::fromUTF8(k.c_str()));
        return keys;
    }
    // Sin créditos: el artista guardado, con sus invitados después de " feat. ".
    const auto credit = juce::String::fromUTF8(track.artist.c_str());
    keys.push_back(TrackNaming::key(credit.upToFirstOccurrenceOf(" feat. ", false, false)));
    if (credit.contains(" feat. "))
        for (const auto& guest : TrackNaming::splitNames(credit.fromFirstOccurrenceOf(" feat. ", false, false)))
            keys.push_back(TrackNaming::key(guest));
    return keys;
}

int PlaylistGenerator::feasibleCategorySeparation(int requested, const Pool& pool) {
    if (requested <= 0 || pool.candidates.empty()) return 0;
    std::unordered_map<int, int> songsIn;
    for (const auto& t : pool.candidates) {
        const auto it = pool.categoriesOf.find(t.id);
        if (it != pool.categoriesOf.end())
            for (int c : it->second) ++songsIn[c];
    }
    if (songsIn.empty()) return 0;
    int largest = 0;
    for (const auto& [category, count] : songsIn) largest = juce::jmax(largest, count);
    // Con una sola categoría no hay a cuál cambiar; y una categoría con un tercio de las
    // canciones solo puede sonar cada tres sin que las otras se repitan.
    const int byDistinct = (int) songsIn.size() - 1;
    const int byShare = (int) pool.candidates.size() / largest - 1;
    return juce::jlimit(0, requested, juce::jmin(byDistinct, byShare));
}

std::vector<TrackInfo> PlaylistGenerator::buildSequence(const GeneratorSettings& settings, const Pool& pool,
                                                        juce::Random& random, juce::String& errorMessage, Report* report) {
    errorMessage = {};
    Report ownReport;
    auto& rep = report != nullptr ? *report : ownReport;
    rep = {};

    std::vector<TrackInfo> generated;
    if (pool.candidates.empty()) {
        errorMessage = TRANS("gen_err_no_tracks");
        rep.reachedTarget = false;
        return generated;
    }

    static const std::vector<int> noCategories;
    auto categoriesOf = [&pool](int trackId) -> const std::vector<int>& {
        const auto it = pool.categoriesOf.find(trackId);
        return it != pool.categoriesOf.end() ? it->second : noCategories;
    };

    // Intérpretes y título (por clave) de las candidatas y de lo que suena antes del bloque.
    std::set<juce::String> distinctArtists, distinctTitles;
    std::unordered_map<int, std::vector<juce::String>> performers;
    std::unordered_map<int, juce::String> titleKeys;
    auto learn = [&](const TrackInfo& t) {
        if (performers.count(t.id) > 0) return;
        auto keys = performerKeys(t, pool);
        if (keys.empty()) keys.push_back({});
        performers[t.id] = std::move(keys);
        titleKeys[t.id] = TrackNaming::key(juce::String::fromUTF8(t.title.c_str()));
    };
    for (const auto& t : pool.candidates) {
        learn(t);
        distinctArtists.insert(performers[t.id].front());
        distinctTitles.insert(titleKeys[t.id]);
    }
    for (const auto& t : pool.preceding) learn(t);

    auto sharePerformer = [&performers](const TrackInfo& a, const TrackInfo& b) {
        for (const auto& x : performers[a.id])
            for (const auto& y : performers[b.id])
                if (x.isNotEmpty() && x == y) return true;
        return false;
    };

    // Una separación no puede pedir más variedad de la que hay entre las candidatas.
    auto feasible = [](int separation, size_t distinctValues) {
        return juce::jmin(separation, juce::jmax(0, (int) distinctValues - 1));
    };
    const int artistSeparation = feasible(settings.artistSeparation, distinctArtists.size());
    const int titleSeparation = feasible(settings.titleSeparation, distinctTitles.size());
    const int categorySeparation = feasibleCategorySeparation(settings.categorySeparation, pool);

    // Solo música: los separadores no cuentan para las reglas. Empieza con lo que ya hay en
    // la cola, para que el bloque no repita el artista de la última canción que tiene delante.
    std::vector<TrackInfo> history = pool.preceding;
    auto clashesWithRecent = [&history](int count, const std::function<bool(const TrackInfo&)>& clash) {
        const int n = juce::jmin((int) history.size(), count);
        for (int i = 0; i < n; ++i)
            if (clash(history[history.size() - 1 - (size_t) i])) return true;
        return false;
    };
    auto passes = [&](const TrackInfo& candidate, int artistSep, int titleSep, int categorySep) {
        if (artistSep > 0 && clashesWithRecent(artistSep, [&](const TrackInfo& p) { return sharePerformer(candidate, p); }))
            return false;
        if (titleSep > 0) {
            const auto& title = titleKeys[candidate.id];
            if (clashesWithRecent(titleSep, [&](const TrackInfo& p) { return title == titleKeys[p.id]; })) return false;
        }
        if (categorySep > 0) {
            const auto& categories = categoriesOf(candidate.id);
            if (clashesWithRecent(categorySep, [&](const TrackInfo& p) {
                    for (int c : categoriesOf(p.id))
                        if (std::find(categories.begin(), categories.end(), c) != categories.end()) return true;
                    return false;
                }))
                return false;
        }
        return true;
    };

    // La bolsa: cada candidata sale una vez antes de repetir. La primera no trae lo que ya
    // está en la cola (si con eso queda vacía, vale todo).
    std::vector<TrackInfo> bag;
    auto shuffle = [&random](std::vector<TrackInfo>& tracks) {
        for (int i = (int) tracks.size() - 1; i > 0; --i)
            std::swap(tracks[(size_t) i], tracks[(size_t) random.nextInt(i + 1)]);
    };
    for (const auto& t : pool.candidates)
        if (pool.queued.count(t.id) == 0) bag.push_back(t);
    shuffle(bag);
    // Volver a llenarla: lo que todavía no salió queda adelante, para que salga primero.
    auto refill = [&] {
        std::set<int> left;
        for (const auto& t : bag) left.insert(t.id);
        std::vector<TrackInfo> rest;
        for (const auto& t : pool.candidates)
            if (left.count(t.id) == 0) rest.push_back(t);
        shuffle(rest);
        bag.insert(bag.end(), rest.begin(), rest.end());
    };
    auto pick = [&](int artistSep, int titleSep, int categorySep) {
        return std::find_if(bag.begin(), bag.end(), [&](const TrackInfo& t) { return passes(t, artistSep, titleSep, categorySep); });
    };

    const double targetSeconds = settings.targetSeconds();
    const bool useSeparators = settings.separatorInterval > 0 && !pool.separators.empty();
    int sinceSeparator = settings.separatorInterval;  // el bloque empieza con un separador
    int lastSeparator = -1;
    double duration = 0.0;
    std::set<int> used = pool.queued;

    while (duration < targetSeconds) {
        if ((int) generated.size() >= maxRows) {
            errorMessage = TRANS("gen_err_too_long");
            rep.reachedTarget = false;
            break;
        }

        if (useSeparators && sinceSeparator >= settings.separatorInterval) {
            // Al azar, pero no el mismo dos veces seguidas si hay más de uno.
            auto index = random.nextInt((int) pool.separators.size());
            if (pool.separators.size() > 1 && pool.separators[(size_t) index].id == lastSeparator)
                index = (index + 1 + random.nextInt((int) pool.separators.size() - 1)) % (int) pool.separators.size();
            const auto& separator = pool.separators[(size_t) index];
            lastSeparator = separator.id;
            generated.push_back(separator);
            duration += separator.getAirDuration();
            ++rep.separators;
            sinceSeparator = 0;
            continue;
        }

        if (bag.empty()) refill();

        // Lo primero que cumpla todo. Si nada de la bolsa cumple: se cede en la categoría; si
        // ni así, se vuelve a llenar la bolsa (repetir canciones, con la separación de
        // títulos); y con la bolsa llena se acercan los artistas y, al final, los títulos.
        auto found = pick(artistSeparation, titleSeparation, categorySeparation);
        if (found == bag.end() && categorySeparation > 0) {
            found = pick(artistSeparation, titleSeparation, 0);
            if (found != bag.end()) ++rep.relaxedCategory;
        }
        if (found == bag.end() && bag.size() < pool.candidates.size()) {
            refill();
            continue;
        }
        if (found == bag.end()) {
            for (int a = artistSeparation - 1; a >= 0 && found == bag.end(); --a) found = pick(a, titleSeparation, 0);
            if (found != bag.end()) ++rep.relaxedArtist;
        }
        if (found == bag.end()) {
            for (int t = titleSeparation - 1; t >= 0 && found == bag.end(); --t) found = pick(0, t, 0);
            if (found != bag.end()) ++rep.relaxedTitle;
        }
        if (found == bag.end()) found = bag.begin();  // sin reglas siempre hay una

        const TrackInfo track = *found;
        bag.erase(found);
        if (!used.insert(track.id).second) ++rep.repeatedSongs;
        generated.push_back(track);
        history.push_back(track);
        duration += track.getAirDuration();
        ++sinceSeparator;
    }
    return generated;
}
