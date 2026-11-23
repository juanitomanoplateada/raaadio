 

#include "AdsListView.h"
#include "../helpers/External.h"
#include "../library/TrackNaming.h"
#include "TrackDetailsDialog.h"
#include "CartwallView.h"
#include "../autodj/OnAir.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

AdsListView::AdsListView(MusicLibrary& lib, AdvertisingManager& ads)
    : library(lib), adsManager(ads)
{
    addAndMakeVisible(refreshButton);
    UiStyle::setVariant(refreshButton, "ghost");
    UiStyle::setIcon(refreshButton, "refresh");
    refreshButton.onClick = [this] { refreshList(); };

    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);

    table.getHeader().addColumn(TRANS("header_cue"), 1, 64, 56, 80, juce::TableHeaderComponent::notResizableOrSortable);
    table.getHeader().addColumn(TRANS("header_title"), 2, 200, 100, -1, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn(TRANS("header_artist"), 3, 150, 80, -1, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn(TRANS("header_length"), 4, 72, 60, 90, juce::TableHeaderComponent::notResizableOrSortable);
    table.setMultipleSelectionEnabled(true);

    adsManager.addChangeListener(this);
    refreshList();
}

AdsListView::~AdsListView()
{
    adsManager.removeChangeListener(this);
}

void AdsListView::changeListenerCallback(juce::ChangeBroadcaster*)
{
     
    table.repaint(); 
}

void AdsListView::setDecks(Deck* dA, Deck* dB) {
    deckA = dA;
    deckB = dB;
}

void AdsListView::setPlaylist(Playlist* p) {
    playlist = p;
}

void AdsListView::refreshList()
{
    cachedTracks.clear();
    identificationIds.clear();

    // Las categorías fijas IDENTIFICACIONES y CUÑAS, por su rol: el nombre no importa.
    for (const auto& t : library.getTracksWithRole(CategoryRole::StationIds)) {
        cachedTracks.push_back(t);
        identificationIds.insert(t.id);
    }
    for (const auto& t : library.getTracksWithRole(CategoryRole::Spots))
        if (identificationIds.count(t.id) == 0)
            cachedTracks.push_back(t);

    // Identificaciones primero (por título); luego las cuñas con número, en orden, y al
    // final las que no tienen número.
    std::sort(cachedTracks.begin(), cachedTracks.end(), [this](const TrackInfo& a, const TrackInfo& b) {
        bool isIdA = identificationIds.count(a.id);
        bool isIdB = identificationIds.count(b.id);

         
        if (isIdA && !isIdB) return true;
        if (!isIdA && isIdB) return false;

         
        if (isIdA && isIdB) return a.title < b.title;

         
        int cueA = adsManager.getCueNumber(a.id);
        int cueB = adsManager.getCueNumber(b.id);

        if (cueA != -1 && cueB == -1) return true;   
        if (cueA == -1 && cueB != -1) return false;  

        if (cueA != -1 && cueB != -1) return cueA < cueB;  

         
        return a.title < b.title;
    });
    
    table.updateContent();
    table.repaint();
}

void AdsListView::paint(juce::Graphics& g)
{
    UiStyle::drawSectionTitle(g, titleArea, TRANS("ads_list_title"));
}

void AdsListView::paintOverChildren(juce::Graphics& g)
{
    if (cachedTracks.empty()) {
        auto area = table.getBounds().withTrimmedTop(table.getHeaderHeight()).reduced(16, 0);
        auto& t = ThemeManager::getInstance();
        UiStyle::drawIcon(g, "megaphone", area.toFloat().removeFromTop(area.getHeight() * 0.5f)
                                              .withTrimmedTop(area.getHeight() * 0.18f).withSizeKeepingCentre(36.0f, 36.0f),
                          t.getMutedTextColour());
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(13.5f));
        g.drawFittedText(TRANS("msg_no_ads"), area.withTrimmedTop(area.getHeight() / 2), juce::Justification::centredTop, 3);
    }
}

void AdsListView::lookAndFeelChanged() {
    table.setColour(juce::ListBox::backgroundColourId, ThemeManager::getInstance().getPanelBackgroundColour());
    refreshButton.setTooltip(TRANS("btn_refresh_list"));
    table.repaint();
}

void AdsListView::resized()
{
    auto area = getLocalBounds().withTrimmedTop(UiStyle::gap);
    auto toolbar = area.removeFromTop(UiStyle::controlHeight);
    refreshButton.setBounds(toolbar.removeFromRight(UiStyle::controlHeight));
    titleArea = toolbar;
    area.removeFromTop(UiStyle::gap);

    table.setBounds(area);

    const int fixed = 64 + 72 + table.getViewport()->getScrollBarThickness();
    const int flexible = juce::jmax(120, table.getWidth() - fixed);
    table.getHeader().setColumnWidth(2, (int) (flexible * 0.6f));
    table.getHeader().setColumnWidth(3, flexible - (int) (flexible * 0.6f));
}

void AdsListView::visibilityChanged()
{
    if (isVisible()) {
        refreshList();
    }
}

int AdsListView::getNumRows()
{
    return (int)cachedTracks.size();
}

void AdsListView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int height, bool rowIsSelected)
{
    auto& t = ThemeManager::getInstance();
    if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }
}

void AdsListView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool)
{
    if (rowNumber >= (int) cachedTracks.size()) return;

    auto& t = ThemeManager::getInstance();
    const auto& track = cachedTracks[(size_t) rowNumber];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    if (columnId == 1) {
        const auto chipArea = area.toFloat().withSizeKeepingCentre((float) area.getWidth(), 20.0f);
        if (identificationIds.count(track.id)) {
            UiStyle::drawChip(g, chipArea, "ID", t.getRaisedColour(), t.getSecondaryTextColour());
        } else {
            const int num = adsManager.getCueNumber(track.id);
            if (num != -1)
                UiStyle::drawChip(g, chipArea, juce::String(num).paddedLeft('0', 2),
                                  t.getCueOnColour().withAlpha(0.2f), t.getCueOnColour().interpolatedWith(t.getPrimaryTextColour(), 0.2f));
            else {
                g.setColour(t.getMutedTextColour());
                g.setFont(UiStyle::mono(13.0f));
                g.drawText("--", area, juce::Justification::centredLeft, false);
            }
        }
    }
    else if (columnId == 2) {
        g.setColour(t.getPrimaryTextColour());
        g.setFont(UiStyle::ui(14.5f));
        g.drawText(TrackNaming::display(track.title), area, juce::Justification::centredLeft, true);
    }
    else if (columnId == 3) {
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(14.0f));
        g.drawText(TrackNaming::display(track.artist), area, juce::Justification::centredLeft, true);
    }
    else if (columnId == 4) {
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::mono(13.0f));
        g.drawText(UiStyle::formatTime(track.duration), area, juce::Justification::centredRight, false);
    }
}

void AdsListView::cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&)
{
    if (columnId == 1)  
    {
        editCueNumber(rowNumber);
    }
    else  
    {
        if (rowNumber < cachedTracks.size()) {
            auto& track = cachedTracks[rowNumber];
            
            auto* dialog = new TrackDetailsDialog(library, track);
            
            juce::DialogWindow::LaunchOptions opt;
            opt.content.setOwned(dialog);
            opt.dialogTitle = TRANS("track_info");
            opt.dialogBackgroundColour = ThemeManager::getInstance().getBaseBackgroundColour();
            opt.escapeKeyTriggersCloseButton = true;
            opt.useNativeTitleBar = true;
            opt.resizable = true;
            
            auto* w = opt.launchAsync();
            w->centreWithSize(dialog->getWidth(), dialog->getHeight());
            
            dialog->onSave = [this] {
                refreshList();
            };
            dialog->onCancel = [w] { w->closeButtonPressed(); };
        }
    }
}

 
juce::var AdsListView::getDragSourceDescription(const juce::SparseSet<int>& selectedRows)
{
    if (selectedRows.isEmpty()) return {};
    juce::StringArray paths;
    for (int i = 0; i < selectedRows.size(); ++i) {
        int row = selectedRows[i];
        if (row < cachedTracks.size()) paths.add(cachedTracks[row].path);
    }
    if (paths.isEmpty()) return {};
    return "track_list_" + paths.joinIntoString("|");
}

 
void AdsListView::cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu()) 
    {
        if (rowNumber < cachedTracks.size()) {
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
             
              
             m.addItem(2, TRANS("menu_edit_cue"));
             m.addSeparator();
             
              
             m.addItem(10, TRANS("menu_add_to_start"));
             m.addItem(11, TRANS("menu_add_to_end"));
             if (cartPlayer != nullptr && selectedRows.size() == 1)
                 m.addSubMenu(TRANS("menu_assign_cart"), CartwallView::createAssignMenu(*cartPlayer, 5000));
             m.addSeparator();
             
              
             auto categories = library.getCategories();
             
             juce::PopupMenu addCatMenu;
             for (const auto& cat : categories) addCatMenu.addItem(1000 + cat.id, MusicLibrary::getDisplayName(cat));
             m.addSubMenu(TRANS("menu_add_to_category"), addCatMenu);

             juce::PopupMenu removeCatMenu;
             for (const auto& cat : categories) removeCatMenu.addItem(2000 + cat.id, MusicLibrary::getDisplayName(cat));
             m.addSubMenu(TRANS("menu_remove_from_category"), removeCatMenu);

             m.addItem(3000, TRANS("menu_remove_from_all_categories"));
             m.addSeparator();

             m.addItem(4000, TRANS("menu_open_location"));
             
             m.showMenuAsync(juce::PopupMenu::Options(), [this, selectedRows](int res) {
                 if (res == 1 && selectedRows.size() == 1) {
                     const int row = selectedRows[0];
                     if (row < (int) cachedTracks.size() && deckA != nullptr && deckB != nullptr)
                         OnAir::requestPlayNow(*deckA, *deckB, cachedTracks[(size_t) row]);
                 }
                 else if (res >= 5000 && res < 5000 + CartPlayer::numCarts && cartPlayer != nullptr) {
                     if (selectedRows[0] < (int) cachedTracks.size())
                         cartPlayer->assignFromLibrary(res - 5000, cachedTracks[(size_t) selectedRows[0]]);
                 }
                 else if (res == 2 && selectedRows.size() == 1) {
                     editCueNumber(selectedRows[0]);
                 }
                 else if (res == 10) {  
                     if (playlist) {
                         std::vector<TrackInfo> tracks;
                         for (int i=0; i<selectedRows.size(); ++i) {
                             int row = selectedRows[i];
                             if (row < cachedTracks.size()) tracks.push_back(cachedTracks[row]);
                         }
                         playlist->insertTracks(tracks, 0);
                     }
                 }
                 else if (res == 11) {  
                     if (playlist) {
                         for (int i=0; i<selectedRows.size(); ++i) {
                             int row = selectedRows[i];
                             if (row < cachedTracks.size()) playlist->addTrack(cachedTracks[row]);
                         }
                     }
                 }
                 else if (res >= 1000 && res < 2000) {  
                      int catId = res - 1000;
                      for (int i=0; i<selectedRows.size(); ++i) {
                          int row = selectedRows[i];
                          if (row < cachedTracks.size()) {
                              int trackId = cachedTracks[row].id;
                              auto currentCats = library.getTrackCategories(trackId);
                              bool exists = std::find(currentCats.begin(), currentCats.end(), catId) != currentCats.end();
                              if (!exists) {
                                  currentCats.push_back(catId);
                                  library.setTrackCategories(trackId, currentCats);
                              }
                          }
                      }
                      refreshList();
                 }
                 else if (res >= 2000 && res < 3000) {  
                      int catId = res - 2000;
                      for (int i=0; i<selectedRows.size(); ++i) {
                          int row = selectedRows[i];
                          if (row < cachedTracks.size()) {
                              int trackId = cachedTracks[row].id;
                              auto currentCats = library.getTrackCategories(trackId);
                              auto it = std::remove(currentCats.begin(), currentCats.end(), catId);
                              if (it != currentCats.end()) {
                                  currentCats.erase(it, currentCats.end());
                                  library.setTrackCategories(trackId, currentCats);
                              }
                          }
                      }
                      refreshList();
                 }
                 else if (res == 3000) {  
                      for (int i=0; i<selectedRows.size(); ++i) {
                          int row = selectedRows[i];
                          if (row < cachedTracks.size()) library.setTrackCategories(cachedTracks[row].id, {});
                      }
                      refreshList();
                 }
                 else if (res == 4000 && selectedRows.size() == 1) {
                     int row = selectedRows[0];
                     if (row < cachedTracks.size()) {
                        juce::File f(cachedTracks[row].path);
                        if (f.exists()) External::reveal(f);
                     }
                 }
             });
        }
    }
}

void AdsListView::editCueNumber(int row)
{
    if (row >= cachedTracks.size()) return;
    auto trackId = cachedTracks[row].id;
    
    if (identificationIds.count(trackId)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("msg_restricted"), TRANS("msg_id_no_cue"));
        return;
    }

    int currentNum = adsManager.getCueNumber(trackId);
    
    auto* window = new juce::AlertWindow(TRANS("dialog_assign_cue"), TRANS("dialog_enter_cue"), juce::AlertWindow::QuestionIcon);
    window->addTextEditor("number", currentNum != -1 ? juce::String(currentNum) : "", TRANS("label_number"));
    if (auto* editor = window->getTextEditor("number"))
        editor->setInputRestrictions(2, "0123456789");  // 0 a 99; vacío quita el número
    window->addButton(TRANS("ok"), 1);
    window->addButton(TRANS("cancel"), 0);
    
    window->enterModalState(true, juce::ModalCallbackFunction::create([this, trackId, window](int result) {
        if (result == 1)
        {
            auto text = window->getTextEditorContents("number");
            
             
            if (text.trim().isEmpty()) {
                 adsManager.setCueNumber(trackId, -1);
                 if (onSelectionChanged) onSelectionChanged(-1);
            }
            else {
                int newNum = text.getIntValue();
                if (newNum >= 0 && newNum <= 99) {
                    adsManager.setCueNumber(trackId, newNum); 
                    if (onSelectionChanged) onSelectionChanged(newNum);
                }
            }
        }
        delete window; 
    }));
}
