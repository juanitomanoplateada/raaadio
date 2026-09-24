#pragma once
#include <JuceHeader.h>

// Estilo visual de toda la aplicación. Se instala como LookAndFeel por defecto en
// Main.cpp y toma sus colores de ThemeManager; applyTheme() lo actualiza cuando
// el usuario cambia entre tema claro y oscuro.
//
// Los botones aceptan dos propiedades opcionales (ver UiStyle::setVariant/setIcon):
//   "variant": "primary" | "success" | "warning" | "danger" | "ghost"
//   "icon":    nombre de un icono de UiStyle::icon()
// y los botones con estado, "toggleStyle" = "soft" para un encendido tenue
// (fondo translúcido del color de encendido en lugar de relleno sólido).
class RaaadioLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RaaadioLookAndFeel();

    void applyTheme();

    // Botones
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool isHighlighted, bool isDown) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool isHighlighted, bool isDown) override;
    void drawTickBox(juce::Graphics&, juce::Component&, float x, float y, float w, float h,
                     bool ticked, bool isEnabled, bool isHighlighted, bool isDown) override;

    // Deslizadores
    int getSliderThumbRadius(juce::Slider&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle, juce::Slider&) override;

    // Barras de desplazamiento
    int getDefaultScrollbarWidth() override;
    void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height,
                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                       bool isMouseOver, bool isMouseDown) override;

    // Campos de texto y combos
    void fillTextEditorBackground(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawTextEditorOutline(juce::Graphics&, int width, int height, juce::TextEditor&) override;
    void drawComboBox(juce::Graphics&, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;

    // Menús emergentes
    void drawPopupMenuBackground(juce::Graphics&, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked, bool hasSubMenu,
                           const juce::String& text, const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;
    juce::Font getPopupMenuFont() override;
    void getIdealPopupMenuItemSize(const juce::String& text, bool isSeparator, int standardMenuItemHeight,
                                   int& idealWidth, int& idealHeight) override;

    // Tablas
    void drawTableHeaderBackground(juce::Graphics&, juce::TableHeaderComponent&) override;
    void drawTableHeaderColumn(juce::Graphics&, juce::TableHeaderComponent&, const juce::String& columnName,
                               int columnId, int width, int height, bool isMouseOver,
                               bool isMouseDown, int columnFlags) override;

    // Pestañas
    int getTabButtonBestWidth(juce::TabBarButton&, int tabDepth) override;
    int getTabButtonOverlap(int tabDepth) override;
    juce::Font getTabButtonFont(juce::TabBarButton&, float height) override;
    void drawTabButton(juce::TabBarButton&, juce::Graphics&, bool isMouseOver, bool isMouseDown) override;
    void drawTabbedButtonBarBackground(juce::TabbedButtonBar&, juce::Graphics&) override;
    void drawTabAreaBehindFrontButton(juce::TabbedButtonBar&, juce::Graphics&, int w, int h) override;

    // Contenedores y varios
    void drawGroupComponentOutline(juce::Graphics&, int w, int h, const juce::String& text,
                                   const juce::Justification&, juce::GroupComponent&) override;
    void drawProgressBar(juce::Graphics&, juce::ProgressBar&, int width, int height,
                         double progress, const juce::String& textToShow) override;
    void drawStretchableLayoutResizerBar(juce::Graphics&, int w, int h, bool isVerticalBar,
                                         bool isMouseOver, bool isMouseDragging) override;

    // Diálogos de alerta
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;
    int getAlertWindowButtonHeight() override;

private:
    juce::Colour resolveButtonFill(juce::Button&, juce::Colour fromButton) const;
    void drawIconAndText(juce::Graphics&, juce::Rectangle<float> area, const juce::String& iconName,
                         const juce::String& text, const juce::Font& font, juce::Colour colour,
                         juce::Justification justification = juce::Justification::centred);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RaaadioLookAndFeel)
};
