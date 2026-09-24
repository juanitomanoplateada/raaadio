#pragma once
#include <JuceHeader.h>

// Lectura de artista y título desde las etiquetas del archivo:
// - MP3: ID3v2 (2.2, 2.3 y 2.4) al inicio, y si no hay, ID3v1 al final.
// - FLAC y Ogg Vorbis: comentarios Vorbis (se leen aquí porque JUCE convierte mal los
//   textos con tildes de los Ogg).
// - WAV: bloque LIST/INFO, tal como lo entrega el lector de JUCE.
// Otros formatos (M4A, WMA) no tienen lectura de etiquetas: se usa el nombre del archivo.
namespace TagReader {
    struct Tags {
        juce::String artist;
        juce::String title;
        bool isEmpty() const { return artist.isEmpty() && title.isEmpty(); }
    };

    Tags read(const juce::File& file, const juce::StringPairArray& readerMetadata = {});

    // Partes separadas para las pruebas.
    // data empieza en "ID3"; devuelve también cuántos bytes ocupa la etiqueta.
    Tags parseId3v2(const juce::uint8* data, size_t size, size_t* tagLength = nullptr);
    Tags parseId3v1(const juce::uint8* data, size_t size);  // los últimos 128 bytes
    // Cuerpo de un comentario Vorbis: proveedor, número de campos y "CLAVE=valor".
    Tags parseVorbisComment(const juce::uint8* data, size_t size);
    Tags parseFlac(const juce::uint8* data, size_t size);   // data empieza en "fLaC"
    Tags parseOgg(const juce::uint8* data, size_t size);    // data empieza en "OggS"
}
