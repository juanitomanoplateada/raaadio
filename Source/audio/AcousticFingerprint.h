#pragma once
#include <JuceHeader.h>
#include <cstdint>
#include <vector>

// Huella acústica para reconocer la misma grabación aunque venga en otro formato, con otro
// nombre o a otro volumen (MP3 de 128 kbps y OGG de la misma canción). Diez segundos de la
// canción, a partir de su punto de entrada más 30 s (o el 30 % si es corta), se reducen a
// 33 bandas entre 300 y 2000 Hz; cada 23 ms queda un entero de 32 bits con el signo de cómo
// cambia la energía de una banda a la siguiente y de un instante al siguiente (el método de
// Haitsma y Kalker). Unos 1,7 kB por canción.
namespace AcousticFingerprint {

constexpr int version = 1;

// Lo que se considera la misma grabación: al menos el 60 % de los cambios coinciden (una
// copia en otro formato ronda el 90 %; dos canciones distintas, el 20 o 30 %).
constexpr double sameRecording = 0.6;

std::vector<uint32_t> compute(juce::AudioFormatReader& reader, double cueIn, double airDuration,
                              const std::function<bool()>& shouldAbort = {});

// Parecido entre dos huellas: 1 si son iguales, poco si no tienen nada que ver.
// Se busca el mejor desfase de hasta un segundo (el punto de entrada varía unos milisegundos
// entre formatos). 0 si alguna está vacía o se solapan muy poco.
double similarity(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b);

}
