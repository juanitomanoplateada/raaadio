#include "MixerComponent.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

// Rango visible de los medidores, en dBFS.
static constexpr float meterMinDb = -54.0f;
static constexpr float meterMaxDb = 6.0f;

MixerComponent::MixerComponent(Mixer& m, Deck& dA, Deck& dB, AutoDJ& adj)
    : mixer(m), autoDJ(adj), deckA(dA), deckB(dB)
{
    auto configFader = [this](juce::Slider& s, int channel) {
        addAndMakeVisible(s);
        s.setSliderStyle(juce::Slider::LinearVertical);
        s.setRange(-60.0, 12.0);
        s.setSkewFactorFromMidPoint(0.0);
        s.setValue(0.0, juce::dontSendNotification);
        s.setDoubleClickReturnValue(true, 0.0);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.getProperties().set("fill", "start");
        s.onValueChange = [this, channel, &s] {
            mixer.setChannelVolume(channel, (float)s.getValue());
            repaint(channel == 0 ? readoutA : readoutB);
        };
    };
    configFader(volSliderA, 0);
    configFader(volSliderB, 1);

    auto configRoute = [this](juce::TextButton& b, const juce::String& icon) {
        addAndMakeVisible(b);
        b.setClickingTogglesState(true);
        b.getProperties().set("toggleStyle", "soft");
        UiStyle::setIcon(b, icon);
    };
    configRoute(airA, "broadcast");
    configRoute(cueA, "headphones");
    configRoute(airB, "broadcast");
    configRoute(cueB, "headphones");

    auto updateRoutingA = [this]() { deckA.setRouting(airA.getToggleState(), cueA.getToggleState()); };
    airA.onClick = updateRoutingA;
    cueA.onClick = updateRoutingA;
    airA.setToggleState(true, juce::dontSendNotification);
    deckA.setRouting(true, false);

    auto updateRoutingB = [this]() { deckB.setRouting(airB.getToggleState(), cueB.getToggleState()); };
    airB.onClick = updateRoutingB;
    cueB.onClick = updateRoutingB;
    airB.setToggleState(true, juce::dontSendNotification);
    deckB.setRouting(true, false);

    addAndMakeVisible(crossfader);
    crossfader.setSliderStyle(juce::Slider::LinearHorizontal);
    crossfader.setRange(0.0, 1.0);
    crossfader.setValue(0.5, juce::dontSendNotification);
    crossfader.setDoubleClickReturnValue(true, 0.5);
    crossfader.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    crossfader.onValueChange = [this] { mixer.setCrossfader((float)crossfader.getValue()); };

    addAndMakeVisible(talkoverButton);
    talkoverButton.setClickingTogglesState(true);
    UiStyle::setIcon(talkoverButton, "mic");
    talkoverButton.onClick = [this] { mixer.setTalkover(talkoverButton.getToggleState()); };

    addAndMakeVisible(autoDjButton);
    autoDjButton.setClickingTogglesState(true);
    UiStyle::setIcon(autoDjButton, "auto");
    autoDjButton.setButtonText("AUTO DJ");
    autoDjButton.onClick = [this] { autoDJ.setEnabled(autoDjButton.getToggleState()); };
    autoDjButton.setToggleState(autoDJ.isEnabled(), juce::dontSendNotification);

    startTimerHz(30);
    lookAndFeelChanged();
}

MixerComponent::~MixerComponent() {
    stopTimer();
}

void MixerComponent::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();

    for (auto* b : { &airA, &airB }) {
        b->setButtonText(TRANS("route_air"));
        b->setColour(juce::TextButton::buttonOnColourId, t.getAirOnColour());
    }
    for (auto* b : { &cueA, &cueB }) {
        b->setButtonText("CUE");
        b->setColour(juce::TextButton::buttonOnColourId, t.getCueOnColour());
    }
    airA.setTooltip(TRANS("tip_air") + " A");
    airB.setTooltip(TRANS("tip_air") + " B");
    cueA.setTooltip(TRANS("tip_cue") + " A");
    cueB.setTooltip(TRANS("tip_cue") + " B");

    talkoverButton.setButtonText(UiStyle::toUpper(TRANS("talkover")));
    talkoverButton.setTooltip(TRANS("tip_talkover"));
    talkoverButton.setColour(juce::TextButton::buttonOnColourId, t.getTalkoverOnColour());

    autoDjButton.setTooltip(TRANS("tip_autodj"));
    autoDjButton.setColour(juce::TextButton::buttonOnColourId, t.getAutoDjOnColour());

    for (auto* s : { &volSliderA, &volSliderB }) {
        s->setColour(juce::Slider::thumbColourId, t.getHighlightColour());
        s->setColour(juce::Slider::trackColourId, t.getStrongOutlineColour());
        s->setTooltip(TRANS("tip_fader"));
    }
    crossfader.setColour(juce::Slider::thumbColourId, t.getPrimaryTextColour());
    crossfader.setColour(juce::Slider::trackColourId, t.getStrongOutlineColour());
    crossfader.setTooltip(TRANS("tip_crossfader"));

    repaint();
}

void MixerComponent::timerCallback() {
    auto update = [](MeterState& m, float rmsL, float rmsR, float peakL, float peakR) {
        const float rms[2] = { rmsL, rmsR };
        const float peak[2] = { peakL, peakR };
        for (int c = 0; c < 2; ++c) {
            // Subida inmediata, caída suave: más fácil de leer que el valor crudo.
            m.rms[c] = rms[c] > m.rms[c] ? rms[c] : m.rms[c] * 0.82f + rms[c] * 0.18f;
            m.peak[c] = juce::jmax(peak[c], m.peak[c] * 0.94f);
        }
    };

    update(meterA, mixer.getRMS(0, 0), mixer.getRMS(0, 1), mixer.getPeak(0, 0), mixer.getPeak(0, 1));
    update(meterB, mixer.getRMS(1, 0), mixer.getRMS(1, 1), mixer.getPeak(1, 0), mixer.getPeak(1, 1));
    update(meterMaster, mixer.getMasterRMS(0), mixer.getMasterRMS(1), mixer.getMasterPeak(0), mixer.getMasterPeak(1));

    if (autoDjButton.getToggleState() != autoDJ.isEnabled())
        autoDjButton.setToggleState(autoDJ.isEnabled(), juce::dontSendNotification);

    repaint(meterAreaA.expanded(2));
    repaint(meterAreaB.expanded(2));
    repaint(meterAreaMaster.expanded(2));
}

float MixerComponent::levelToProportion(float gain) {
    const float db = juce::Decibels::gainToDecibels(gain, meterMinDb);
    return juce::jlimit(0.0f, 1.0f, (db - meterMinDb) / (meterMaxDb - meterMinDb));
}

void MixerComponent::drawMeter(juce::Graphics& g, juce::Rectangle<float> area, const MeterState& meter) {
    auto& t = ThemeManager::getInstance();
    const float barGap = 2.0f;
    const float barWidth = (area.getWidth() - barGap) * 0.5f;

    const float yellowFrom = (-9.0f - meterMinDb) / (meterMaxDb - meterMinDb);
    const float redFrom = (-3.0f - meterMinDb) / (meterMaxDb - meterMinDb);

    for (int c = 0; c < 2; ++c) {
        auto bar = juce::Rectangle<float>(area.getX() + (float) c * (barWidth + barGap), area.getY(), barWidth, area.getHeight());

        g.setColour(t.getBaseBackgroundColour());
        g.fillRoundedRectangle(bar, 2.0f);

        const float level = levelToProportion(meter.rms[c]);
        if (level > 0.0f) {
            auto lit = bar.withTop(bar.getBottom() - bar.getHeight() * level);
            g.saveState();
            g.reduceClipRegion(lit.toNearestInt());
            auto zone = [&](float from, float to, juce::Colour colour) {
                g.setColour(colour);
                g.fillRect(bar.withTop(bar.getBottom() - bar.getHeight() * to).withBottom(bar.getBottom() - bar.getHeight() * from));
            };
            zone(0.0f, yellowFrom, t.getMeterGreenColour());
            zone(yellowFrom, redFrom, t.getMeterYellowColour());
            zone(redFrom, 1.0f, t.getMeterRedColour());
            g.restoreState();
        }

        const float peak = levelToProportion(meter.peak[c]);
        if (peak > 0.01f) {
            const float y = bar.getBottom() - bar.getHeight() * peak;
            g.setColour(peak >= redFrom ? t.getMeterRedColour() : t.getPrimaryTextColour().withAlpha(0.85f));
            g.fillRect(bar.getX(), y, bar.getWidth(), 2.0f);
        }
    }
}

void MixerComponent::drawFaderLabels(juce::Graphics& g, juce::Slider& fader, juce::Rectangle<int> readout) {
    auto& t = ThemeManager::getInstance();

    // Marca de 0 dB junto al recorrido del fader.
    const float zeroY = (float) fader.getY() + (float) fader.getPositionOfValue(0.0);
    const float x = (float) fader.getX();
    g.setColour(t.getMutedTextColour());
    g.fillRect(x - 1.0f, zeroY - 0.5f, 7.0f, 1.0f);
    g.fillRect(x + (float) fader.getWidth() - 6.0f, zeroY - 0.5f, 7.0f, 1.0f);

    const double value = fader.getValue();
    const juce::String text = value <= -59.9 ? juce::String(juce::CharPointer_UTF8("-\xe2\x88\x9e dB"))
                                             : (value > 0.05 ? "+" : "") + juce::String(value, 1) + " dB";
    g.setColour(std::abs(value) < 0.05 ? t.getSecondaryTextColour() : t.getHighlightColour());
    g.setFont(UiStyle::mono(12.0f, true));
    g.drawText(text, readout, juce::Justification::centred, false);
}

void MixerComponent::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    UiStyle::drawPanel(g, getLocalBounds().toFloat().reduced(0.5f));

    g.setFont(UiStyle::caption(11.5f));
    g.setColour(t.getSecondaryTextColour());
    g.drawText("DECK A", titleA, juce::Justification::centred, false);
    g.drawText("DECK B", titleB, juce::Justification::centred, false);
    g.drawText(UiStyle::toUpper(TRANS("master_output")), titleMaster, juce::Justification::centred, false);

    drawMeter(g, meterAreaA.toFloat(), meterA);
    drawMeter(g, meterAreaB.toFloat(), meterB);
    drawMeter(g, meterAreaMaster.toFloat(), meterMaster);

    // Escala del medidor de salida
    // Con poca altura se omiten las marcas que se pisarían con la anterior.
    g.setFont(UiStyle::mono(10.0f));
    g.setColour(t.getMutedTextColour());
    float lastLabelY = -100.0f;
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f }) {
        const float y = (float) meterAreaMaster.getBottom()
                      - (float) meterAreaMaster.getHeight() * (db - meterMinDb) / (meterMaxDb - meterMinDb);
        if (y - lastLabelY < 12.0f) continue;
        lastLabelY = y;
        g.drawText(juce::String((int) db), juce::Rectangle<float>((float) scaleArea.getX(), y - 6.0f, (float) scaleArea.getWidth(), 12.0f),
                   juce::Justification::centredRight, false);
    }

    drawFaderLabels(g, volSliderA, readoutA);
    drawFaderLabels(g, volSliderB, readoutB);

    // Extremos del crossfader
    g.setFont(UiStyle::ui(13.0f, true));
    g.setColour(t.getSecondaryTextColour());
    g.drawText("A", crossfaderArea.withWidth(16), juce::Justification::centred, false);
    g.drawText("B", crossfaderArea.withTrimmedLeft(crossfaderArea.getWidth() - 16), juce::Justification::centred, false);
}

void MixerComponent::resized() {
    auto area = getLocalBounds().reduced(10);
    const int gap = 6;
    const int centreWidth = juce::jmax(92, (int) (area.getWidth() * 0.36f));
    const int sideWidth = (area.getWidth() - centreWidth - gap * 2) / 2;

    auto left = area.removeFromLeft(sideWidth);
    area.removeFromLeft(gap);
    auto right = area.removeFromRight(sideWidth);
    area.removeFromRight(gap);
    auto centre = area;

    // Títulos de columna
    titleA = left.removeFromTop(18);
    titleB = right.removeFromTop(18);
    titleMaster = centre.removeFromTop(18);
    for (auto* col : { &left, &right, &centre }) col->removeFromTop(4);

    // Botones
    const int buttonHeight = getHeight() < 300 ? 30 : 34;
    airA.setBounds(left.removeFromTop(buttonHeight));
    airB.setBounds(right.removeFromTop(buttonHeight));
    autoDjButton.setBounds(centre.removeFromTop(buttonHeight));
    for (auto* col : { &left, &right, &centre }) col->removeFromTop(gap);
    cueA.setBounds(left.removeFromTop(buttonHeight));
    cueB.setBounds(right.removeFromTop(buttonHeight));
    talkoverButton.setBounds(centre.removeFromTop(buttonHeight));
    for (auto* col : { &left, &right, &centre }) col->removeFromTop(10);

    // Crossfader al pie, ocupando las tres columnas
    auto bottom = getLocalBounds().reduced(10).removeFromBottom(34);
    crossfaderArea = bottom;
    crossfader.setBounds(bottom.reduced(18, 0));
    for (auto* col : { &left, &right, &centre }) col->removeFromBottom(34 + 8);

    // Lecturas en dB bajo los faders
    readoutA = left.removeFromBottom(16);
    readoutB = right.removeFromBottom(16);
    centre.removeFromBottom(16);
    for (auto* col : { &left, &right, &centre }) col->removeFromBottom(4);

    // Faders y medidores
    const int meterWidth = 14;
    auto faderA = left.withTrimmedRight(meterWidth + 4);
    meterAreaA = left.removeFromRight(meterWidth).withTrimmedTop(10).withTrimmedBottom(10);
    volSliderA.setBounds(faderA.withSizeKeepingCentre(juce::jmin(44, faderA.getWidth()), faderA.getHeight()));

    auto faderB = right.withTrimmedLeft(meterWidth + 4);
    meterAreaB = right.removeFromLeft(meterWidth).withTrimmedTop(10).withTrimmedBottom(10);
    volSliderB.setBounds(faderB.withSizeKeepingCentre(juce::jmin(44, faderB.getWidth()), faderB.getHeight()));

    const int masterWidth = 20;
    auto master = centre.withSizeKeepingCentre(masterWidth + 28, centre.getHeight()).withTrimmedTop(10).withTrimmedBottom(10);
    scaleArea = master.removeFromLeft(22).translated(-6, 0);
    meterAreaMaster = master.removeFromLeft(masterWidth);
}
