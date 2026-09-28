#include "CartwallView.h"
#include "../library/MusicLibrary.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

CartwallView::CartwallView(CartPlayer& p, MusicLibrary& lib) : player(p), library(lib) {
    player.addChangeListener(this);
}

CartwallView::~CartwallView() {
    player.removeChangeListener(this);
}

void CartwallView::resized() {
    auto area = getLocalBounds().withTrimmedTop(UiStyle::gap);
    titleArea = area.removeFromTop(UiStyle::controlHeight);
    area.removeFromTop(UiStyle::gap);

    constexpr int columns = 4, rows = 3;
    const int cellWidth = (area.getWidth() - (columns - 1) * UiStyle::gap) / columns;
    const int cellHeight = (area.getHeight() - (rows - 1) * UiStyle::gap) / rows;
    for (int i = 0; i < CartPlayer::numCarts; ++i) {
        const int column = i % columns, row = i / columns;
        cells[(size_t) i] = { area.getX() + column * (cellWidth + UiStyle::gap),
                              area.getY() + row * (cellHeight + UiStyle::gap), cellWidth, cellHeight };
    }
}

void CartwallView::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();

    UiStyle::drawSectionTitle(g, titleArea, TRANS("cartwall_title"));
    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::ui(12.5f));
    g.drawText(TRANS("cartwall_hint"), titleArea, juce::Justification::centredRight, true);

    for (int i = 0; i < CartPlayer::numCarts; ++i) {
        const auto cell = cells[(size_t) i].toFloat();
        const bool assigned = player.isAssigned(i);
        const bool playing = player.isPlaying(i);

        g.setColour(playing ? t.getSuccessColour().withAlpha(0.14f) : t.getRaisedColour());
        g.fillRoundedRectangle(cell, UiStyle::controlRadius);
        if (playing) {
            g.setColour(t.getSuccessColour().withAlpha(0.28f));
            g.fillRoundedRectangle(cell.withWidth(cell.getWidth() * (float) player.getProgress(i)), UiStyle::controlRadius);
        }

        juce::Colour border = t.getOutlineColour();
        if (i == dropSlot) border = t.getHighlightColour();
        else if (playing) border = t.getSuccessColour();
        else if (i == hoverSlot && assigned) border = t.getStrongOutlineColour();
        g.setColour(border);
        if (assigned || i == dropSlot) {
            g.drawRoundedRectangle(cell.reduced(0.5f), UiStyle::controlRadius, i == dropSlot || playing ? 2.0f : 1.0f);
        } else {
            juce::Path outline;
            outline.addRoundedRectangle(cell.reduced(1.0f), UiStyle::controlRadius);
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType(1.0f).createDashedStroke(outline, outline, dashes, 2);
            g.fillPath(outline);
        }

        auto content = cell.reduced(10.0f, 8.0f);
        auto top = content.removeFromTop(20.0f);
        const float keyWidth = UiStyle::drawChip(g, top.withWidth(44.0f), "F" + juce::String(i + 1),
                                                 playing ? t.getSuccessColour() : t.getBaseBackgroundColour(),
                                                 playing ? juce::Colours::white : t.getSecondaryTextColour());
        juce::ignoreUnused(keyWidth);

        if (!assigned) {
            g.setColour(t.getMutedTextColour());
            g.setFont(UiStyle::ui(12.5f));
            g.drawFittedText(TRANS("cartwall_empty"), content.toNearestInt(), juce::Justification::centred, 2);
            continue;
        }

        const auto track = player.getTrack(i);
        g.setColour(playing ? t.getPrimaryTextColour() : t.getSecondaryTextColour());
        g.setFont(UiStyle::mono(13.0f, playing));
        g.drawText(UiStyle::formatTime(player.getRemainingSeconds(i)), top, juce::Justification::centredRight, false);

        content.removeFromTop(4.0f);
        const auto artist = TrackNaming::display(track.artist);
        auto artistArea = content.removeFromBottom(artist.isNotEmpty() ? 18.0f : 0.0f);
        g.setColour(t.getPrimaryTextColour());
        g.setFont(UiStyle::semibold(14.5f));
        g.drawFittedText(TrackNaming::display(track.title), content.toNearestInt(),
                         juce::Justification::centredLeft, 2, 0.9f);
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(12.5f));
        g.drawText(artist, artistArea, juce::Justification::centredLeft, true);
    }
}

int CartwallView::slotAt(juce::Point<int> position) const {
    for (int i = 0; i < CartPlayer::numCarts; ++i)
        if (cells[(size_t) i].contains(position)) return i;
    return -1;
}

void CartwallView::mouseDown(const juce::MouseEvent& e) {
    const int slot = slotAt(e.getPosition());
    if (slot < 0) return;

    if (e.mods.isPopupMenu()) {
        if (!player.isAssigned(slot)) return;
        juce::PopupMenu menu;
        menu.addItem(1, TRANS("cartwall_remove"));
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withMousePosition(),
                           [this, slot](int result) { if (result == 1) player.clear(slot); });
        return;
    }
    player.toggle(slot);
}

void CartwallView::mouseMove(const juce::MouseEvent& e) {
    const int slot = slotAt(e.getPosition());
    if (slot != hoverSlot) {
        hoverSlot = slot;
        setMouseCursor(slot >= 0 && player.isAssigned(slot) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void CartwallView::mouseExit(const juce::MouseEvent&) {
    hoverSlot = -1;
    repaint();
}

juce::PopupMenu CartwallView::createAssignMenu(const CartPlayer& player, int firstItemId) {
    juce::PopupMenu menu;
    for (int i = 0; i < CartPlayer::numCarts; ++i) {
        const auto current = player.isAssigned(i) ? TrackNaming::display(player.getTrack(i).title)
                                                  : TRANS("cartwall_free");
        menu.addItem(firstItemId + i, "F" + juce::String(i + 1) + juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) + current);
    }
    return menu;
}

int CartwallView::assignToFirstFree(const juce::String& dragDescription) {
    const auto track = library.getTrackByPath(DragFormats::firstPath(dragDescription));
    if (track.id == -1) return -1;

    for (int i = 0; i < CartPlayer::numCarts; ++i) {
        if (!player.isAssigned(i)) {
            player.assignFromLibrary(i, track);
            return i;
        }
    }
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("cartwall_title"), TRANS("cartwall_full"));
    return -1;
}

bool CartwallView::isInterestedInDragSource(const SourceDetails& details) {
    return DragFormats::firstPath(details.description.toString()).isNotEmpty();
}

void CartwallView::itemDragMove(const SourceDetails& details) {
    const int slot = slotAt(details.localPosition);
    if (slot != dropSlot) {
        dropSlot = slot;
        repaint();
    }
}

void CartwallView::itemDragExit(const SourceDetails&) {
    dropSlot = -1;
    repaint();
}

void CartwallView::itemDropped(const SourceDetails& details) {
    const int slot = slotAt(details.localPosition);
    dropSlot = -1;
    repaint();
    if (slot < 0) return;

    // Se asigna la primera pista arrastrada (desde la cola no se quita de la cola).
    const auto track = library.getTrackByPath(DragFormats::firstPath(details.description.toString()));
    if (track.id != -1) player.assignFromLibrary(slot, track);
}
