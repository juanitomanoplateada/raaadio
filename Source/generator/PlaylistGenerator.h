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
        // Intérpretes (principal e invitados, por clave) de las pistas con créditos. Las demás
        // se leen del artista guardado ("Eddy Herrera feat. Liz").
        std::unordered_map<int, std::vector<std::string>> performersOf;
    };

    // Claves de los intérpretes de una pista: la separación de artistas no deja sonar
    // seguidas dos canciones que comparten alguno ("Eddy Herrera" y "Eddy Herrera feat. Liz").
    static std::vector<juce::String> performerKeys(const TrackInfo& track, const Pool& pool);

    // Lógica pura, sin base de datos (se prueba aparte).
    // Candidatas: pistas de las categorías incluidas (si no hay ninguna, las de
    // allCategoryIds y las que no tienen categoría), sin las excluidas, las que sonaron
    // hace menos de lastPlayedSeparation minutos ni las importadas por revisar.
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
