#include "ThemeManager.h"
#include "AppSettings.h"
#include "RaaadioLookAndFeel.h"

ThemeManager& ThemeManager::getInstance()
{
    static ThemeManager instance;
    return instance;
}

static juce::PropertiesFile::Options getSettingsOptions()
{
    return AppSettings::options();
}

void ThemeManager::initialise()
{
    juce::ApplicationProperties appProperties;
    appProperties.setStorageParameters(getSettingsOptions());

    if (auto* props = appProperties.getUserSettings())
    {
        juce::String savedTheme = props->getValue("ui.theme", "dark");
        currentTheme = (savedTheme == "light") ? Theme::Light : Theme::Dark;
    }
}

void ThemeManager::setTheme(Theme newTheme, bool persist)
{
    currentTheme = newTheme;

    if (persist)
    {
        juce::ApplicationProperties appProperties;
        appProperties.setStorageParameters(getSettingsOptions());

        if (auto* props = appProperties.getUserSettings())
        {
            props->setValue("ui.theme", (currentTheme == Theme::Light) ? "light" : "dark");
            props->saveIfNeeded();
        }
    }

    if (auto* lnf = dynamic_cast<RaaadioLookAndFeel*>(&juce::LookAndFeel::getDefaultLookAndFeel()))
        lnf->applyTheme();

    auto& desktop = juce::Desktop::getInstance();
    for (int i = 0; i < desktop.getNumComponents(); ++i)
    {
        if (auto* c = desktop.getComponent(i))
        {
            c->sendLookAndFeelChange();
            c->repaint();
        }
    }
}

ThemeManager::Theme ThemeManager::getTheme() const { return currentTheme; }

juce::String ThemeManager::getThemeName() const
{
    return (currentTheme == Theme::Light) ? "Light" : "Dark";
}

juce::Colour ThemeManager::getBaseBackgroundColour() const  { return juce::Colour(palette().background); }
juce::Colour ThemeManager::getPanelBackgroundColour() const { return juce::Colour(palette().surface); }
juce::Colour ThemeManager::getRaisedColour() const          { return juce::Colour(palette().raised); }
juce::Colour ThemeManager::getHoverColour() const           { return juce::Colour(palette().hover); }
juce::Colour ThemeManager::getOutlineColour() const         { return juce::Colour(palette().border); }
juce::Colour ThemeManager::getStrongOutlineColour() const   { return juce::Colour(palette().borderStrong); }

juce::Colour ThemeManager::getPrimaryTextColour() const     { return juce::Colour(palette().textPrimary); }
juce::Colour ThemeManager::getSecondaryTextColour() const   { return juce::Colour(palette().textSecondary); }
juce::Colour ThemeManager::getMutedTextColour() const       { return juce::Colour(palette().textMuted); }

juce::Colour ThemeManager::getHighlightColour() const       { return juce::Colour(palette().accent); }
juce::Colour ThemeManager::getAccentColour() const          { return getHighlightColour(); }
juce::Colour ThemeManager::getSelectionColour() const       { return getHighlightColour().withAlpha(currentTheme == Theme::Light ? 0.16f : 0.22f); }
juce::Colour ThemeManager::getSuccessColour() const         { return juce::Colour(palette().success); }
juce::Colour ThemeManager::getWarningColour() const         { return juce::Colour(palette().warning); }
juce::Colour ThemeManager::getErrorColour() const           { return juce::Colour(palette().danger); }

juce::Colour ThemeManager::getAirOnColour() const           { return getErrorColour(); }
juce::Colour ThemeManager::getCueOnColour() const           { return getWarningColour(); }
juce::Colour ThemeManager::getTalkoverOnColour() const      { return getHighlightColour(); }
juce::Colour ThemeManager::getAutoDjOnColour() const        { return getSuccessColour(); }

juce::Colour ThemeManager::getMeterGreenColour() const      { return juce::Colour(0xFF22C55E); }
juce::Colour ThemeManager::getMeterYellowColour() const     { return juce::Colour(0xFFEAB308); }
juce::Colour ThemeManager::getMeterRedColour() const        { return juce::Colour(0xFFEF4444); }
juce::Colour ThemeManager::getPlayheadColour() const        { return getPrimaryTextColour(); }
juce::Colour ThemeManager::getWaveformColour() const        { return juce::Colour(palette().waveform); }

juce::Colour ThemeManager::getSkipButtonColour() const      { return getRaisedColour(); }
juce::Colour ThemeManager::getPauseButtonColour() const     { return getWarningColour(); }
juce::Colour ThemeManager::getPlayButtonColour() const      { return getSuccessColour(); }
juce::Colour ThemeManager::getInfoButtonColour() const      { return getRaisedColour(); }
juce::Colour ThemeManager::getRemoveButtonColour() const    { return getRaisedColour(); }

juce::Colour ThemeManager::getContrastingTextColour(juce::Colour background) const
{
    // Un relleno translúcido deja ver la superficie: el texto normal del tema es el legible.
    if (background.getFloatAlpha() < 0.5f)
        return getPrimaryTextColour();

    const float brightness = 0.299f * background.getFloatRed()
                           + 0.587f * background.getFloatGreen()
                           + 0.114f * background.getFloatBlue();

    return brightness > 0.62f ? juce::Colour(0xFF0B0D10) : juce::Colour(0xFFF8FAFC);
}
