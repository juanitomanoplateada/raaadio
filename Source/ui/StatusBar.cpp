#include "StatusBar.h"
#include "BinaryData.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

StatusBar::StatusBar(AudioEngine& engine, AutoDJ& adj, RecordingManager& recordingManager,
                     DeadAirGuard& guard, StreamEncoder& stream)
    : audioEngine(engine), autoDJ(adj), deadAirGuard(guard), streamEncoder(stream),
      recordingPanel(recordingManager, engine)
{
    logo = juce::ImageCache::getFromMemory(BinaryData::RAAADIO_ICON_png, BinaryData::RAAADIO_ICON_pngSize);

    addAndMakeVisible(recordingPanel);

    addAndMakeVisible(settingsButton);
    UiStyle::setVariant(settingsButton, "ghost");
    UiStyle::setIcon(settingsButton, "settings");
    settingsButton.onClick = [this] { if (onSettingsRequested) onSettingsRequested(); };

    lookAndFeelChanged();
    refreshState();
    startTimer(250);
}

StatusBar::~StatusBar()
{
    stopTimer();
}

void StatusBar::lookAndFeelChanged()
{
    settingsButton.setButtonText(TRANS("settings"));
    resized();
    repaint();
}

void StatusBar::timerCallback()
{
    refreshState();
}

void StatusBar::refreshState()
{
    auto& a = audioEngine.getDeck(0);
    auto& b = audioEngine.getDeck(1);
    const bool aOnAir = a.isPlaying() && a.isRoutedToMain();
    const bool bOnAir = b.isPlaying() && b.isRoutedToMain();

    juce::String playing;
    if (aOnAir || bOnAir)
    {
        auto& current = (aOnAir && bOnAir) ? (a.getLastPlayTime() > b.getLastPlayTime() ? a : b)
                                           : (aOnAir ? a : b);
        const auto track = current.getCurrentTrack();
        const juce::String artist(track.artist), title(track.title);
        playing = artist.isNotEmpty() ? artist + juce::String::fromUTF8("  \xe2\x80\x94  ") + title : title;
    }

    const auto now = juce::Time::getCurrentTime();
    const auto clock = now.formatted("%H:%M:%S");
    const auto date = now.formatted("%d/%m/%Y");

    const bool alarm = deadAirGuard.isAlarmActive();
    const bool blink = (juce::Time::getMillisecondCounter() / 500) % 2 == 0;
    const auto stream = streamEncoder.getStatus();

    const bool changed = (aOnAir || bOnAir) != onAir || autoDJ.isEnabled() != autoMode
                      || playing != nowPlaying || clock != clockText || date != dateText
                      || alarm != deadAir || (alarm && blink != blinkOn) || stream != streamStatus;

    onAir = aOnAir || bOnAir;
    autoMode = autoDJ.isEnabled();
    deadAir = alarm;
    blinkOn = blink;
    streamStatus = stream;
    nowPlaying = playing;
    clockText = clock;
    dateText = date;

    if (changed)
        repaint();
}

juce::String StatusBar::getTooltip()
{
    const auto mouse = getMouseXYRelative().toFloat();
    if (streamStatus != StreamEncoder::Status::Off && streamChipArea.contains(mouse))
        return streamStatus == StreamEncoder::Status::Error ? streamEncoder.getLastError() : TRANS("tip_stream_status");
    if (deadAir && stateArea.toFloat().contains(mouse))
        return TRANS("tip_dead_air");
    return {};
}

void StatusBar::resized()
{
    auto area = getLocalBounds().reduced(UiStyle::padding, 0);

    brandArea = area.removeFromLeft(150);

    settingsButton.setBounds(area.removeFromRight(juce::jmax(140, settingsButton.getBestWidthForHeight(34) + 8))
                                 .withSizeKeepingCentre(juce::jmax(132, settingsButton.getBestWidthForHeight(34)), 34));
    area.removeFromRight(UiStyle::gap);

    clockArea = area.removeFromRight(128);
    area.removeFromRight(UiStyle::gap * 2);

    recordingPanel.setBounds(area.removeFromRight(390).withSizeKeepingCentre(390, 38));
    area.removeFromRight(UiStyle::gap * 2);

    stateArea = area;
}

void StatusBar::paint(juce::Graphics& g)
{
    auto& t = ThemeManager::getInstance();
    auto bounds = getLocalBounds();

    g.setColour(t.getPanelBackgroundColour());
    g.fillRect(bounds);
    g.setColour(t.getOutlineColour());
    g.fillRect(bounds.removeFromBottom(1));

    // Marca
    auto brand = brandArea.toFloat();
    auto logoArea = brand.removeFromLeft(34.0f).withSizeKeepingCentre(34.0f, 34.0f);
    if (logo.isValid())
    {
        juce::Path clip;
        clip.addEllipse(logoArea);
        g.saveState();
        g.reduceClipRegion(clip);
        g.drawImage(logo, logoArea, juce::RectanglePlacement::centred);
        g.restoreState();
    }
    brand.removeFromLeft(10.0f);
    g.setColour(t.getPrimaryTextColour());
    g.setFont(juce::Font(juce::FontOptions(19.0f, juce::Font::bold).withKerningFactor(0.04f)));
    g.drawText("RAAADIO", brand, juce::Justification::centredLeft, false);

    // Separadores verticales
    g.setColour(t.getOutlineColour());
    g.fillRect(stateArea.getX() - UiStyle::gap, 12, 1, getHeight() - 24);
    g.fillRect(clockArea.getX() - UiStyle::gap, 12, 1, getHeight() - 24);

    // Estado de la emisora
    auto state = stateArea.toFloat().withSizeKeepingCentre((float) stateArea.getWidth(), 26.0f);
    state.removeFromLeft(4.0f);

    const auto airText = onAir ? TRANS("status_on_air") : TRANS("status_off_air");
    const float airWidth = onAir
        ? UiStyle::drawChip(g, state, airText, t.getAirOnColour(), juce::Colours::white, true)
        : UiStyle::drawChip(g, state, airText, t.getRaisedColour(), t.getMutedTextColour(), true);
    state.removeFromLeft(airWidth + 8.0f);

    const auto modeText = autoMode ? TRANS("status_auto") : TRANS("status_manual");
    const auto modeColour = autoMode ? t.getSuccessColour() : t.getWarningColour();
    const float modeWidth = UiStyle::drawChip(g, state, modeText, modeColour.withAlpha(0.18f),
                                              modeColour.interpolatedWith(t.getPrimaryTextColour(), 0.2f));
    state.removeFromLeft(modeWidth + 8.0f);

    // Silencio al aire (protección contra silencio): rojo intermitente.
    if (deadAir)
    {
        const auto fill = t.getErrorColour().withAlpha(blinkOn ? 1.0f : 0.35f);
        const float w = UiStyle::drawChip(g, state, TRANS("status_dead_air"), fill, juce::Colours::white, true);
        state.removeFromLeft(w + 8.0f);
    }

    // Transmisión por internet
    streamChipArea = {};
    if (streamStatus != StreamEncoder::Status::Off)
    {
        const bool connected = streamStatus == StreamEncoder::Status::Connected;
        const auto colour = connected ? t.getSuccessColour()
                          : streamStatus == StreamEncoder::Status::Connecting ? t.getWarningColour() : t.getErrorColour();
        const auto text = connected ? TRANS("status_stream_on")
                        : streamStatus == StreamEncoder::Status::Connecting ? TRANS("status_stream_connecting")
                                                                            : TRANS("status_stream_error");
        const float w = UiStyle::drawChip(g, state, text, colour.withAlpha(0.18f),
                                          colour.interpolatedWith(t.getPrimaryTextColour(), 0.2f), true);
        streamChipArea = state.withWidth(w);
        state.removeFromLeft(w + 8.0f);
    }
    state.removeFromLeft(6.0f);

    if (nowPlaying.isNotEmpty())
    {
        g.setColour(t.getPrimaryTextColour());
        g.setFont(UiStyle::semibold(15.0f));
        g.drawText(nowPlaying, state, juce::Justification::centredLeft, true);
    }

    // Reloj
    auto clock = clockArea.toFloat();
    auto dateRow = clock.removeFromTop(clock.getHeight() * 0.5f).withTrimmedTop(6.0f);
    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::caption(11.0f));
    g.drawText(dateText, dateRow, juce::Justification::bottomRight, false);
    g.setColour(t.getPrimaryTextColour());
    g.setFont(UiStyle::mono(22.0f, true));
    g.drawText(clockText, clock.withTrimmedBottom(4.0f), juce::Justification::topRight, false);
}
