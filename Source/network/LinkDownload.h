#pragma once
#include <JuceHeader.h>
#include "DownloadTools.h"
#include "SongDownload.h"

// Bajar el audio de un enlace pegado (Facebook, Instagram, X, TikTok, YouTube... cualquier sitio
// que yt-dlp entienda) para un audio temporal: validar el enlace, los argumentos de yt-dlp, leer
// lo que escribe y qué decir cuando falla. Lógica pura; quien lo corre es SongDownloader, en la
// misma fila que las canciones: con su ritmo, su cancelación (job object) y su FFmpeg, que pasa
// el audio a MP3 (AudioConverter).
//
// Sin cookies ni sesiones: RAAADIO no usa la cuenta del navegador del operador. Lo privado o lo
// que pide iniciar sesión (Instagram y Facebook lo hacen seguido) falla con un error claro.
namespace LinkDownload {

// Lo pegado, como enlace "https://...". Del texto se toma el primer enlace (un mensaje de
// WhatsApp trae texto alrededor); sin "https://" se acepta "www..." o "instagram.com/...".
// Vacío si no es un enlace web: otro esquema, caracteres de control o comillas, sin dominio.
juce::String normalise(const juce::String& pasted);

// "Facebook", "Instagram", "X", "TikTok", "YouTube", "SoundCloud"... o el dominio ("vimeo.com").
juce::String siteName(const juce::String& url);
// ¿Es de YouTube? Esas descargas siguen el ritmo y las pausas de YouTube. Acepta un enlace o el
// id de 11 caracteres de un video (lo que anota la búsqueda de canciones).
bool isYouTube(const juce::String& urlOrVideoId);

// yt-dlp: solo el mejor audio (o el video, si el sitio no da el audio aparte), el primero si la
// publicación tiene varios y nada en vivo, en outputFolder/<id>.<ext>. Imprime el título y el
// autor antes de bajar y la ruta final al terminar. El enlace va después de "--": nunca se
// toma por una opción.
juce::StringArray arguments(const DownloadTools::Paths& tools, const juce::String& url, const juce::File& outputFolder);

// Lo que yt-dlp imprime: "[raaadio-title] <JSON>", "[raaadio-author] <JSON>" (vacío si la línea
// no es esa, o si yt-dlp no lo sabe) y la ruta final del archivo, de cualquier extensión.
juce::String parseTitle(const juce::String& line);
juce::String parseAuthor(const juce::String& line);
juce::String parseFinalPath(const juce::String& line);

// Lo que salió mal: primero lo propio de las redes (sesión, sin audio, en vivo, sitio que yt-dlp
// no conoce); lo demás, como en las canciones.
SongDownload::Problem classify(const juce::String& output);

// El nombre del temporal: el título en una línea, sin enlaces y corto (un tuit entero no cabe en
// la lista). Sin título, el autor; sin autor, el sitio.
juce::String cleanTitle(const juce::String& title, const juce::String& author, const juce::String& site);

}
