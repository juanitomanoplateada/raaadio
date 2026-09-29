#pragma once
#include <JuceHeader.h>

// Ejecuta un programa sin ventana y entrega su salida (la normal y la de errores, juntas)
// línea por línea. En Windows el programa corre dentro de un "job object": al cancelarlo o
// cuando termina, también se cierran los procesos que abrió (yt-dlp abre una copia de sí mismo
// y FFmpeg; matar solo el primero los dejaba descargando).
namespace ProcessRunner {

enum : int {
    couldNotStart = -1000,
    stopped = -1001,   // shouldStop() pidió cancelar
    timedOut = -1002,  // pasó silenceTimeoutMs sin escribir nada
};

using LineCallback = std::function<void(const juce::String& line)>;
using StopCheck = std::function<bool()>;

// Bloquea hasta que el programa termina. Devuelve su código de salida o uno de los de arriba.
// shouldStop se consulta varias veces por segundo (puede venir de otro hilo).
int run(const juce::StringArray& command, const LineCallback& onLine, const StopCheck& shouldStop,
        int silenceTimeoutMs = 5 * 60 * 1000);

// La línea de comandos de Windows para estos argumentos (comillas y barras como las lee
// CommandLineToArgvW).
juce::String commandLine(const juce::StringArray& command);

}
