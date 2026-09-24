#include "PlaylistGenerator.h"
#include <set>

PlaylistGenerator::PlaylistGenerator(MusicLibrary& lib) : library(lib) {}

PlaylistGenerator::GenerationResult PlaylistGenerator::generate(const GeneratorSettings& settings, Playlist& currentPlaylist) {
    GenerationResult result;
    result.success = false;
    result.tracksGenerated = 0;

    juce::String error;
    const auto generatedTracks = buildSequence(settings, loadPool(settings), random, error);
    if (generatedTracks.empty()) {
        result.message = error.isNotEmpty() ? error : TRANS("gen_err_no_tracks");
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
    result.message = "Generated " + juce::String(generatedTracks.size()) + " tracks.";
    return result;
}

PlaylistGenerator::Pool PlaylistGenerator::loadPool(const GeneratorSettings& settings) {
    Pool pool;

    // Sin categorías elegidas vale toda la música: las identificaciones, cuñas y
    // separadores entran solo si se incluyen a propósito.
    std::vector<int> allCategoryIds;
    for (const auto& c : library.getCategories())
        if (!c.isSystem()) allCategoryIds.push_back(c.id);

    pool.categoriesOf = library.getAllTrackCategories();
    const auto allTracks = library.search("", "");
    pool.candidates = selectCandidates(allTracks, pool.categoriesOf, allCategoryIds, settings, juce::Time::getCurrentTime());

    if (settings.separatorCategoryId != -1) {
        for (const auto& t : allTracks) {
            const auto it = pool.categoriesOf.find(t.id);
            if (it != pool.categoriesOf.end()
                && std::find(it->second.begin(), it->second.end(), settings.separatorCategoryId) != it->second.end())
                pool.separators.push_back(t);
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

std::vector<TrackInfo> PlaylistGenerator::buildSequence(const GeneratorSettings& settings, const Pool& pool,
                                                        juce::Random& random, juce::String& errorMessage) {
    errorMessage = {};
    std::vector<TrackInfo> generated;
    if (pool.candidates.empty()) {
        errorMessage = TRANS("gen_err_no_tracks");
        return generated;
    }

    static const std::vector<int> noCategories;
    auto categoriesOf = [&pool](int trackId) -> const std::vector<int>& {
        const auto it = pool.categoriesOf.find(trackId);
        return it != pool.categoriesOf.end() ? it->second : noCategories;
    };

    // Una separación no puede pedir más valores distintos de los que hay entre las
    // candidatas: con una sola categoría, o menos artistas que la separación de artistas,
    // la regla dejaba al generador atascado a las pocas pistas.
    std::set<int> distinctCategories;
    std::set<juce::String> distinctArtists, distinctTitles;
    for (const auto& t : pool.candidates) {
        for (int c : categoriesOf(t.id)) distinctCategories.insert(c);
        distinctArtists.insert(juce::String(t.artist).toUpperCase());
        distinctTitles.insert(juce::String(t.title).toUpperCase());
    }
    auto feasible = [](int separation, size_t distinctValues) {
        return juce::jmin(separation, juce::jmax(0, (int) distinctValues - 1));
    };
    const int artistSeparation = feasible(settings.artistSeparation, distinctArtists.size());
    const int titleSeparation = feasible(settings.titleSeparation, distinctTitles.size());
    const int categorySeparation = feasible(settings.categorySeparation, distinctCategories.size());

    std::vector<TrackInfo> history;  // solo música: los separadores no cuentan para las reglas
    auto clashesWithRecent = [&history](int count, const std::function<bool(const TrackInfo&)>& clash) {
        const int n = juce::jmin((int) history.size(), count);
        for (int i = 0; i < n; ++i)
            if (clash(history[history.size() - 1 - (size_t) i])) return true;
        return false;
    };
    auto passesRules = [&](const TrackInfo& candidate) {
        const juce::String artist(candidate.artist), title(candidate.title);
        if (artistSeparation > 0
            && clashesWithRecent(artistSeparation, [&](const TrackInfo& p) { return artist.equalsIgnoreCase(juce::String(p.artist)); }))
            return false;
        if (titleSeparation > 0
            && clashesWithRecent(titleSeparation, [&](const TrackInfo& p) { return title.equalsIgnoreCase(juce::String(p.title)); }))
            return false;
        if (categorySeparation > 0) {
            const auto& categories = categoriesOf(candidate.id);
            if (clashesWithRecent(categorySeparation, [&](const TrackInfo& p) {
                    for (int c : categoriesOf(p.id))
                        if (std::find(categories.begin(), categories.end(), c) != categories.end()) return true;
                    return false;
                }))
                return false;
        }
        return true;
    };

    const double targetSeconds = settings.targetDurationVal * (settings.targetDurationIsHours ? 3600.0 : 60.0);
    const bool useSeparators = settings.separatorInterval > 0 && !pool.separators.empty();
    int sinceSeparator = settings.separatorInterval;  // el bloque empieza con un separador
    double duration = 0.0;
    std::vector<TrackInfo> bag;  // cada candidata sale una vez antes de repetir

    for (int iteration = 0; duration < targetSeconds; ++iteration) {
        if (iteration > 5000) {
            errorMessage = TRANS("gen_err_timeout");
            break;
        }

        if (useSeparators && sinceSeparator >= settings.separatorInterval) {
            const auto& separator = pool.separators[(size_t) random.nextInt((int) pool.separators.size())];
            generated.push_back(separator);
            duration += separator.getAirDuration();
            sinceSeparator = 0;
            continue;
        }

        if (bag.empty()) {
            bag = pool.candidates;
            for (int i = (int) bag.size() - 1; i > 0; --i)
                std::swap(bag[(size_t) i], bag[(size_t) random.nextInt(i + 1)]);
        }

        const auto found = std::find_if(bag.begin(), bag.end(), passesRules);
        if (found == bag.end()) {
            // Lo que queda en la bolsa no cumple las reglas: se vuelve a llenar. Si ni la
            // bolsa llena las cumple, no hay forma de seguir.
            if (bag.size() < pool.candidates.size()) {
                bag.clear();
                continue;
            }
            errorMessage = TRANS("gen_err_stuck");
            break;
        }

        const TrackInfo track = *found;
        bag.erase(found);
        generated.push_back(track);
        history.push_back(track);
        duration += track.getAirDuration();
        ++sinceSeparator;
    }
    return generated;
}
