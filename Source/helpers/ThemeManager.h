#pragma once
#include <JuceHeader.h>

// Paleta semántica de la interfaz. Los componentes piden colores por su función
// (superficie, borde, texto, al aire...) y nunca por un valor fijo, para que el
// tema claro y el oscuro se vean correctos sin código especial en cada vista.
class ThemeManager
{
public:
    enum class Theme
    {
        Light,
        Dark
    };

    static ThemeManager& getInstance();

    void initialise();
    // persist = false cambia el tema solo en memoria (lo usa el modo de captura de UI).
    void setTheme(Theme newTheme, bool persist = true);
    Theme getTheme() const;
    juce::String getThemeName() const;

    // Superficies
    juce::Colour getBaseBackgroundColour() const;   // fondo de la ventana
    juce::Colour getPanelBackgroundColour() const;  // tarjetas y paneles
    juce::Colour getRaisedColour() const;           // controles, cabeceras, campos
    juce::Colour getHoverColour() const;            // control bajo el puntero
    juce::Colour getOutlineColour() const;          // bordes suaves
    juce::Colour getStrongOutlineColour() const;    // bordes de controles

    // Texto
    juce::Colour getPrimaryTextColour() const;
    juce::Colour getSecondaryTextColour() const;
    juce::Colour getMutedTextColour() const;

    // Acento y estados
    juce::Colour getHighlightColour() const;
    juce::Colour getAccentColour() const;
    juce::Colour getSelectionColour() const;
    juce::Colour getSuccessColour() const;
    juce::Colour getWarningColour() const;
    juce::Colour getErrorColour() const;

    // Semántica de radio
    juce::Colour getAirOnColour() const;
    juce::Colour getCueOnColour() const;
    juce::Colour getTalkoverOnColour() const;
    juce::Colour getAutoDjOnColour() const;
    juce::Colour getMeterGreenColour() const;
    juce::Colour getMeterYellowColour() const;
    juce::Colour getMeterRedColour() const;
    juce::Colour getPlayheadColour() const;
    juce::Colour getWaveformColour() const;

    // Botones de transporte
    juce::Colour getSkipButtonColour() const;
    juce::Colour getPauseButtonColour() const;
    juce::Colour getPlayButtonColour() const;
    juce::Colour getInfoButtonColour() const;
    juce::Colour getRemoveButtonColour() const;

    // Texto legible sobre un relleno dado (blanco sobre colores oscuros, casi negro sobre claros).
    juce::Colour getContrastingTextColour(juce::Colour background) const;

    // Alias históricos
    juce::Colour getBackgroundColour() const { return getBaseBackgroundColour(); }
    juce::Colour getTextColour() const { return getPrimaryTextColour(); }

private:
    ThemeManager() = default;
    ~ThemeManager() = default;

    struct Palette
    {
        juce::uint32 background, surface, raised, hover, border, borderStrong;
        juce::uint32 textPrimary, textSecondary, textMuted;
        juce::uint32 accent, success, warning, danger;
        juce::uint32 waveform;
    };

    static constexpr Palette darkPalette {
        0xFF0F1115, 0xFF171A21, 0xFF1F232C, 0xFF2A303B, 0xFF262B35, 0xFF363D4A,
        0xFFE8EAED, 0xFFA3A9B5, 0xFF6B7280,
        0xFF38BDF8, 0xFF10B981, 0xFFF59E0B, 0xFFEF4444,
        0xFF38BDF8
    };

    static constexpr Palette lightPalette {
        0xFFEEF0F3, 0xFFFFFFFF, 0xFFF1F3F6, 0xFFE3E7EC, 0xFFDDE1E7, 0xFFC5CBD4,
        0xFF111827, 0xFF4B5563, 0xFF8B93A1,
        0xFF0284C7, 0xFF059669, 0xFFD97706, 0xFFDC2626,
        0xFF0284C7
    };

    const Palette& palette() const { return currentTheme == Theme::Light ? lightPalette : darkPalette; }

    Theme currentTheme = Theme::Dark;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeManager)
};
