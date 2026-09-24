#pragma once
#include <JuceHeader.h>

// Claves que se guardan en archivos de la emisora (la del servidor de streaming). En
// Windows se cifran con DPAPI para el usuario actual: el archivo y sus respaldos no las
// muestran, y otro usuario u otro equipo no las puede leer (allí hay que volver a
// escribirlas). Fuera de Windows se guardan sin cifrar.
namespace SecretStore {

// "dpapi:<base64>" (o "plain:<texto>" donde no hay cifrado). Vacío si secret está vacío.
juce::String protect(const juce::String& secret);

// Lo contrario de protect(). False si no se puede descifrar: se guardó con otro usuario
// de Windows o en otro equipo, o el texto está dañado.
bool unprotect(const juce::String& stored, juce::String& secret);

}
