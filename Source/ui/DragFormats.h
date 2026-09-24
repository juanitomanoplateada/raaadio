#pragma once
#include <JuceHeader.h>

// Descripciones de los arrastres de pistas entre vistas:
// - Biblioteca y lista de cuñas: "track_list_" + rutas separadas por "|".
// - Una sola ruta: "track_path_" + ruta.
// - Cola: "playlist_drag_" + "índice:ruta" separados por "|".
namespace DragFormats {
    // Ruta de la primera pista del arrastre, o vacío si no es un arrastre de pistas.
    inline juce::String firstPath(const juce::String& description) {
        if (description.startsWith("track_list_"))
            return description.substring(11).upToFirstOccurrenceOf("|", false, false);
        if (description.startsWith("track_path_"))
            return description.substring(11);
        if (description.startsWith("playlist_drag_"))  // la ruta puede llevar ":" (D:\...)
            return description.substring(14).upToFirstOccurrenceOf("|", false, false).fromFirstOccurrenceOf(":", false, false);
        return {};
    }
}
