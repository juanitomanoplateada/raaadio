#pragma once
#include <JuceHeader.h>
#include <unordered_map>
#include "GeneratorSettings.h"
#include "../library/MusicLibrary.h"
#include "../library/Playlist.h"

class PlaylistGenerator {
public:
    PlaylistGenerator(MusicLibrary& library);


    struct GenerationResult {
        bool success;
        juce::String message;
        int tracksGenerated;
    };

    GenerationResult generate(const GeneratorSettings& settings, Playlist& currentPlaylist);

    // Lo que el generador necesita de la biblioteca, cargado de una vez (tres consultas)
    // para que las reglas no vuelvan a la base de datos por cada pista candidata.
    struct Pool {
        std::vector<TrackInfo> candidates;
        std::vector<TrackInfo> separators;
        std::unordered_map<int, std::vector<int>> categoriesOf;  // id de pista → categorías
    };

    // Lógica pura, sin base de datos (se prueba aparte).
    // Candidatas: pistas de las categorías incluidas (si no hay ninguna, las de
    // allCategoryIds y las que no tienen categoría), sin las excluidas ni las que sonaron
    // hace menos de lastPlayedSeparation minutos.
    static std::vector<TrackInfo> selectCandidates(const std::vector<TrackInfo>& allTracks,
                                                   const std::unordered_map<int, std::vector<int>>& categoriesOf,
                                                   const std::vector<int>& allCategoryIds,
                                                   const GeneratorSettings& settings, juce::Time now);
    // Secuencia que respeta las reglas de separación hasta llegar a la duración pedida.
    // errorMessage queda vacío si no hubo problemas.
    static std::vector<TrackInfo> buildSequence(const GeneratorSettings& settings, const Pool& pool,
                                                juce::Random& random, juce::String& errorMessage);

private:
    MusicLibrary& library;
    juce::Random random;

    Pool loadPool(const GeneratorSettings& settings);
};
