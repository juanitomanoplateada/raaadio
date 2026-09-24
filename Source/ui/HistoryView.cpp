#include "HistoryView.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

enum Columns { colTime = 1, colTitle, colArtist, colLength, colDeck };

HistoryView::HistoryView(HistoryManager& hm) : historyManager(hm) {
    historyManager.addChangeListener(this);

    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);

    auto& header = table.getHeader();
    header.addColumn("", colTime, 150, 100, 180, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colTitle, 200, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colArtist, 150, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colLength, 80, 70, 90, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colDeck, 70, 60, 80, juce::TableHeaderComponent::notResizableOrSortable);

    cachedHistory = historyManager.getHistory();
    lookAndFeelChanged();
}

HistoryView::~HistoryView() {
    historyManager.removeChangeListener(this);
    table.setModel(nullptr);
}

void HistoryView::paint(juce::Graphics&) {}

void HistoryView::paintOverChildren(juce::Graphics& g) {
    if (!cachedHistory.empty()) return;
    UiStyle::drawEmptyState(g, table.getBounds().withTrimmedTop(table.getHeaderHeight()), "history",
                            TRANS("history_empty_title"), TRANS("history_empty_hint"));
}

void HistoryView::resized() {
    table.setBounds(getLocalBounds().withTrimmedTop(UiStyle::gap));

    const int fixed = 150 + 80 + 70 + table.getViewport()->getScrollBarThickness();
    const int flexible = juce::jmax(120, table.getWidth() - fixed);
    table.getHeader().setColumnWidth(colTitle, (int) (flexible * 0.55f));
    table.getHeader().setColumnWidth(colArtist, flexible - (int) (flexible * 0.55f));
}

void HistoryView::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (source == &historyManager) {
        cachedHistory = historyManager.getHistory();
        table.updateContent();
        repaint();
    }
}

int HistoryView::getNumRows() {
    return static_cast<int>(cachedHistory.size());
}

void HistoryView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }
}

void HistoryView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber >= (int) cachedHistory.size()) return;

    auto& t = ThemeManager::getInstance();
    const auto& entry = cachedHistory[(size_t) rowNumber];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    switch (columnId) {
        case colTime: {
            // La hora basta para hoy; en días anteriores se antepone la fecha.
            const auto now = juce::Time::getCurrentTime();
            const bool today = entry.timestamp.getDayOfYear() == now.getDayOfYear() && entry.timestamp.getYear() == now.getYear();
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::mono(13.0f));
            g.drawText(entry.timestamp.formatted(today ? "%H:%M:%S" : "%d/%m %H:%M"), area, juce::Justification::centredLeft, false);
            break;
        }
        case colTitle:
            g.setColour(t.getPrimaryTextColour());
            g.setFont(UiStyle::ui(14.5f));
            g.drawText(juce::String(entry.track.title), area, juce::Justification::centredLeft, true);
            break;
        case colArtist:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::ui(14.0f));
            g.drawText(juce::String(entry.track.artist), area, juce::Justification::centredLeft, true);
            break;
        case colLength:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::mono(13.0f));
            g.drawText(UiStyle::formatTime(entry.track.duration), area, juce::Justification::centredRight, false);
            break;
        case colDeck: {
            const auto letter = entry.deckName.getLastCharacters(1);
            UiStyle::drawChip(g, area.toFloat().withSizeKeepingCentre((float) area.getWidth(), 20.0f),
                              TRANS("deck") + " " + letter, t.getRaisedColour(), t.getSecondaryTextColour());
            break;
        }
        default:
            break;
    }
}

void HistoryView::lookAndFeelChanged() {
    auto& header = table.getHeader();
    header.setColumnName(colTime, TRANS("header_time"));
    header.setColumnName(colTitle, TRANS("header_title"));
    header.setColumnName(colArtist, TRANS("header_artist"));
    header.setColumnName(colLength, TRANS("header_length"));
    header.setColumnName(colDeck, TRANS("header_deck"));

    table.setColour(juce::ListBox::backgroundColourId, ThemeManager::getInstance().getPanelBackgroundColour());
    table.repaint();
}
