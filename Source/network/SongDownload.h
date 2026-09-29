#pragma once
#include <JuceHeader.h>
#include "DownloadTools.h"
#include "SongSearch.h"
#include "../library/TagWriter.h"

// Descargar el audio de un video de YouTube a MP3 con yt-dlp: los argumentos, cómo leer lo que
// escribe y qué hacer con sus errores. Lógica pura; quien lo corre es SongDownloader.
//
// Cuidar a YouTube es parte del diseño: un video a la vez, al menos 20 s entre uno y otro, no
// más de 20 por hora, pocos reintentos y, si YouTube dice que se pidió demasiado o pide
// "confirmar que no es un robot", se para todo durante un buen rato en vez de insistir (insistir
// es lo que alarga el bloqueo).
namespace SongDownload {

constexpr int secondsBetween = 20;
constexpr int perHour = 20;

// Id de un video de YouTube: 11 letras, números, "-" o "_" (así no puede colarse otro argumento).
bool isValidVideoId(const juce::String& id);

// yt-dlp: el mejor audio, convertido a MP3 VBR de máxima calidad, en outputFolder/<id>.mp3.
// Sin la configuración del usuario de yt-dlp, con pocos reintentos y la ruta final impresa.
juce::StringArray arguments(const DownloadTools::Paths& tools, const juce::String& videoId, const juce::File& outputFolder);
// yt-dlp se actualiza solo (YouTube cambia seguido y las versiones viejas dejan de funcionar).
juce::StringArray updateArguments(const DownloadTools::Paths& tools);

// "[raaadio]  45.3%" → 0.453; -1 si la línea no es de progreso.
double parseProgress(const juce::String& line);
// La ruta que yt-dlp imprime al terminar (una línea que es la ruta de un .mp3), o vacía.
juce::String parseFinalPath(const juce::String& line);

// Lo que salió mal, leído de la salida de yt-dlp.
enum class Problem {
    None,
    RateLimited,    // "try again later", HTTP 429: se pidió demasiado (dura hasta una hora)
    BotCheck,       // "Sign in to confirm you're not a bot": la conexión quedó marcada
    Unavailable,    // borrado, privado o no disponible
    AgeRestricted,  // pide iniciar sesión para confirmar la edad
    Region,         // no disponible en el país
    Network,        // sin conexión, tiempo agotado
    Tools,          // falta FFmpeg u otro programa
    Other
};
Problem classify(const juce::String& output);
// Cuánto esperar antes de volver a pedirle algo a YouTube después de ese problema (0: no hace
// falta parar la fila).
int pauseMinutes(Problem problem);
juce::String problemKey(Problem problem);  // para guardar y traducir: "rate_limited"...
Problem problemFromKey(const juce::String& key);

// Nombre del archivo en la biblioteca: "Artista - Título".
juce::String fileName(const SongSearch::Track& track);
// Las etiquetas del MP3: los datos de Deezer y el enlace del video.
TagWriter::Tags tagsFor(const SongSearch::Track& track, const juce::String& videoId);

// Cuántos segundos faltan para poder empezar otra descarga, dados los comienzos de la última
// hora (en cualquier orden). minGapSeconds: la pausa mínima desde la anterior.
int secondsToWait(const juce::Array<juce::Time>& recentStarts, juce::Time now, int minGapSeconds = secondsBetween);

}
