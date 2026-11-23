#pragma once
#include <JuceHeader.h>

// Resultado de analizar un archivo completo, una sola vez (al importar, o en segundo
// plano para lo que se importó con versiones anteriores). Se guarda en la base de datos
// para que el deck no tenga que adivinar nada mientras la pista está al aire.
struct TrackAnalysis {
    bool valid = false;
    double lengthSeconds = 0.0;
    double loudnessLufs = -70.0;  // sonoridad integrada EBU R 128 (ITU-R BS.1770)
    double truePeakDb = -70.0;    // pico real (dBTP), con sobremuestreo
    double cueIn = 0.0;           // segundos: donde empieza el sonido
    double cueOut = 0.0;          // segundos: donde termina la pista para la radio
    double mixPoint = 0.0;        // segundos: desde aquí solo queda el final que se desvanece
};

namespace TrackAnalyzer {
    // Subirla obliga a reanalizar la biblioteca (MusicLibrary lo hace en segundo plano).
    // 2: punto de mezcla.
    constexpr int version = 2;

    // Normalización: todas las pistas a la misma sonoridad percibida. Una pista suave solo
    // se sube hasta que su pico real llega al techo (el limitador maestro recorta en seco,
    // no conviene empujarlo), y nunca más de maxBoostDb.
    constexpr double targetLufs = -16.0;
    constexpr double peakCeilingDb = -1.0;
    constexpr double maxBoostDb = 12.0;

    // Puntos de entrada y salida. Nivel de ventanas de 20 ms, el canal más fuerte.
    // - Silencio de cabeza y cola: por debajo de trimThresholdDb.
    // - Un hueco de silencio digital (por debajo de gapThresholdDb) de al menos
    //   minGapSeconds, después de los primeros protectedSeconds de sonido, termina la
    //   pista: así una "pista oculta" tras minutos de silencio no deja la radio muda.
    //   Las pausas musicales normales duran mucho menos y no se tocan.
    constexpr double trimThresholdDb = -50.0;
    constexpr double gapThresholdDb = -60.0;
    constexpr double minGapSeconds = 6.0;
    constexpr double protectedSeconds = 30.0;

    // Punto de mezcla: el último momento en que el nivel, promediado en mixWindowSeconds,
    // llega a mixDropDb por debajo de la sonoridad de la pista. Lo que sigue es el final que
    // se desvanece (la cola de un acorde, el fundido de la canción, el ruido de fondo de una
    // grabación), y ahí puede entrar la siguiente sin tapar nada: en una voz, eso es después
    // de su última palabra. Como mucho maxMixLeadSeconds antes del punto de salida.
    constexpr double mixDropDb = 18.0;
    constexpr double mixWindowSeconds = 0.2;
    constexpr double maxMixLeadSeconds = 10.0;

    // Decodifica el archivo completo tal como lo reproduce el deck (dos canales; un
    // archivo mono se duplica). Si shouldAbort devuelve true, se abandona (valid = false).
    TrackAnalysis analyse(juce::AudioFormatReader& reader, const std::function<bool()>& shouldAbort = {});

    // Ganancia lineal que lleva la pista a targetLufs respetando el techo de pico.
    double gainFor(double loudnessLufs, double truePeakDb);

    // Puntos de entrada y salida a partir del nivel (lineal) de ventanas consecutivas.
    // Separado de analyse() para poder probarlo sin archivos.
    void findCuePoints(const std::vector<float>& windowLevels, double windowSeconds,
                       double lengthSeconds, double& cueIn, double& cueOut);

    // Punto de mezcla, con los niveles de findCuePoints y sus puntos de entrada y salida.
    double findMixPoint(const std::vector<float>& windowLevels, double windowSeconds,
                        double loudnessLufs, double cueIn, double cueOut);
}
