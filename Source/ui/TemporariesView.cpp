#include "TemporariesView.h"
#include "CartwallView.h"
#include "../audio/Deck.h"
#include "../audio/PreviewPlayer.h"
#include "../autodj/OnAir.h"
#include "../library/Playlist.h"
#include "../library/TrackNaming.h"
#include "../network/LinkDownload.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

namespace {
    enum Columns { colTitle = 1, colAuthor, colAired, colLength, colAdded };

    // "10:42" si llegó hoy, "ayer 18:03", "22/11 09:10" (las fechas se guardan en hora local).
    juce::String whenAdded(const std::string& createdAt) {
        const auto t = parseLocalTimestamp(createdAt);
        if (t.toMilliseconds() == 0) return {};
        const auto now = juce::Time::getCurrentTime();
        const auto sameDay = [](juce::Time a, juce::Time b) {
            return a.getYear() == b.getYear() && a.getMonth() == b.getMonth() && a.getDayOfMonth() == b.getDayOfMonth();
        };
        if (sameDay(t, now)) return t.formatted("%H:%M");
        if (sameDay(t, now - juce::RelativeTime::days(1))) return TRANS("temp_yesterday") + " " + t.formatted("%H:%M");
        return t.formatted("%d/%m %H:%M");
    }
}

TemporariesView::TemporariesView(MusicLibrary& lib) : library(lib) {
    addAndMakeVisible(linkEditor);
    linkEditor.setIndents(34, 0);
    linkEditor.setJustification(juce::Justification::centredLeft);
    linkEditor.setFont(UiStyle::ui(14.0f));
    linkEditor.onReturnKey = [this] { startLinkDownload(); };
    linkEditor.onTextChange = [this] { updateButtons(); };

    addAndMakeVisible(downloadButton);
    UiStyle::setVariant(downloadButton, "primary");
    UiStyle::setIcon(downloadButton, "import");
    downloadButton.onClick = [this] { startLinkDownload(); };

    addChildComponent(linkActionButton);
    UiStyle::setVariant(linkActionButton, "ghost");
    // Una copia: la acción puede refrescar el estado, que reemplaza linkAction mientras corre.
    linkActionButton.onClick = [this] { if (auto action = linkAction) action(); };

    addAndMakeVisible(emptyButton);
    UiStyle::setVariant(emptyButton, "ghost");
    UiStyle::setIcon(emptyButton, "trash");
    emptyButton.onClick = [this] { emptyAll(); };

    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);
    table.getHeader().addColumn("", colTitle, 220, 100, -1, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn("", colAuthor, 140, 60, -1, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn("", colAired, 64, 56, 80, juce::TableHeaderComponent::notResizableOrSortable);
    table.getHeader().addColumn("", colLength, 72, 64, 90, juce::TableHeaderComponent::notResizableOrSortable);
    table.getHeader().addColumn("", colAdded, 96, 80, 120, juce::TableHeaderComponent::notResizableOrSortable);
    table.setMultipleSelectionEnabled(true);

    for (auto* b : { &cueButton, &airButton, &deckAButton, &deckBButton, &queueButton, &cartButton, &renameButton, &removeButton })
        addAndMakeVisible(b);
    UiStyle::setIcon(cueButton, "headphones");
    cueButton.setClickingTogglesState(false);
    cueButton.onClick = [this] { togglePreview(); };
    UiStyle::setVariant(airButton, "primary");
    UiStyle::setIcon(airButton, "broadcast");
    airButton.onClick = [this] { playNow(); };
    deckAButton.onClick = [this] { loadInto(deckA, "A"); };
    deckBButton.onClick = [this] { loadInto(deckB, "B"); };
    UiStyle::setIcon(queueButton, "list");
    queueButton.onClick = [this] { showQueueMenu(); };
    UiStyle::setIcon(cartButton, "grid");
    cartButton.onClick = [this] { showCartMenu(); };
    UiStyle::setVariant(renameButton, "ghost");
    UiStyle::setIcon(renameButton, "edit");
    renameButton.onClick = [this] { renameSelected(); };
    UiStyle::setVariant(removeButton, "ghost");
    UiStyle::setIcon(removeButton, "trash");
    removeButton.onClick = [this] { removeSelected(); };

    addChildComponent(importEmptyButton);
    UiStyle::setVariant(importEmptyButton, "primary");
    UiStyle::setIcon(importEmptyButton, "hourglass");
    importEmptyButton.onClick = [this] { if (onImportRequested) onImportRequested(); };

    lookAndFeelChanged();
    refresh();
    startTimer(250);
}

TemporariesView::~TemporariesView() {
    stopTimer();
    if (downloader != nullptr) downloader->removeChangeListener(this);
    table.setModel(nullptr);
}

void TemporariesView::setSongDownloader(SongDownloader* d) {
    if (downloader != nullptr) downloader->removeChangeListener(this);
    downloader = d;
    if (downloader != nullptr) downloader->addChangeListener(this);
    refreshLinkStatus();
    updateButtons();
}

//==============================================================================
// Lista

void TemporariesView::refresh() {
    std::set<int> selectedIds;
    for (const auto& t : selectedTracks()) selectedIds.insert(t.id);

    catalogVersionSeen = library.getCatalogVersion();
    tracks = library.getTemporaries();
    usage = currentUsage(&cartSlotOf);
    table.updateContent();

    // La selección sigue a las mismas pistas (lo nuevo entra arriba y corre las filas); lo que
    // acaba de bajar de un enlace queda elegido, listo para preescucharlo o ponerlo al aire.
    juce::SparseSet<int> rows;
    for (int i = 0; i < (int) tracks.size(); ++i) {
        if (selectAfterDownload != 0 && tracks[(size_t) i].id == selectAfterDownload) {
            rows.clear();
            rows.addRange({ i, i + 1 });
            selectAfterDownload = 0;
            break;
        }
        if (selectedIds.count(tracks[(size_t) i].id) > 0) rows.addRange({ i, i + 1 });
    }
    table.setSelectedRows(rows, juce::dontSendNotification);

    importEmptyButton.setVisible(tracks.empty());
    updateButtons();
    resized();
    repaint();
}

std::vector<TrackInfo> TemporariesView::selectedTracks() const {
    std::vector<TrackInfo> result;
    const auto rows = table.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i] >= 0 && rows[i] < (int) tracks.size()) result.push_back(tracks[(size_t) rows[i]]);
    return result;
}

TemporaryAudio::Usage TemporariesView::currentUsage(std::map<int, int>* cartSlots) const {
    TemporaryAudio::Usage u;
    for (auto* deck : { deckA, deckB })
        if (deck != nullptr && deck->isLoaded()) u.onDecks.insert(deck->getCurrentTrack().id);
    if (cartSlots != nullptr) cartSlots->clear();
    if (cartPlayer != nullptr) {
        for (int slot = 0; slot < CartPlayer::numCarts; ++slot) {
            if (!cartPlayer->isAssigned(slot)) continue;
            const int id = cartPlayer->getTrack(slot).id;
            if (cartPlayer->isPlaying(slot)) u.playingCarts.insert(id);
            if (cartSlots != nullptr && cartSlots->count(id) == 0) (*cartSlots)[id] = slot;
        }
    }
    if (playlist != nullptr)
        for (const auto& t : playlist->getQueue()) u.queued.insert(t.id);
    return u;
}

juce::String TemporariesView::displayTitle(const TrackInfo& track) const {
    return TrackNaming::display(track.title);
}

juce::String TemporariesView::busyText(const TrackInfo& track, TemporaryAudio::Busy busy) const {
    const auto key = busy == TemporaryAudio::Busy::OnDeck ? "temp_busy_deck"
                   : busy == TemporaryAudio::Busy::InCart ? "temp_busy_cart" : "temp_busy_queue";
    return TRANS(key).replace("%1", displayTitle(track));
}

void TemporariesView::flash(const juce::String& text, juce::Colour colour) {
    flashText = text;
    flashColour = colour;
    flashUntil = juce::Time::getMillisecondCounter() + 6000;
    repaint();
}

int TemporariesView::getNumRows() { return (int) tracks.size(); }

void TemporariesView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }
}

void TemporariesView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber < 0 || rowNumber >= (int) tracks.size()) return;
    auto& t = ThemeManager::getInstance();
    const auto& track = tracks[(size_t) rowNumber];
    auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    if (columnId == colTitle) {
        // Lo que se está preescuchando lleva los audífonos delante.
        if (previewPlayer != nullptr && previewPlayer->isPlaying() && previewPlayer->getTrackId() == track.id) {
            UiStyle::drawIcon(g, "headphones", area.removeFromLeft(16).toFloat().withSizeKeepingCentre(15.0f, 15.0f),
                              t.getCueOnColour());
            area.removeFromLeft(6);
        }

        // Marcas a la derecha: en un deck, en la cola, en la botonera (por eso no se puede borrar).
        juce::StringArray chips;
        std::vector<juce::Colour> colours;
        if (usage.onDecks.count(track.id) > 0) { chips.add(TRANS("temp_chip_deck")); colours.push_back(t.getAirOnColour()); }
        if (usage.queued.count(track.id) > 0) { chips.add(TRANS("temp_chip_queued")); colours.push_back(t.getHighlightColour()); }
        if (const auto it = cartSlotOf.find(track.id); it != cartSlotOf.end()) {
            chips.add("F" + juce::String(it->second + 1));
            colours.push_back(t.getSecondaryTextColour());
        }
        for (int i = chips.size(); --i >= 0;) {
            const float w = UiStyle::getChipWidth(chips[i], 18.0f);
            if ((float) area.getWidth() < w + 60.0f) break;
            auto chip = area.removeFromRight((int) std::ceil(w)).toFloat().withSizeKeepingCentre(w, 18.0f);
            UiStyle::drawChip(g, chip, chips[i], colours[(size_t) i].withAlpha(0.18f),
                              colours[(size_t) i].interpolatedWith(t.getPrimaryTextColour(), 0.25f));
            area.removeFromRight(6);
        }

        g.setColour(t.getPrimaryTextColour());
        g.setFont(UiStyle::ui(14.5f));
        g.drawText(displayTitle(track), area, juce::Justification::centredLeft, true);
    } else if (columnId == colAuthor) {
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(14.0f));
        g.drawText(TrackNaming::display(track.artist), area, juce::Justification::centredLeft, true);
    } else if (columnId == colAired) {
        g.setColour(track.playCount > 0 ? t.getSecondaryTextColour() : t.getMutedTextColour());
        g.setFont(UiStyle::mono(13.0f));
        g.drawText(juce::String(track.playCount), area, juce::Justification::centredRight, false);
    } else if (columnId == colLength) {
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::mono(13.0f));
        g.drawText(UiStyle::formatTime(track.getAirDuration()), area, juce::Justification::centredRight, false);
    } else if (columnId == colAdded) {
        g.setColour(t.getMutedTextColour());
        g.setFont(UiStyle::ui(13.0f));
        g.drawText(whenAdded(track.createdAt), area, juce::Justification::centredRight, true);
    }
}

juce::String TemporariesView::getCellTooltip(int rowNumber, int) {
    if (rowNumber < 0 || rowNumber >= (int) tracks.size()) return {};
    const auto& track = tracks[(size_t) rowNumber];
    const auto busy = TemporaryAudio::whyBusy(track.id, usage);
    return busy != TemporaryAudio::Busy::No ? busyText(track, busy) : TRANS("tip_temp_row");
}

void TemporariesView::cellClicked(int rowNumber, int, const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu() || rowNumber < 0 || rowNumber >= (int) tracks.size()) return;
    if (!table.isRowSelected(rowNumber)) table.selectRow(rowNumber);
    const bool one = table.getNumSelectedRows() == 1;

    juce::PopupMenu m;
    m.addItem(1, TRANS("temp_preview"), one && previewPlayer != nullptr);
    m.addItem(2, TRANS("menu_play_now"), one && deckA != nullptr && deckB != nullptr);
    m.addItem(3, TRANS("temp_load_deck").replace("%1", "A"), one && deckA != nullptr);
    m.addItem(4, TRANS("temp_load_deck").replace("%1", "B"), one && deckB != nullptr);
    m.addSeparator();
    m.addItem(5, TRANS("temp_queue_next"), playlist != nullptr);
    m.addItem(6, TRANS("temp_queue_end"), playlist != nullptr);
    if (cartPlayer != nullptr && one)
        m.addSubMenu(TRANS("menu_assign_cart"), CartwallView::createAssignMenu(*cartPlayer, 100));
    m.addSeparator();
    m.addItem(7, TRANS("temp_rename"), one);
    m.addItem(8, TRANS("temp_remove"));

    juce::Component::SafePointer<TemporariesView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options(), [safe](int result) {
        if (safe == nullptr || result <= 0) return;
        switch (result) {
            case 1: safe->togglePreview(); break;
            case 2: safe->playNow(); break;
            case 3: safe->loadInto(safe->deckA, "A"); break;
            case 4: safe->loadInto(safe->deckB, "B"); break;
            case 5: safe->addToQueue(true); break;
            case 6: safe->addToQueue(false); break;
            case 7: safe->renameSelected(); break;
            case 8: safe->removeSelected(); break;
            default:
                if (result >= 100 && result < 100 + CartPlayer::numCarts && safe->cartPlayer != nullptr) {
                    const auto sel = safe->selectedTracks();
                    if (!sel.empty() && safe->cartPlayer->assignFromLibrary(result - 100, sel.front()))
                        safe->flash(TRANS("temp_assigned_cart").replace("%1", "F" + juce::String(result - 99)),
                                    ThemeManager::getInstance().getSuccessColour());
                    safe->refresh();
                }
                break;
        }
    });
}

void TemporariesView::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) {
    // Doble clic: preescuchar por CUE (nunca sale al aire sin querer).
    if (rowNumber < 0 || rowNumber >= (int) tracks.size()) return;
    table.selectRow(rowNumber);
    togglePreview();
}

void TemporariesView::selectedRowsChanged(int) { updateButtons(); }

void TemporariesView::deleteKeyPressed(int) { removeSelected(); }

juce::var TemporariesView::getDragSourceDescription(const juce::SparseSet<int>& selectedRows) {
    juce::StringArray paths;
    for (int i = 0; i < selectedRows.size(); ++i)
        if (selectedRows[i] >= 0 && selectedRows[i] < (int) tracks.size()) paths.add(tracks[(size_t) selectedRows[i]].path);
    if (paths.isEmpty()) return {};
    return "track_list_" + paths.joinIntoString("|");  // como la biblioteca: a un deck, la cola o la botonera
}

//==============================================================================
// Archivos soltados sobre la lista: entran como temporales.

bool TemporariesView::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (library.isImportable(juce::File(f))) return true;
    return false;
}

void TemporariesView::fileDragEnter(const juce::StringArray&, int, int) { fileDragOver = true; repaint(); }
void TemporariesView::fileDragExit(const juce::StringArray&) { fileDragOver = false; repaint(); }

void TemporariesView::filesDropped(const juce::StringArray& files, int, int) {
    fileDragOver = false;
    repaint();
    if (library.isImporting()) {
        flash(TRANS("temp_import_busy"), ThemeManager::getInstance().getWarningColour());
        return;
    }
    juce::Array<juce::File> list;
    for (const auto& f : files)
        if (library.isImportable(juce::File(f))) list.add(juce::File(f));
    if (!list.isEmpty()) library.importTemporaries(list);
}

//==============================================================================
// Acciones

void TemporariesView::togglePreview() {
    if (previewPlayer == nullptr) return;
    const auto sel = selectedTracks();
    if (previewPlayer->isPlaying() && (sel.empty() || previewPlayer->getTrackId() == sel.front().id)) {
        previewPlayer->stop();
    } else if (!sel.empty() && !previewPlayer->play(sel.front(), true)) {
        flash(TRANS("temp_cannot_read").replace("%1", displayTitle(sel.front())), ThemeManager::getInstance().getWarningColour());
    }
    updateButtons();
    table.repaint();
}

void TemporariesView::playNow() {
    const auto sel = selectedTracks();
    if (sel.size() != 1 || deckA == nullptr || deckB == nullptr) return;
    if (previewPlayer != nullptr && previewPlayer->getTrackId() == sel.front().id) previewPlayer->stop();
    OnAir::requestPlayNow(*deckA, *deckB, sel.front());
}

void TemporariesView::loadInto(Deck* deck, const juce::String& deckName) {
    const auto sel = selectedTracks();
    if (deck == nullptr || sel.size() != 1) return;
    // Cargar reemplaza lo que tiene el deck: si está al aire, cortaría la transmisión.
    if (OnAir::isOnAir(*deck)) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("deck_on_air_title"), TRANS("deck_on_air_msg"));
        return;
    }
    deck->unload();
    if (deck->loadTrack(juce::File(juce::String::fromUTF8(sel.front().path.c_str()))))
        flash(TRANS("temp_loaded").replace("%1", displayTitle(sel.front())).replace("%2", deckName),
              ThemeManager::getInstance().getSuccessColour());
    else
        flash(TRANS("temp_cannot_read").replace("%1", displayTitle(sel.front())), ThemeManager::getInstance().getWarningColour());
    usage = currentUsage(&cartSlotOf);
    table.repaint();
}

void TemporariesView::showQueueMenu() {
    if (playlist == nullptr || selectedTracks().empty()) return;
    juce::PopupMenu m;
    m.addItem(1, TRANS("temp_queue_next"));
    m.addItem(2, TRANS("temp_queue_end"));
    juce::Component::SafePointer<TemporariesView> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&queueButton), [safe](int result) {
        if (safe != nullptr && result > 0) safe->addToQueue(result == 1);
    });
}

void TemporariesView::addToQueue(bool next) {
    const auto sel = selectedTracks();
    if (playlist == nullptr || sel.empty()) return;
    if (next) playlist->insertTracks(sel, 0);
    else for (const auto& t : sel) playlist->addTrack(t);
    const auto what = sel.size() == 1 ? displayTitle(sel.front()) : juce::String((int) sel.size());
    flash(TRANS(next ? (sel.size() == 1 ? "temp_queued_next_one" : "temp_queued_next_many")
                     : (sel.size() == 1 ? "temp_queued_end_one" : "temp_queued_end_many")).replace("%1", what),
          ThemeManager::getInstance().getSuccessColour());
    usage = currentUsage(&cartSlotOf);
    table.repaint();
}

void TemporariesView::showCartMenu() {
    const auto sel = selectedTracks();
    if (cartPlayer == nullptr || sel.size() != 1) return;
    juce::Component::SafePointer<TemporariesView> safe(this);
    CartwallView::createAssignMenu(*cartPlayer, 1).showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(&cartButton), [safe, track = sel.front()](int result) {
            if (safe == nullptr || result < 1 || result > CartPlayer::numCarts || safe->cartPlayer == nullptr) return;
            if (safe->cartPlayer->assignFromLibrary(result - 1, track))
                safe->flash(TRANS("temp_assigned_cart").replace("%1", "F" + juce::String(result)),
                            ThemeManager::getInstance().getSuccessColour());
            else
                safe->flash(TRANS("temp_cannot_read").replace("%1", safe->displayTitle(track)),
                            ThemeManager::getInstance().getWarningColour());
            safe->refresh();
        });
}

void TemporariesView::renameSelected() {
    const auto sel = selectedTracks();
    if (sel.size() != 1) return;
    const auto track = sel.front();

    auto* window = new juce::AlertWindow(TRANS("temp_rename_title"), {}, juce::AlertWindow::NoIcon, this);
    window->addTextEditor("title", juce::String::fromUTF8(track.title.c_str()), TRANS("temp_rename_label_title"));
    window->addTextEditor("author", juce::String::fromUTF8(track.artist.c_str()), TRANS("temp_rename_label_author"));
    window->addButton(TRANS("ok"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(TRANS("cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<TemporariesView> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window, id = track.id](int result) {
        if (safe == nullptr || result != 1) return;
        const auto title = window->getTextEditorContents("title").trim();
        if (title.isEmpty()) return;
        safe->library.renameTemporary(id, title, window->getTextEditorContents("author").trim());
        safe->refresh();
    }), true);
}

void TemporariesView::releaseFiles(const std::vector<int>& trackIds) {
    const std::set<int> ids(trackIds.begin(), trackIds.end());
    if (previewPlayer != nullptr && ids.count(previewPlayer->getTrackId()) > 0) previewPlayer->stop();
    if (cartPlayer != nullptr)
        for (int slot = 0; slot < CartPlayer::numCarts; ++slot)
            if (cartPlayer->isAssigned(slot) && !cartPlayer->isPlaying(slot) && ids.count(cartPlayer->getTrack(slot).id) > 0)
                cartPlayer->clear(slot);
}

void TemporariesView::removeSelected() {
    const auto sel = selectedTracks();
    if (sel.empty()) return;

    // Lo que suena o está en la cola no se borra: se avisa por qué.
    const auto now = currentUsage(&cartSlotOf);
    std::vector<TrackInfo> removable;
    juce::StringArray kept, carts;
    for (const auto& t : sel) {
        const auto busy = TemporaryAudio::whyBusy(t.id, now);
        if (busy != TemporaryAudio::Busy::No) { kept.add(busyText(t, busy)); continue; }
        removable.push_back(t);
        if (const auto it = cartSlotOf.find(t.id); it != cartSlotOf.end()) carts.add("F" + juce::String(it->second + 1));
    }
    const auto title = TRANS(sel.size() == 1 ? "temp_remove_title_one" : "temp_remove_title_many");
    if (removable.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, title, kept.joinIntoString("\n"), {}, this);
        return;
    }

    auto text = removable.size() == 1 ? TRANS("temp_remove_confirm_one").replace("%1", displayTitle(removable.front()))
                                      : TRANS("temp_remove_confirm_many").replace("%1", juce::String((int) removable.size()));
    if (!carts.isEmpty()) text << "\n\n" << TRANS("temp_remove_carts").replace("%1", carts.joinIntoString(", "));
    if (!kept.isEmpty()) text << "\n\n" << kept.joinIntoString("\n");

    std::vector<int> ids;
    for (const auto& t : removable) ids.push_back(t.id);
    juce::Component::SafePointer<TemporariesView> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, title, text, TRANS("temp_remove_ok"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([safe, ids](int answer) {
            if (safe == nullptr || answer != 1) return;
            // Mientras se leía la pregunta, AutoDJ pudo cargar alguno en un deck.
            const auto use = safe->currentUsage();
            std::vector<int> now;
            for (int id : ids)
                if (TemporaryAudio::whyBusy(id, use) == TemporaryAudio::Busy::No) now.push_back(id);
            safe->releaseFiles(now);
            int deleted = 0, failed = 0;
            for (int id : now) (safe->library.deleteTemporary(id) ? deleted : failed)++;
            safe->refresh();
            auto& t = ThemeManager::getInstance();
            if (failed > 0 || (int) now.size() < (int) ids.size())
                safe->flash(TRANS("temp_removed_partial").replace("%1", juce::String(deleted))
                                                         .replace("%2", juce::String((int) ids.size() - deleted)), t.getWarningColour());
            else
                safe->flash(TRANS(deleted == 1 ? "temp_removed_one" : "temp_removed_many").replace("%1", juce::String(deleted)),
                            t.getSuccessColour());
        }));
}

void TemporariesView::emptyAll() {
    if (tracks.empty()) return;
    const auto now = currentUsage(&cartSlotOf);
    int busy = 0;
    juce::StringArray carts;
    for (const auto& t : tracks) {
        if (TemporaryAudio::whyBusy(t.id, now) != TemporaryAudio::Busy::No) { ++busy; continue; }
        if (const auto it = cartSlotOf.find(t.id); it != cartSlotOf.end()) carts.add("F" + juce::String(it->second + 1));
    }
    const int removable = (int) tracks.size() - busy;
    if (removable == 0) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("temp_empty_title"),
                                               TRANS("temp_empty_all_busy"), {}, this);
        return;
    }

    auto text = TRANS(removable == 1 ? "temp_empty_confirm_one" : "temp_empty_confirm").replace("%1", juce::String(removable));
    if (!carts.isEmpty()) text << "\n\n" << TRANS("temp_remove_carts").replace("%1", carts.joinIntoString(", "));
    if (busy > 0) text << "\n\n" << TRANS("temp_empty_busy").replace("%1", juce::String(busy));

    juce::Component::SafePointer<TemporariesView> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, TRANS("temp_empty_title"), text,
                                       TRANS("temp_empty_ok"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([safe](int answer) {
            if (safe == nullptr || answer != 1) return;
            const auto use = safe->currentUsage();
            std::vector<int> ids;
            for (const auto& t : safe->library.getTemporaries())
                if (TemporaryAudio::whyBusy(t.id, use) == TemporaryAudio::Busy::No) ids.push_back(t.id);
            safe->releaseFiles(ids);
            const auto result = TemporaryAudio::emptyAll(safe->library, use);
            safe->refresh();
            auto& t = ThemeManager::getInstance();
            if (result.busy + result.failed > 0)
                safe->flash(TRANS("temp_removed_partial").replace("%1", juce::String(result.deleted))
                                                         .replace("%2", juce::String(result.busy + result.failed)), t.getWarningColour());
            else
                safe->flash(TRANS("temp_emptied").replace("%1", juce::String(result.deleted)), t.getSuccessColour());
        }));
}

//==============================================================================
// Enlaces de redes sociales

void TemporariesView::startLinkDownload() {
    if (downloader == nullptr) return;
    const auto url = LinkDownload::normalise(linkEditor.getText());
    if (url.isEmpty()) {
        flash(TRANS(linkEditor.getText().trim().isEmpty() ? "temp_link_empty" : "temp_link_invalid"),
              ThemeManager::getInstance().getWarningColour());
        return;
    }
    confirmLinkDownload(0, url);
}

void TemporariesView::confirmLinkDownload(int step, const juce::String& url) {
    if (downloader == nullptr) return;
    juce::Component::SafePointer<TemporariesView> safe(this);
    auto ask = [this, safe](juce::MessageBoxIconType icon, const juce::String& title, const juce::String& text,
                            const juce::String& ok, const std::function<void()>& onOk) {
        juce::AlertWindow::showOkCancelBox(icon, title, text, ok, TRANS("cancel"), this,
            juce::ModalCallbackFunction::create([safe, onOk](int answer) { if (safe != nullptr && answer == 1) onOk(); }));
    };

    // 1. Una vez por emisora: los permisos, y que no se usa la sesión del navegador.
    if (step <= 0 && !downloader->isAcknowledged(true)) {
        ask(juce::AlertWindow::WarningIcon, TRANS("temp_link_notice_title"), TRANS("temp_link_notice"),
            TRANS("online_download_notice_ok"), [safe, url] {
                safe->downloader->acknowledge(true);
                safe->confirmLinkDownload(1, url);
            });
        return;
    }
    // 2. Las herramientas, si faltan: se instalan y el enlace espera su turno.
    if (step <= 1) {
        const auto missing = downloader->getMissingTools();
        if (!missing.empty() && !downloader->getStatus().installing) {
            juce::StringArray names;
            for (const auto& a : missing) names.add(a.name);
            ask(juce::AlertWindow::QuestionIcon, TRANS("online_tools_title"),
                TRANS("online_tools_text").replace("%1", names.joinIntoString(", "))
                                          .replace("%2", juce::String(DownloadTools::totalMegabytes(missing)))
                                          .replace("%3", DownloadTools::defaultFolder().getFullPathName()),
                TRANS("online_tools_install"), [safe, url] {
                    safe->downloader->installTools();
                    safe->confirmLinkDownload(2, url);
                });
            return;
        }
    }

    if (downloader->enqueueLink(url) == 0) {
        flash(TRANS("temp_link_already"), ThemeManager::getInstance().getWarningColour());
        return;
    }
    linkEditor.clear();
    dismissedLinkJob = 0;
    refreshLinkStatus();
    updateButtons();
}

juce::String TemporariesView::linkProblemText(const SongDownloader::Job& job) const {
    using P = SongDownload::Problem;
    const auto site = job.site.isNotEmpty() ? job.site : TRANS("temp_link_site_generic");
    switch (job.problem) {
        case P::LoginRequired: return TRANS("temp_link_login").replace("%1", site);
        case P::NoAudio:       return TRANS("temp_link_no_audio").replace("%1", site);
        case P::Unsupported:   return TRANS("temp_link_unsupported").replace("%1", site);
        case P::Live:          return TRANS("temp_link_live");
        case P::Unavailable:   return TRANS("temp_link_unavailable");
        case P::AgeRestricted: return TRANS("temp_link_age").replace("%1", site);
        case P::Region:        return TRANS("temp_link_region");
        case P::RateLimited:
        case P::BotCheck:      return TRANS("temp_link_rate_limited").replace("%1", site);
        case P::Network:       return TRANS("temp_link_network").replace("%1", site);
        case P::Tools:         return TRANS("online_problem_tools");
        case P::Other:
            return job.detail == "import" ? TRANS("temp_link_import_failed")
                                          : TRANS("online_problem_other").replace("%1", job.detail.substring(0, 140));
        case P::None: break;
    }
    return {};
}

void TemporariesView::refreshLinkStatus() {
    juce::String text, action;
    std::function<void()> act;
    auto& t = ThemeManager::getInstance();
    auto colour = t.getSecondaryTextColour();
    bool bar = false;
    double progress = -1.0;

    if (downloader != nullptr) {
        const auto s = downloader->getStatus();
        const auto jobs = downloader->getJobs(SongDownloader::Kind::Temporary);
        const auto now = juce::Time::getCurrentTime();
        const SongDownloader::Job* active = nullptr;
        const SongDownloader::Job* firstQueued = nullptr;
        const SongDownloader::Job* last = nullptr;  // el último que terminó
        int queued = 0;
        for (const auto& j : jobs) {
            if (j.id == s.activeJob) active = &j;
            if (j.state == SongDownloader::State::Queued) { ++queued; if (firstQueued == nullptr) firstQueued = &j; }
            if (j.isFinished() && j.state != SongDownloader::State::Cancelled) last = &j;
        }
        auto name = [](const SongDownloader::Job& j) {
            return j.title.isNotEmpty() ? "\"" + TrackNaming::display(j.title) + "\"" : j.site;
        };

        if (s.installing && (active != nullptr || queued > 0)) {
            text = TRANS("online_tools_installing").replace("%1", s.installStep);
            bar = true;
            progress = s.installProgress;
            colour = t.getHighlightColour();
        } else if (s.installError.isNotEmpty() && queued > 0) {
            text = TRANS("online_tools_failed").replace("%1", s.installErrorStep).replace("%2", TRANS("online_tools_error_" + s.installError));
            colour = t.getErrorColour();
            action = TRANS("online_tools_retry");
            act = [this] { if (downloader != nullptr) downloader->installTools(); };
        } else if (active != nullptr) {
            if (active->state == SongDownloader::State::Importing)
                text = TRANS("temp_link_importing").replace("%1", name(*active));
            else if (active->progress >= 0.999)
                text = TRANS("temp_link_converting").replace("%1", name(*active));
            else if (active->progress > 0.0)
                text = TRANS("temp_link_downloading").replace("%1", name(*active))
                                                     .replace("%2", juce::String(juce::roundToInt(active->progress * 100)));
            else
                text = TRANS("temp_link_starting").replace("%1", active->site);
            bar = true;
            progress = active->state == SongDownloader::State::Downloading && active->progress < 0.999 ? active->progress : -1.0;
            colour = t.getPrimaryTextColour();
            if (active->state == SongDownloader::State::Downloading) {
                action = TRANS("online_download_cancel_short");
                act = [this, id = active->id] { if (downloader != nullptr) downloader->cancel(id); };
            }
        } else if (firstQueued != nullptr) {
            const bool youtube = LinkDownload::isYouTube(firstQueued->url);
            if (youtube && s.pausedUntil > now)
                text = TRANS("temp_link_paused_youtube").replace("%1", s.pausedUntil.formatted("%H:%M"));
            else if (s.nextStart > now)
                text = TRANS("temp_link_waiting").replace("%1", UiStyle::formatTime(std::ceil((s.nextStart - now).inSeconds())));
            else
                text = s.activeJob != 0 ? TRANS("temp_link_after_song") : TRANS("temp_link_starting").replace("%1", firstQueued->site);
            if (queued > 1) text << "  (" << TRANS("online_download_queued").replace("%1", juce::String(queued)) << ")";
            action = TRANS("online_download_cancel_short");
            act = [this, id = firstQueued->id] { if (downloader != nullptr) downloader->cancel(id); };
        } else if (last != nullptr && last->id != dismissedLinkJob) {
            const bool success = last->state == SongDownloader::State::Done || last->state == SongDownloader::State::Duplicate;
            if (success && last->trackId != -1 && last->id != selectedLinkJob) {
                // Recién terminado: se selecciona en la lista.
                selectAfterDownload = last->trackId;
                selectedLinkJob = last->id;
                refresh();
            }
            if (last->state == SongDownloader::State::Done) {
                text = TRANS("temp_link_done").replace("%1", name(*last));
                colour = t.getSuccessColour();
            } else if (last->state == SongDownloader::State::Duplicate) {
                text = TRANS("temp_link_duplicate");
                colour = t.getSuccessColour();
            } else {
                text = linkProblemText(*last);
                colour = t.getErrorColour();
                action = TRANS("online_download_retry");
                act = [this, id = last->id] { if (downloader != nullptr) { dismissedLinkJob = 0; downloader->retry(id); } };
            }
            if (action.isEmpty()) {
                action = TRANS("temp_link_dismiss");
                act = [this, id = last->id] { dismissedLinkJob = id; refreshLinkStatus(); };
            }
        }
    }

    const bool layoutChanged = text.isNotEmpty() != linkText.isNotEmpty() || bar != linkBar
                            || action.isNotEmpty() != linkActionButton.isVisible() || action != linkActionButton.getButtonText();
    const bool changed = text != linkText || colour != linkColour || progress != linkProgress;
    linkText = text;
    linkColour = colour;
    linkBar = bar;
    linkProgress = progress;
    linkAction = act;
    linkActionButton.setButtonText(action);
    linkActionButton.setVisible(action.isNotEmpty());
    if (layoutChanged) resized();
    if (changed || layoutChanged) repaint();
}

//==============================================================================

void TemporariesView::updateButtons() {
    const auto sel = selectedTracks();
    const bool one = sel.size() == 1, any = !sel.empty();
    const bool previewing = previewPlayer != nullptr && previewPlayer->isPlaying()
                         && (!one || previewPlayer->getTrackId() == sel.front().id);

    cueButton.setEnabled(previewPlayer != nullptr && (one || previewing));
    cueButton.setToggleState(previewing, juce::dontSendNotification);
    airButton.setEnabled(one && deckA != nullptr && deckB != nullptr);
    deckAButton.setEnabled(one && deckA != nullptr);
    deckBButton.setEnabled(one && deckB != nullptr);
    queueButton.setEnabled(any && playlist != nullptr);
    cartButton.setEnabled(one && cartPlayer != nullptr);
    renameButton.setEnabled(one);
    removeButton.setEnabled(any);
    emptyButton.setEnabled(!tracks.empty());

    const bool linksAvailable = downloader != nullptr;
    linkEditor.setEnabled(linksAvailable);
    downloadButton.setEnabled(linksAvailable && linkEditor.getText().trim().isNotEmpty());

    const auto cueText = TRANS(previewing ? "temp_preview_stop" : "temp_preview");
    if (cueButton.getProperties()["fullText"].toString() != cueText) {
        cueButton.getProperties().set("fullText", cueText);
        layoutActions();
    }
}

void TemporariesView::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();
    linkEditor.setTextToShowWhenEmpty(TRANS("temp_link_placeholder"), t.getMutedTextColour());
    linkEditor.setTooltip(TRANS("tip_temp_link"));
    downloadButton.setButtonText(TRANS("temp_link_download"));
    downloadButton.setTooltip(TRANS("tip_temp_link"));
    emptyButton.setButtonText(TRANS("temp_empty"));
    emptyButton.setTooltip(TRANS("tip_temp_empty"));
    importEmptyButton.setButtonText(TRANS("import_temporal_btn"));
    importEmptyButton.setTooltip(TRANS("tip_import_temporal"));

    const std::pair<juce::TextButton*, const char*> buttons[] = {
        { &cueButton, "temp_preview" }, { &airButton, "temp_on_air" }, { &deckAButton, "temp_deck_a" },
        { &deckBButton, "temp_deck_b" }, { &queueButton, "temp_queue" }, { &cartButton, "temp_cart" },
        { &renameButton, "temp_rename_short" }, { &removeButton, "temp_remove" } };
    for (const auto& [button, key] : buttons) {
        button->getProperties().set("fullText", TRANS(key));
        button->setTooltip(TRANS(juce::String("tip_") + key));
    }

    table.getHeader().setColumnName(colTitle, TRANS("header_title"));
    table.getHeader().setColumnName(colAuthor, TRANS("temp_header_author"));
    table.getHeader().setColumnName(colAired, TRANS("temp_header_aired"));
    table.getHeader().setColumnName(colLength, TRANS("header_length"));
    table.getHeader().setColumnName(colAdded, TRANS("temp_header_added"));
    table.setColour(juce::ListBox::backgroundColourId, t.getPanelBackgroundColour());

    updateButtons();
    resized();
    repaint();
}

void TemporariesView::visibilityChanged() {
    if (isVisible()) refresh();
}

void TemporariesView::timerCallback() {
    if (library.getCatalogVersion() != catalogVersionSeen) refresh();

    // La cola, los decks y la botonera cambian solos (AutoDJ): las marcas de cada fila los siguen.
    std::map<int, int> slots;
    const auto now = currentUsage(&slots);
    if (now.onDecks != usage.onDecks || now.queued != usage.queued || now.playingCarts != usage.playingCarts || slots != cartSlotOf) {
        usage = now;
        cartSlotOf = slots;
        table.repaint();
    }

    const bool previewing = previewPlayer != nullptr && previewPlayer->isPlaying();
    if (previewing != cueButton.getToggleState() || previewing) {
        updateButtons();
        table.repaint();
    }
    if (flashText.isNotEmpty() && juce::Time::getMillisecondCounter() >= flashUntil) {
        flashText = {};
        repaint();
    }
    refreshLinkStatus();
    if (linkBar && linkProgress < 0.0) repaint(linkBarArea);  // el tramo que va y viene
}

void TemporariesView::changeListenerCallback(juce::ChangeBroadcaster*) {
    refreshLinkStatus();
}

//==============================================================================
// Dibujo y distribución

void TemporariesView::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();

    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    const int n = (int) tracks.size();
    g.drawText(n == 1 ? TRANS("temp_count_one") : TRANS("temp_count").replace("%1", juce::String(n)),
               countArea, juce::Justification::centredRight, false);

    // Estado del enlace (o el último mensaje), debajo del campo.
    const bool showFlash = flashText.isNotEmpty() && juce::Time::getMillisecondCounter() < flashUntil;
    if (showFlash || linkText.isNotEmpty()) {
        g.setColour(showFlash ? flashColour : linkColour);
        g.setFont(UiStyle::ui(13.0f));
        g.drawFittedText(showFlash ? flashText : linkText, linkStatusArea, juce::Justification::centredLeft, 2, 0.9f);
    } else if (!tracks.empty()) {
        // Sin nada que contar: cómo se usa la lista.
        g.setColour(t.getMutedTextColour());
        g.setFont(UiStyle::ui(12.5f));
        g.drawFittedText(TRANS("temp_hint"), linkStatusArea, juce::Justification::centredLeft, 1, 0.9f);
    }
    if (linkBar && !showFlash && !linkBarArea.isEmpty()) {
        const auto track = linkBarArea.toFloat();
        g.setColour(t.getRaisedColour());
        g.fillRoundedRectangle(track, track.getHeight() * 0.5f);
        g.setColour(t.getHighlightColour());
        if (linkProgress >= 0.0) {
            g.fillRoundedRectangle(track.withWidth(juce::jmax(track.getHeight(), track.getWidth() * (float) linkProgress)),
                                   track.getHeight() * 0.5f);
        } else {
            // Sin porcentaje (convirtiendo): un tramo que va y viene.
            const float phase = (float) (juce::Time::getMillisecondCounter() % 1600) / 1600.0f;
            const float w = track.getWidth() * 0.3f;
            const float x = track.getX() + (track.getWidth() - w) * (0.5f - 0.5f * std::cos(phase * juce::MathConstants<float>::twoPi));
            g.fillRoundedRectangle({ x, track.getY(), w, track.getHeight() }, track.getHeight() * 0.5f);
        }
    }

    // Separador sobre la barra de acciones.
    g.setColour(t.getOutlineColour());
    g.fillRect(actionsArea.getX(), actionsArea.getY() - UiStyle::gap / 2, actionsArea.getWidth(), 1);
}

void TemporariesView::paintOverChildren(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    UiStyle::drawIcon(g, "link", linkEditor.getBounds().toFloat().removeFromLeft(34.0f).withSizeKeepingCentre(15.0f, 15.0f),
                      t.getMutedTextColour());

    if (tracks.empty()) {
        auto area = table.getBounds().withTrimmedTop(table.getHeaderHeight());
        UiStyle::drawEmptyState(g, area.withTrimmedBottom(56), "hourglass", TRANS("temp_empty_state_title"),
                                TRANS("temp_empty_state_hint"));
    }
    if (fileDragOver) {
        const auto area = table.getBounds().toFloat().reduced(2.0f);
        g.setColour(t.getHighlightColour().withAlpha(0.08f));
        g.fillRoundedRectangle(area, UiStyle::controlRadius);
        g.setColour(t.getHighlightColour());
        g.drawRoundedRectangle(area, UiStyle::controlRadius, 2.0f);
    }
}

void TemporariesView::layoutActions() {
    // Con texto si cabe. Si no, por niveles (la ayuda emergente dice qué hace cada uno):
    // 1. "A" / "B" para los decks, y botonera, renombrar y quitar solo con su icono;
    // 2. todos solo con su icono.
    juce::TextButton* buttons[] = { &cueButton, &airButton, &deckAButton, &deckBButton, &queueButton, &cartButton,
                                    &renameButton, &removeButton };
    const int h = UiStyle::controlHeight;
    const int gap = 6;
    auto widthOf = [h](juce::TextButton& b) { return b.getButtonText().isEmpty() ? h + 8 : b.getBestWidthForHeight(h) + 22; };
    for (int level = 0; level <= 2; ++level) {
        for (auto* b : buttons) {
            const bool shortened = level >= 2 || (level == 1 && (b == &cartButton || b == &renameButton || b == &removeButton));
            if (b == &deckAButton || b == &deckBButton)
                b->setButtonText(level >= 1 ? (b == &deckAButton ? "A" : "B") : b->getProperties()["fullText"].toString());
            else
                b->setButtonText(shortened ? juce::String() : b->getProperties()["fullText"].toString());
        }
        int total = 0;
        for (auto* b : buttons) total += widthOf(*b) + gap;
        if (total <= actionsArea.getWidth()) break;
    }

    auto row = actionsArea;
    auto place = [&](juce::TextButton& b, bool fromRight) {
        const int w = widthOf(b);
        b.setBounds((fromRight ? row.removeFromRight(w) : row.removeFromLeft(w)).withSizeKeepingCentre(w, h));
        if (fromRight) row.removeFromRight(gap); else row.removeFromLeft(gap);
    };
    for (auto* b : { &cueButton, &airButton, &deckAButton, &deckBButton, &queueButton, &cartButton })
        place(*b, false);
    place(removeButton, true);
    place(renameButton, true);
}

void TemporariesView::resized() {
    auto area = getLocalBounds().withTrimmedTop(UiStyle::gap);
    const int h = UiStyle::controlHeight;

    // Arriba: el enlace, la descarga, cuántos hay y Vaciar.
    auto toolbar = area.removeFromTop(h);
    const int emptyWidth = emptyButton.getBestWidthForHeight(h) + 24;
    emptyButton.setBounds(toolbar.removeFromRight(emptyWidth));
    toolbar.removeFromRight(8);
    countArea = toolbar.removeFromRight(juce::jmin(86, toolbar.getWidth() / 5));
    toolbar.removeFromRight(12);
    const int downloadWidth = downloadButton.getBestWidthForHeight(h) + 26;
    downloadButton.setBounds(toolbar.removeFromRight(downloadWidth));
    toolbar.removeFromRight(6);
    linkEditor.setBounds(toolbar);

    // Debajo: el estado de la descarga (o el último mensaje), con su acción.
    auto status = area.removeFromTop(30).withTrimmedTop(4);
    if (linkActionButton.isVisible()) {
        const int w = linkActionButton.getBestWidthForHeight(26) + 24;
        linkActionButton.setBounds(status.removeFromRight(w).withSizeKeepingCentre(w, 26));
        status.removeFromRight(8);
    }
    linkBarArea = {};
    if (linkBar) {
        const int w = juce::jmin(140, status.getWidth() / 4);
        linkBarArea = status.removeFromRight(w).withSizeKeepingCentre(w, 6);
        status.removeFromRight(10);
    }
    linkStatusArea = status.withTrimmedLeft(2);

    // Abajo: lo que se hace con lo elegido.
    actionsArea = area.removeFromBottom(h);
    area.removeFromBottom(UiStyle::gap + 2);
    layoutActions();

    table.setBounds(area);
    const int fixed = 64 + 72 + 96 + table.getViewport()->getScrollBarThickness();
    const int flexible = juce::jmax(160, table.getWidth() - fixed);
    table.getHeader().setColumnWidth(colTitle, (int) (flexible * 0.62f));
    table.getHeader().setColumnWidth(colAuthor, flexible - (int) (flexible * 0.62f));

    // "Importar temporal" debajo del aviso de lista vacía.
    const auto empty = area.withTrimmedTop(table.getHeaderHeight());
    const int buttonWidth = juce::jmin(empty.getWidth() - 24, importEmptyButton.getBestWidthForHeight(34) + 44);
    importEmptyButton.setBounds(empty.getCentreX() - buttonWidth / 2, empty.getCentreY() + 40, buttonWidth, 34);
}
