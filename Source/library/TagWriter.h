#pragma once
#include <JuceHeader.h>

// Etiquetas ID3v2.3 para un MP3 (lo que descarga la búsqueda en internet): artista, título,
// álbum, año, ISRC y de dónde vino. Los textos van en UTF-16, que todo reproductor lee con
// tildes y eñes. TagReader las lee al importar.
namespace TagWriter {

struct Tags {
    juce::String artist, title, album, isrc;
    int year = 0;
    juce::String source;  // enlace de origen (cuadro TXXX "SOURCE")
};

// La etiqueta completa, lista para ir al principio del archivo.
juce::MemoryBlock buildId3v2(const Tags& tags);

// Reemplaza la etiqueta ID3v2 del principio del MP3 (si tiene) por una con estos datos. El
// audio no se toca. false si el archivo no se pudo leer o escribir.
bool writeMp3(const juce::File& file, const Tags& tags);

}
