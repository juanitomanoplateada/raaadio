#include "LibraryView.h"
#include "TrackDetailsDialog.h"
#include "CartwallView.h"
#include "../audio/Deck.h"
#include "../autodj/OnAir.h"
#include "../library/Playlist.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

enum Columns { colTitle = 1, colArtist, colLength };

LibraryView::LibraryView(MusicLibrary& lib)
    : library(lib), categorySidebar(lib)
{
    addAndMakeVisible(categorySidebar);
    addAndMakeVisible(refreshButton);
    addAndMakeVisible(searchBox);
    addAndMakeVisible(table);

    UiStyle::setVariant(refreshButton, "ghost");
    UiStyle::setIcon(refreshButton, "refresh");
    refreshButton.onClick = [this] { refresh(); };

    searchBox.setIndents(34, 0);
    searchBox.setJustification(juce::Justification::centredLeft);
    searchBox.setFont(UiStyle::ui(14.5f));
    searchBox.onTextChange = [this] {
        if (searchBox.getText().isNotEmpty()) {
            currentCategoryFilter = "";
            categorySidebar.setSelectedCategory("");
        }
        refresh();
    };
    searchBox.onEscapeKey = [this] { searchBox.clear(); refresh(); };

    categorySidebar.onCategorySelected = [this](const juce::String& category) {
        currentCategoryFilter = category;
        refresh();
    };

    table.setModel(this);
    UiStyle::styleTable(table);
    table.getHeader().addColumn("", colTitle, 200, 80, -1, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn("", colArtist, 150, 60, -1, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn("", colLength, 80, 70, 90, juce::TableHeaderComponent::notResizableOrSortable);
    table.setMultipleSelectionEnabled(true);

    lookAndFeelChanged();
    refresh();
}

LibraryView::~LibraryView() {
    table.setModel(nullptr);
}

void LibraryView::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();

    // Separador entre la barra de categorías y la tabla.
    g.setColour(t.getOutlineColour());
    g.fillRect(categorySidebar.getRight() + 7, categorySidebar.getY(), 1, categorySidebar.getHeight());

    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    g.drawText(juce::String((int) cachedTracks.size()) + " " + TRANS("tracks_count"), countArea,
               juce::Justification::centredRight, false);
}

void LibraryView::paintOverChildren(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    UiStyle::drawIcon(g, "search", searchBox.getBounds().toFloat().removeFromLeft(34.0f).withSizeKeepingCentre(15.0f, 15.0f),
                      t.getMutedTextColour());

    if (cachedTracks.empty()) {
        auto area = table.getBounds().withTrimmedTop(table.getHeaderHeight());
        if (searchBox.getText().isNotEmpty())
            UiStyle::drawEmptyState(g, area, "search", TRANS("library_no_results"), "\"" + searchBox.getText() + "\"");
        else
            UiStyle::drawEmptyState(g, area, "music", TRANS("library_empty_title"), TRANS("library_empty_hint"));
    }
}

void LibraryView::resized() {
    auto area = getLocalBounds().withTrimmedTop(UiStyle::gap);

    categorySidebar.setBounds(area.removeFromLeft(210));
    area.removeFromLeft(16);

    auto toolbar = area.removeFromTop(UiStyle::controlHeight);
    refreshButton.setBounds(toolbar.removeFromRight(UiStyle::controlHeight));
    toolbar.removeFromRight(4);
    countArea = toolbar.removeFromRight(90);
    toolbar.removeFromRight(8);
    searchBox.setBounds(toolbar);

    area.removeFromTop(UiStyle::gap);
    table.setBounds(area);

    const int lengthWidth = 80;
    const int flexible = juce::jmax(120, table.getWidth() - lengthWidth - table.getViewport()->getScrollBarThickness());
    table.getHeader().setColumnWidth(colTitle, (int) (flexible * 0.58f));
    table.getHeader().setColumnWidth(colArtist, flexible - (int) (flexible * 0.58f));
    table.getHeader().setColumnWidth(colLength, lengthWidth);
}

void LibraryView::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();
    searchBox.setTextToShowWhenEmpty(TRANS("search_placeholder"), t.getMutedTextColour());
    refreshButton.setTooltip(TRANS("btn_refresh"));

    table.getHeader().setColumnName(colTitle, TRANS("header_title"));
    table.getHeader().setColumnName(colArtist, TRANS("header_artist"));
    table.getHeader().setColumnName(colLength, TRANS("header_length"));
    table.setColour(juce::ListBox::backgroundColourId, t.getPanelBackgroundColour());

    table.repaint();
    categorySidebar.sendLookAndFeelChange();
    repaint();
}

void LibraryView::refresh() {
    cachedTracks = library.search(searchBox.getText(), currentCategoryFilter);
    table.updateContent();
    categorySidebar.refreshCategories();
    repaint();
}

void LibraryView::setDecks(Deck* dA, Deck* dB) { deckA = dA; deckB = dB; }
void LibraryView::setPlaylist(Playlist* p) { playlist = p; }

int LibraryView::getNumRows() { return static_cast<int>(cachedTracks.size()); }

void LibraryView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }
}

void LibraryView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber >= (int) cachedTracks.size()) return;

    auto& t = ThemeManager::getInstance();
    const auto& track = cachedTracks[(size_t) rowNumber];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    if (columnId == colTitle) {
        g.setColour(t.getPrimaryTextColour());
        g.setFont(UiStyle::ui(14.5f));
        g.drawText(juce::String(track.title), area, juce::Justification::centredLeft, true);
    } else if (columnId == colArtist) {
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(14.0f));
        g.drawText(juce::String(track.artist), area, juce::Justification::centredLeft, true);
    } else if (columnId == colLength) {
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::mono(13.0f));
        g.drawText(UiStyle::formatTime(track.duration), area, juce::Justification::centredRight, false);
    }
}

void LibraryView::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) {
     if (rowNumber >= (int) cachedTracks.size()) return;

    auto* dialog = new TrackDetailsDialog(library, cachedTracks[(size_t) rowNumber]);
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dialog);
    opt.dialogTitle = TRANS("track_info");
    opt.dialogBackgroundColour = ThemeManager::getInstance().getBaseBackgroundColour();
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;

    auto* w = opt.launchAsync();
    w->centreWithSize(520, 640);

    dialog->onSave = [this] { refresh(); };
    dialog->onCancel = [w] { w->closeButtonPressed(); };
}

juce::var LibraryView::getDragSourceDescription(const juce::SparseSet<int>& selectedRows) {
    if (selectedRows.isEmpty()) return {};
    juce::StringArray paths;
    for (int i = 0; i < selectedRows.size(); ++i) {
        int row = selectedRows[i];
        if (row < (int) cachedTracks.size()) paths.add(cachedTracks[(size_t) row].path);
    }
    if (paths.isEmpty()) return {};
    return "track_list_" + paths.joinIntoString("|");
}

void LibraryView::cellClicked(int rowNumber, int, const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu() || rowNumber >= (int) cachedTracks.size()) return;

    if (!table.isRowSelected(rowNumber)) {
        table.deselectAllRows();
        table.selectRow(rowNumber);
    }

    auto selectedRows = table.getSelectedRows();
    if (selectedRows.isEmpty()) return;

    juce::PopupMenu m;
    if (selectedRows.size() == 1) {
        m.addItem(1, TRANS("menu_play_now"));
        m.addSeparator();
    }
    m.addItem(2, TRANS("menu_add_to_start"));
    m.addItem(3, TRANS("menu_add_to_end"));
    if (cartPlayer != nullptr && selectedRows.size() == 1)
        m.addSubMenu(TRANS("menu_assign_cart"), CartwallView::createAssignMenu(*cartPlayer, 4000));
    m.addSeparator();

    auto categories = library.getCategories();
    juce::PopupMenu addCatMenu;
    for (const auto& cat : categories) addCatMenu.addItem(1000 + cat.id, MusicLibrary::getDisplayName(cat));
    m.addSubMenu(TRANS("menu_add_to_category"), addCatMenu);

    juce::PopupMenu removeCatMenu;
    for (const auto& cat : categories) removeCatMenu.addItem(2000 + cat.id, MusicLibrary::getDisplayName(cat));
    m.addSubMenu(TRANS("menu_remove_from_category"), removeCatMenu);
    m.addItem(3000, TRANS("menu_remove_from_all_categories"));

    m.showMenuAsync(juce::PopupMenu::Options(), [this, selectedRows](int res) {
        auto selectedTracks = [&] {
            std::vector<TrackInfo> tracks;
            for (int i = 0; i < selectedRows.size(); ++i)
                if (selectedRows[i] < (int) cachedTracks.size())
                    tracks.push_back(cachedTracks[(size_t) selectedRows[i]]);
            return tracks;
        };

        if (res == 1 && selectedRows.size() == 1) {
            const auto tracks = selectedTracks();
            if (!tracks.empty() && deckA != nullptr && deckB != nullptr)
                OnAir::requestPlayNow(*deckA, *deckB, tracks.front());
        }
        else if (res == 2 && playlist) {
            playlist->insertTracks(selectedTracks(), 0);
        }
        else if (res == 3 && playlist) {
            for (const auto& t : selectedTracks()) playlist->addTrack(t);
        }
        else if (res >= 1000 && res < 2000) {
            const int catId = res - 1000;
            for (const auto& t : selectedTracks()) {
                auto currentCats = library.getTrackCategories(t.id);
                if (std::find(currentCats.begin(), currentCats.end(), catId) == currentCats.end()) {
                    currentCats.push_back(catId);
                    library.setTrackCategories(t.id, currentCats);
                }
            }
            refresh();
        }
        else if (res >= 2000 && res < 3000) {
            const int catId = res - 2000;
            for (const auto& t : selectedTracks()) {
                auto currentCats = library.getTrackCategories(t.id);
                auto it = std::remove(currentCats.begin(), currentCats.end(), catId);
                if (it != currentCats.end()) {
                    currentCats.erase(it, currentCats.end());
                    library.setTrackCategories(t.id, currentCats);
                }
            }
            refresh();
        }
        else if (res == 3000) {
            for (const auto& t : selectedTracks()) library.setTrackCategories(t.id, {});
            refresh();
        }
        else if (res >= 4000 && res < 4000 + CartPlayer::numCarts && cartPlayer != nullptr) {
            const auto tracks = selectedTracks();
            if (!tracks.empty()) cartPlayer->assignFromLibrary(res - 4000, tracks.front());
        }
    });
}

void LibraryView::visibilityChanged() {
    if (isVisible()) refresh();
}
