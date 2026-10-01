#pragma once
#include <JuceHeader.h>
#include <set>
#include <unordered_map>
#include "GeneratorSettings.h"
#include "../library/MusicLibrary.h"
#include "../library/Playlist.h"

// Arma un bloque de música para la cola con reglas de radio: no repetir canciones, separar
// artistas y categorías, no usar lo que sonó hace poco, separadores cada tantas canciones.
//
// Las reglas son preferencias con un orden: lo primero es llegar a la duración pedida sin
// repetir canciones; después, separar artistas; después, cambiar de categoría. Si la biblioteca
// no alcanza para todo, cede primero la categoría, luego se permite repetir canciones (con la
// separación de títulos) y por último se acercan los artistas. El resultado dice cuánto cedió.
class PlaylistGenerator {
public:
    PlaylistGenerator(MusicLibrary& library);

    struct GenerationResult {
        bool success = false;
        juce::String message;         // por qué no se pudo, si !success
        int tracksGenerated = 0;      // filas agregadas a la cola (canciones y separadores)
        int songs = 0, separators = 0;
        double seconds = 0.0;         // lo que dura al aire
        bool reachedTarget = true;    // false: se quedó corta (límite de filas)
        int repeatedSongs = 0;        // canciones que salen otra vez (en el bloque o ya en la cola)
        int relaxedRules = 0;         // canciones que quedaron más cerca de lo que piden las reglas
        int missingFiles = 0;         // candidatas cuyo archivo no está: no se usaron
        std::vector<TrackInfo> tracks;  // lo agregado, en orden
    };

    GenerationResult generate(const GeneratorSettings& settings, Playlist& currentPlaylist);

    // Con qué cuenta el generador para estos ajustes (para mostrarlo antes de generar).
    struct Availability {
        int songs = 0;            // canciones que pueden salir
        double seconds = 0.0;     // cuánto duran en total
        int separators = 0;       // piezas en la categoría de separadores
        int recentlyPlayed = 0;   // las que quedan fuera por haber sonado hace poco
        int alreadyQueued = 0;    // de las que pueden salir, las que ya están en la cola
        int missingFiles = 0;     // las que quedan fuera porque su archivo no está
    };
    Availability check(const GeneratorSettings& settings, const Playlist& currentPlaylist);

    // Lo que el generador necesita de la biblioteca, cargado de una vez (tres consultas)
    // para que las reglas no vuelvan a la base de datos por cada pista candidata.
    struct Pool {
        std::vector<TrackInfo> candidates;
        std::vector<TrackInfo> separators;
        std::unordered_map<int, std::vector<int>> categoriesOf;  // id de pista → categorías
        // Intérpretes (principal e invitados, por clave) de las pistas con créditos. Las demás
        // se leen del artista guardado ("Eddy Herrera feat. Liz").
        std::unordered_map<int, std::vector<std::string>> performersOf;
        // Lo que va a sonar justo antes del bloque (la cola, si se agrega al final): las reglas
        // de separación también cuentan contra eso.
        std::vector<TrackInfo> preceding;
        // Pistas que ya están en la cola: no se usan mientras queden otras.
        std::set<int> queued;
    };

    // Claves de los intérpretes de una pista: la separación de artistas no deja sonar
    // seguidas dos canciones que comparten alguno ("Eddy Herrera" y "Eddy Herrera feat. Liz").
    static std::vector<juce::String> performerKeys(const TrackInfo& track, const Pool& pool);

    // Lógica pura, sin base de datos (se prueba aparte).
    // Candidatas: pistas de las categorías incluidas (si no hay ninguna, las de
    // allCategoryIds y las que no tienen categoría), sin las excluidas, las que sonaron
    // hace menos de lastPlayedSeparation minutos, las importadas por revisar ni las que no
    // tienen duración.
    static std::vector<TrackInfo> selectCandidates(const std::vector<TrackInfo>& allTracks,
                                                   const std::unordered_map<int, std::vector<int>>& categoriesOf,
                                                   const std::vector<int>& allCategoryIds,
                                                   const GeneratorSettings& settings, juce::Time now);

    // Cuánto tuvo que ceder buildSequence.
    struct Report {
        int relaxedCategory = 0, relaxedArtist = 0, relaxedTitle = 0;
        int repeatedSongs = 0;
        int separators = 0;
        bool reachedTarget = true;
        int relaxed() const { return relaxedCategory + relaxedArtist + relaxedTitle; }
    };
    // Secuencia que llega a la duración pedida respetando las reglas hasta donde la biblioteca
    // alcanza. errorMessage solo queda con texto si no hay con qué generar o se llegó al
    // límite de filas.
    static std::vector<TrackInfo> buildSequence(const GeneratorSettings& settings, const Pool& pool,
                                                juce::Random& random, juce::String& errorMessage, Report* report = nullptr);
    // La separación de categorías que la biblioteca permite sin repetir canciones: si una
    // categoría tiene la mitad de las canciones, no se puede cambiar de categoría en cada una.
    static int feasibleCategorySeparation(int requested, const Pool& pool);

    static constexpr int maxRows = 3000;  // una cola más larga no es práctica

private:
    MusicLibrary& library;
    juce::Random random;
    // Si el archivo de cada pista está: se mira una vez (el resumen se recalcula con cada
    // cambio en la ventana) y de nuevo justo antes de generar.
    std::unordered_map<int, bool> fileChecked;
    bool fileExists(const TrackInfo& track);

    Pool loadPool(const GeneratorSettings& settings, const Playlist* queue, bool checkFiles, int* missingFiles = nullptr);
};
