#include "DeckComponent.h"
#include "../library/TrackNaming.h"
#include "TrackDetailsDialog.h"
#include "../library/Playlist.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

DeckComponent::DeckComponent(Deck& d) : deck(d), waveform(d.getThumbnail()) {
    addAndMakeVisible(waveform);
    waveform.isSeekLocked = [this] { return isOnAir(); };
    waveform.onPositionChanged = [this](double fraction) {
        if (deck.getLengthInSeconds() > 0) {
            deck.setPosition(fraction * deck.getLengthInSeconds());
            waveform.setPosition(fraction);
        }
    };

    addAndMakeVisible(playButton);
    UiStyle::setVariant(playButton, "success");
    UiStyle::setIcon(playButton, "play");
    playButton.onClick = [this] { deck.play(); updateVisibility(); };

    addAndMakeVisible(pauseButton);
    UiStyle::setVariant(pauseButton, "warning");
    UiStyle::setIcon(pauseButton, "pause");
    pauseButton.onClick = [this] { deck.pause(); updateVisibility(); };

    addAndMakeVisible(skipButton);
    UiStyle::setIcon(skipButton, "skip");
    skipButton.onClick = [this] { deck.startSkip(); };

    addAndMakeVisible(loopButton);
    UiStyle::setIcon(loopButton, "loop");
    loopButton.setClickingTogglesState(true);
    loopButton.getProperties().set("toggleStyle", "soft");
    loopButton.onClick = [this] { deck.setLooping(loopButton.getToggleState()); };

    addAndMakeVisible(quitButton);
    UiStyle::setIcon(quitButton, "eject");
    quitButton.onClick = [this] { if (!isOnAir()) deck.unload(); };

    addAndMakeVisible(infoButton);
    UiStyle::setVariant(infoButton, "ghost");
    UiStyle::setIcon(infoButton, "info");
    infoButton.onClick = [this] { openTrackInfo(); };

    addAndMakeVisible(speedSlider);
    speedSlider.setSliderStyle(juce::Slider::LinearVertical);
    speedSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    speedSlider.setRange(0.92, 1.08, 0.001);
    speedSlider.setValue(1.0, juce::dontSendNotification);
    speedSlider.setDoubleClickReturnValue(true, 1.0);
    speedSlider.getProperties().set("fill", "centre");
    speedSlider.onValueChange = [this] {
        deck.setSpeed(speedSlider.getValue());
        repaint(pitchArea);
    };

    addAndMakeVisible(resetSpeedButton);
    UiStyle::setVariant(resetSpeedButton, "ghost");
    resetSpeedButton.setButtonText("0%");
    resetSpeedButton.onClick = [this] { speedSlider.setValue(1.0); };

    deck.addChangeListener(this);
    startTimer(40);

    lookAndFeelChanged();
    updateVisibility();
}

DeckComponent::~DeckComponent() {
    deck.removeChangeListener(this);
    stopTimer();
}

juce::String DeckComponent::getDeckLetter() const {
    return deck.getId() == 1 ? "A" : "B";
}

bool DeckComponent::isOnAir() const {
    return deck.isPlaying() && deck.isRoutedToMain();
}

DeckComponent::State DeckComponent::getState() const {
    if (!deck.isLoaded()) return State::Empty;
    if (deck.isPlaying()) {
        if (deck.isRoutedToMain()) return State::OnAir;
        if (deck.isRoutedToCue()) return State::Cue;
        return State::Silent;
    }
    return deck.getCurrentPosition() > 0.5 ? State::Paused : State::Ready;
}

void DeckComponent::lookAndFeelChanged() {
    playButton.setButtonText(UiStyle::toUpper(TRANS("play")));
    pauseButton.setButtonText(UiStyle::toUpper(TRANS("pause")));
    skipButton.setButtonText(UiStyle::toUpper(TRANS("skip")));
    loopButton.setButtonText(UiStyle::toUpper(TRANS("loop")));
    quitButton.setButtonText(UiStyle::toUpper(TRANS("unload")));

    skipButton.setTooltip(TRANS("tip_skip"));
    loopButton.setTooltip(TRANS("tip_loop"));
    infoButton.setTooltip(TRANS("track_info"));
    resetSpeedButton.setTooltip(TRANS("reset_pitch"));
    speedSlider.setTooltip(TRANS("tip_pitch"));

    auto& t = ThemeManager::getInstance();
    loopButton.setColour(juce::TextButton::buttonOnColourId, t.getHighlightColour());
    speedSlider.setColour(juce::Slider::thumbColourId, t.getHighlightColour());
    speedSlider.setColour(juce::Slider::trackColourId, t.getStrongOutlineColour());

    lastState = getState();
    repaint();
}

//==============================================================================

void DeckComponent::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    const auto bounds = getLocalBounds().toFloat();
    const auto state = getState();

    juce::Colour border;
    if (state == State::OnAir) border = t.getAirOnColour();
    else if (state == State::Cue) border = t.getCueOnColour();
    UiStyle::drawPanel(g, bounds.reduced(0.5f), border, border.isTransparent() ? 1.0f : 2.0f);

    if (state == State::Empty) {
        auto zone = bounds.reduced((float) UiStyle::padding);
        juce::Path dashed, outline;
        outline.addRoundedRectangle(zone, UiStyle::panelRadius);
        const float dashes[] = { 6.0f, 5.0f };
        juce::PathStrokeType(1.5f).createDashedStroke(dashed, outline, dashes, 2);
        g.setColour(dragHover ? t.getHighlightColour() : t.getStrongOutlineColour());
        g.fillPath(dashed);

        UiStyle::drawEmptyState(g, zone.toNearestInt(), "drop",
                                TRANS("deck") + " " + getDeckLetter() + juce::String(juce::CharPointer_UTF8("  \xc2\xb7  "))
                                    + TRANS("empty_deck_title"),
                                TRANS("drop_track_hint"));
        return;
    }

    // Cabecera: letra del deck y estado
    auto header = headerArea.toFloat();
    auto badge = header.removeFromLeft(26.0f).withSizeKeepingCentre(26.0f, 26.0f);
    g.setColour(t.getRaisedColour());
    g.fillRoundedRectangle(badge, 6.0f);
    g.setColour(t.getStrongOutlineColour());
    g.drawRoundedRectangle(badge.reduced(0.5f), 6.0f, 1.0f);
    g.setColour(t.getPrimaryTextColour());
    g.setFont(UiStyle::ui(15.0f, true));
    g.drawText(getDeckLetter(), badge, juce::Justification::centred, false);

    header.removeFromLeft(10.0f);
    auto chipArea = header.withSizeKeepingCentre(header.getWidth(), 22.0f);
    switch (state) {
        case State::OnAir:
            UiStyle::drawChip(g, chipArea, TRANS("deck_state_onair"), t.getAirOnColour(), juce::Colours::white, true);
            break;
        case State::Cue:
            UiStyle::drawChip(g, chipArea, TRANS("deck_state_cue"), t.getCueOnColour(),
                              t.getContrastingTextColour(t.getCueOnColour()), true);
            break;
        case State::Silent:
            UiStyle::drawChip(g, chipArea, TRANS("deck_state_silent"), t.getWarningColour().withAlpha(0.18f),
                              t.getWarningColour(), true);
            break;
        case State::Paused:
            UiStyle::drawChip(g, chipArea, TRANS("deck_state_paused"), t.getWarningColour().withAlpha(0.18f),
                              t.getWarningColour().interpolatedWith(t.getPrimaryTextColour(), 0.2f));
            break;
        case State::Ready:
        case State::Empty:
            UiStyle::drawChip(g, chipArea, TRANS("deck_state_ready"), t.getSuccessColour().withAlpha(0.18f),
                              t.getSuccessColour().interpolatedWith(t.getPrimaryTextColour(), 0.2f));
            break;
    }

    // Título y artista
    const auto track = deck.getCurrentTrack();
    g.setColour(t.getPrimaryTextColour());
    g.setFont(UiStyle::semibold(titleArea.getHeight() >= 30 ? 23.0f : 20.0f));
    g.drawText(TrackNaming::display(track.title), titleArea, juce::Justification::centredLeft, true);

    const auto artist = TrackNaming::display(track.artist);
    g.setColour(t.getSecondaryTextColour());
    g.setFont(UiStyle::ui(15.0f));
    g.drawText(artist.isNotEmpty() ? artist : TRANS("unknown_artist"), artistArea, juce::Justification::centredLeft, true);

    // Tiempo: lo que falta es el dato principal; cambia a ámbar a 30 s y a rojo a 10 s.
    // Se cuenta hasta el punto de salida, que es cuando la pista deja el aire.
    const double len = deck.getEndPosition();
    const double pos = deck.getCurrentPosition();
    const double remaining = deck.getRemainingSeconds();

    auto remainingColour = t.getPrimaryTextColour();
    if (!deck.isLooping() && len > 0.0) {
        if (remaining <= 10.0) remainingColour = t.getErrorColour();
        else if (remaining <= 30.0) remainingColour = t.getWarningColour();
        if (remaining <= 10.0 && deck.isPlaying() && !blinkOn)
            remainingColour = remainingColour.withAlpha(0.3f);
    }

    auto time = timeArea.toFloat();
    g.setColour(remainingColour);
    g.setFont(UiStyle::mono(time.getHeight() >= 50.0f ? 36.0f : 30.0f, true));
    g.drawText("-" + UiStyle::formatTime(remaining), time.removeFromTop(time.getHeight() * 0.68f),
               juce::Justification::bottomRight, false);
    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::mono(13.0f));
    g.drawText(UiStyle::formatTime(pos) + " / " + UiStyle::formatTime(len), time,
               juce::Justification::centredRight, false);

    // Tempo
    if (!pitchArea.isEmpty()) {
        auto pitch = pitchArea.toFloat();
        g.setColour(t.getMutedTextColour());
        g.setFont(UiStyle::caption(10.5f));
        g.drawText(UiStyle::toUpper(TRANS("tempo")), pitch.removeFromTop(16.0f), juce::Justification::centred, false);

        const double percent = (speedSlider.getValue() - 1.0) * 100.0;
        const bool changed = std::abs(percent) >= 0.05;
        g.setColour(changed ? t.getHighlightColour() : t.getSecondaryTextColour());
        g.setFont(UiStyle::mono(13.0f, true));
        g.drawText(changed ? (percent > 0 ? "+" : "") + juce::String(percent, 1) + "%" : "0.0%",
                   pitch.removeFromTop(18.0f), juce::Justification::centred, false);
    }
}

void DeckComponent::paintOverChildren(juce::Graphics& g) {
    if (!dragHover || !deck.isLoaded()) return;

    auto& t = ThemeManager::getInstance();
    const bool blocked = isOnAir();
    const auto colour = blocked ? t.getErrorColour() : t.getHighlightColour();
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);

    g.setColour(colour.withAlpha(0.1f));
    g.fillRoundedRectangle(bounds, UiStyle::panelRadius);
    g.setColour(colour);
    g.drawRoundedRectangle(bounds, UiStyle::panelRadius, 2.0f);

    const auto text = blocked ? TRANS("deck_drop_blocked") : TRANS("deck_drop_here") + " " + getDeckLetter();
    const float h = 30.0f;
    const float w = UiStyle::getChipWidth(text, h);
    UiStyle::drawChip(g, bounds.withSizeKeepingCentre(w, h), text, colour, t.getContrastingTextColour(colour));
}

void DeckComponent::resized() {
    auto area = getLocalBounds().reduced(UiStyle::padding);

    if (!deck.isLoaded()) {
        pitchArea = {};
        return;
    }

    pitchArea = area.removeFromRight(54);
    area.removeFromRight(10);
    {
        auto p = pitchArea;
        p.removeFromTop(38);
        resetSpeedButton.setBounds(p.removeFromBottom(26));
        p.removeFromBottom(6);
        speedSlider.setBounds(p.withSizeKeepingCentre(40, p.getHeight()));
    }

    headerArea = area.removeFromTop(26);
    infoButton.setBounds(headerArea.removeFromRight(32));
    area.removeFromTop(8);

    const bool compact = getHeight() < 290;
    auto textBlock = area.removeFromTop(compact ? 46 : 52);
    timeArea = textBlock.removeFromRight(compact ? 150 : 170);
    textBlock.removeFromRight(12);
    titleArea = textBlock.removeFromTop(compact ? 26 : 30);
    artistArea = textBlock;

    area.removeFromTop(10);
    auto buttonRow = area.removeFromBottom(compact ? 36 : 40);
    area.removeFromBottom(10);
    waveform.setBounds(area);

    const int gap = 6;
    const int unit = (buttonRow.getWidth() - gap * 3) / 5;
    auto transport = buttonRow.removeFromLeft(unit * 2);
    playButton.setBounds(transport);
    pauseButton.setBounds(transport);
    buttonRow.removeFromLeft(gap);
    skipButton.setBounds(buttonRow.removeFromLeft(unit));
    buttonRow.removeFromLeft(gap);
    loopButton.setBounds(buttonRow.removeFromLeft(unit));
    buttonRow.removeFromLeft(gap);
    quitButton.setBounds(buttonRow);
}

//==============================================================================

bool DeckComponent::isInterestedInFileDrag(const juce::StringArray&) { return true; }

void DeckComponent::fileDragEnter(const juce::StringArray&, int, int) { dragHover = true; repaint(); }
void DeckComponent::fileDragExit(const juce::StringArray&) { dragHover = false; repaint(); }
void DeckComponent::itemDragEnter(const SourceDetails&) { dragHover = true; repaint(); }
void DeckComponent::itemDragExit(const SourceDetails&) { dragHover = false; repaint(); }

// Cargar una pista reemplaza la que suena: si este deck está al aire, cortaría la transmisión.
bool DeckComponent::rejectDropIfOnAir() {
    if (isOnAir()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               TRANS("deck_on_air_title"), TRANS("deck_on_air_msg"));
        return true;
    }
    return false;
}

void DeckComponent::filesDropped(const juce::StringArray& files, int, int) {
    dragHover = false;
    repaint();
    if (rejectDropIfOnAir()) return;

    if (files.size() > 0) {
        deck.loadTrack(juce::File(files[0]));
        updateVisibility();
    }
}

bool DeckComponent::isInterestedInDragSource(const SourceDetails& dragSourceDetails) {
    juce::String desc = dragSourceDetails.description.toString();
    return desc.startsWith("track_path_") ||
           desc.startsWith("track_list_") ||
           desc.startsWith("playlist_drag_");
}

void DeckComponent::setPlaylist(Playlist* p) { playlistRef = p; }

void DeckComponent::itemDropped(const SourceDetails& dragSourceDetails) {
    dragHover = false;
    repaint();
    if (rejectDropIfOnAir()) return;

    juce::String desc = dragSourceDetails.description.toString();
    juce::String pathToCheck;
    std::vector<Playlist::RowRef> playlistRows;

    if (desc.startsWith("track_path_")) {
        pathToCheck = desc.substring(11);
    }
    else if (desc.startsWith("track_list_")) {
        pathToCheck = desc.substring(11).upToFirstOccurrenceOf("|", false, false);
    }
    else if (desc.startsWith("playlist_drag_")) {
        // Solo se carga la primera pista arrastrada; se quita de la cola.
        auto first = juce::StringArray::fromTokens(desc.substring(14), "|", "")[0];
        const int index = first.upToFirstOccurrenceOf(":", false, false).getIntValue();
        pathToCheck = first.fromFirstOccurrenceOf(":", false, false);
        playlistRows.push_back({ index, pathToCheck.toStdString() });
    }

    if (pathToCheck.isNotEmpty()) {
        deck.unload();
        deck.loadTrack(juce::File(pathToCheck));
        updateVisibility();

        if (playlistRef != nullptr && !playlistRows.empty() && deck.isLoaded())
            playlistRef->removeTracksIfMatching(playlistRows);
    }
}

//==============================================================================

void DeckComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    // El deck avisa cada 100 ms mientras suena; solo se reorganiza la vista si
    // cambió algo estructural (carga, reproducción o pista).
    const juce::String path(deck.getCurrentTrack().path);
    if (deck.isLoaded() != shownLoaded || deck.isPlaying() != shownPlaying || path != shownPath
        || deck.getCueIn() != shownCueIn || deck.getEndPosition() != shownEnd)
        updateVisibility();
}

void DeckComponent::timerCallback() {
    const double len = deck.getLengthInSeconds();
    waveform.setPosition(len > 0 ? deck.getCurrentPosition() / len : 0.0);

    const bool blink = (juce::Time::getMillisecondCounter() / 450) % 2 == 0;
    const int second = (int) deck.getCurrentPosition();
    const auto state = getState();

    if (state != lastState) {
        lastState = state;
        waveform.setTooltip(state == State::OnAir ? TRANS("tip_seek_on_air") : TRANS("tip_seek"));
        quitButton.setEnabled(state != State::OnAir);
        quitButton.setTooltip(state == State::OnAir ? TRANS("tip_unload_on_air") : juce::String());
        repaint();
    } else if (second != lastPaintedSecond || blink != blinkOn) {
        repaint(timeArea);
    }

    lastPaintedSecond = second;
    blinkOn = blink;
}

void DeckComponent::updateVisibility() {
    const bool loaded = deck.isLoaded();
    const bool playing = deck.isPlaying();

    for (juce::Component* c : { (juce::Component*) &waveform, (juce::Component*) &skipButton,
                                (juce::Component*) &loopButton, (juce::Component*) &quitButton,
                                (juce::Component*) &infoButton, (juce::Component*) &speedSlider,
                                (juce::Component*) &resetSpeedButton })
        c->setVisible(loaded);

    playButton.setVisible(loaded && !playing);
    pauseButton.setVisible(loaded && playing);
    loopButton.setToggleState(deck.isLooping(), juce::dontSendNotification);
    quitButton.setEnabled(!isOnAir());

    if (loaded) waveform.setScalingFactor(deck.getNormalizationGain());
    waveform.setCueRegion(loaded ? deck.getCueIn() : 0.0, loaded ? deck.getEndPosition() : 0.0);

    shownLoaded = loaded;
    shownPlaying = playing;
    shownPath = juce::String(deck.getCurrentTrack().path);
    shownCueIn = deck.getCueIn();
    shownEnd = deck.getEndPosition();
    lastState = getState();
    resized();
    repaint();
}

void DeckComponent::openTrackInfo() {
    if (!deck.isLoaded()) return;

    auto* dialog = new TrackDetailsDialog(deck.getLibrary(), deck.getCurrentTrack());
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dialog);
    opt.dialogTitle = TRANS("track_info");
    opt.dialogBackgroundColour = ThemeManager::getInstance().getBaseBackgroundColour();
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;

    auto* w = opt.launchAsync();
    w->centreWithSize(dialog->getWidth(), dialog->getHeight());

    dialog->onSave = [this] { repaint(); };
    dialog->onCancel = [w] { w->closeButtonPressed(); };
}
