#pragma once
#include <JuceHeader.h>

// Piezas visuales compartidas por todas las vistas: tipografía, iconos, medidas y
// pequeños elementos de dibujo. Mantenerlas aquí evita que cada componente
// invente sus propios tamaños y colores.
namespace UiStyle
{
    // Medidas
    constexpr int gap = 8;               // separación entre paneles
    constexpr int padding = 12;          // margen interior de un panel
    constexpr float panelRadius = 8.0f;
    constexpr float controlRadius = 6.0f;
    constexpr int rowHeight = 28;        // filas de tablas y listas
    constexpr int headerHeight = 30;     // cabeceras de tablas
    constexpr int controlHeight = 32;    // botones, campos y combos

    // Tipografía
    juce::Font ui(float height, bool bold = false);
    juce::Font semibold(float height);
    juce::Font mono(float height, bool bold = false);
    juce::Font caption(float height = 11.5f);   // rótulos en mayúsculas

    // Iconos vectoriales: "play", "pause", "stop", "skip", "loop", "eject", "info",
    // "settings", "folder", "record", "mic", "auto", "import", "search", "refresh",
    // "plus", "sparkle", "music", "drop", "chevron-up", "chevron-down", "close",
    // "broadcast", "headphones", "list", "history", "megaphone", "grid", "lock", "globe", "link".
    juce::Path icon(const juce::String& name, juce::Rectangle<float> area);
    void drawIcon(juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour);

    // Botones: variante visual ("primary", "success", "warning", "danger", "ghost")
    // e icono opcional. El LookAndFeel los lee de las propiedades del botón.
    void setVariant(juce::Button& button, const juce::String& variant);
    void setIcon(juce::Button& button, const juce::String& iconName);

    // Dibujo
    void drawPanel(juce::Graphics& g, juce::Rectangle<float> area,
                   juce::Colour borderOverride = {}, float borderWidth = 1.0f);
    // Etiqueta redondeada ("AL AIRE", "SIGUIENTE"...). Devuelve el ancho usado.
    float drawChip(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
                   juce::Colour fill, juce::Colour textColour, bool withDot = false);
    float getChipWidth(const juce::String& text, float height, bool withDot = false);
    void drawSectionTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text);
    void drawEmptyState(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& iconName,
                        const juce::String& title, const juce::String& subtitle);

    // Formatos
    // Mayúsculas que también convierten vocales con tilde y eñes (String::toUpperCase
    // no lo hace en Windows y deja "LOCUCIóN").
    juce::String toUpper(const juce::String& text);
    juce::String formatTime(double seconds);           // mm:ss (h:mm:ss si pasa de una hora)
    juce::String formatLongDuration(double seconds);   // "1 h 52 min" / "12 min"

    // Configuración común de tablas
    void styleTable(juce::TableListBox& table);
}
