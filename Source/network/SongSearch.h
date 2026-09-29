#pragma once
#include <JuceHeader.h>
#include "HttpTransport.h"
#include "../database/Database.h"

// Buscar una canción en línea y su enlace de YouTube. Deezer da los datos bien escritos
// (artista, título, álbum, duración, ISRC; su API pública no pide clave) y con ellos se busca
// la misma grabación en YouTube Music, y en YouTube si allá no aparece. Solo se piden datos y
// enlaces: nunca se descarga audio. Lógica pura: arma los pedidos, lee las respuestas y compara;
// quien se conecta es SongSearcher.
namespace SongSearch {

// Una canción del catálogo de Deezer.
struct Track {
    juce::int64 id = 0;
    juce::String title;         // con la versión: "Burbujas de Amor (En Vivo)"
    juce::String titleShort;    // sin ella: "Burbujas de Amor"
    juce::String version;       // "(En Vivo)", vacío si no tiene
    juce::String artist;        // intérprete principal
    juce::StringArray featured; // invitados (solo en el detalle de la canción)
    juce::String album, isrc, link, coverUrl;
    int duration = 0;           // segundos
    int year = 0;               // solo en el detalle
    int rank = 0;               // popularidad en Deezer
    bool explicitLyrics = false;
};

// Un video de YouTube o una canción de YouTube Music.
struct Video {
    enum class Kind { Unknown, OfficialAudio, OfficialVideo, UserUpload };
    juce::String id, title, artists, album, channel, views;
    int duration = 0;           // segundos; 0 si no se sabe
    Kind kind = Kind::Unknown;
    bool fromMusic = false;     // de YouTube Music (si no, de la búsqueda de YouTube)
    double score = 0.0;         // qué tanto se parece a la canción buscada, de 0 a 1
};

//==============================================================================
// Deezer

juce::String deezerSearchUrl(const juce::String& query, int limit = 25);
juce::String deezerTrackUrl(juce::int64 id);
// Resultados de la búsqueda. error: el mensaje del servicio si respondió con error (p. ej.,
// "Quota limit exceeded").
std::vector<Track> parseDeezerSearch(const juce::String& json, juce::String& error);
// El detalle de una canción completa lo que la búsqueda no trae: año e invitados.
bool parseDeezerTrack(const juce::String& json, Track& track);

//==============================================================================
// YouTube (la API interna que usa su propia página: la búsqueda no pide clave ni cuenta)

HttpTransport::Request youTubeMusicSearch(const juce::String& query, const juce::String& language);
HttpTransport::Request youTubeSearch(const juce::String& query, const juce::String& language);
std::vector<Video> parseYouTubeMusicSearch(const juce::String& json);
std::vector<Video> parseYouTubeSearch(const juce::String& json);
// "4:09" → 249; "1:02:03" → 3723; 0 si no es una duración.
int parseDuration(const juce::String& text);

juce::String youTubeUrl(const juce::String& videoId);
juce::String youTubeMusicUrl(const juce::String& videoId);
// Sin la API de Spotify (pide cuenta Premium): la búsqueda en su página.
juce::String spotifySearchUrl(const Track& track);
juce::String youTubeSearchPageUrl(const Track& track);
// Lo que se busca en YouTube para una canción de Deezer.
juce::String youTubeQuery(const Track& track);

//==============================================================================
// Comparar

// Qué tanto se parece un video a la canción: título (sin la versión), artista, duración y si
// es el audio oficial. Una versión distinta (en vivo, karaoke, remix...) baja mucho; otro
// título es otra canción.
double matchScore(const Track& track, const Video& video);
constexpr double goodMatch = 0.8;      // "Coincide"
constexpr double possibleMatch = 0.55; // "Parecida"; menos, "Dudosa" (otro intérprete, covers)
constexpr double otherSong = 0.25;     // menos: otra canción, no se muestra
// Pone el puntaje a cada video, quita las otras canciones y los ordena de más a menos parecido.
void rank(const Track& track, std::vector<Video>& videos);

// La canción que ya está en la biblioteca con ese artista y título (aunque se escriba distinto:
// "Juan Luis Guerra 4.40", sin tildes...); id -1 si no está.
int findInLibrary(const Track& track, const std::vector<Database::KeyRow>& library);

}
