#include "../library/MusicLibrary.h"
#include "PlaylistComponent.h"
#include "TrackDetailsDialog.h"
#include "PlaylistGeneratorDialog.h"
#include "../audio/Deck.h"
#include "../autodj/OnAir.h"
#include "../autodj/QueueTiming.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

// La tabla muestra dos filas extra: 0 = inicio de la cola, size + 1 = fin de la cola.
// Las pistas ocupan las filas 1..size, así que el índice en la cola es fila - 1.

enum Columns { colStart = 1, colTitle, colArtist, colLength, colMove };

PlaylistComponent::PlaylistComponent(Playlist& pl, MusicLibrary& lib)
    : playlist(pl), library(lib)
{
    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);
    table.setMultipleSelectionEnabled(true);

    auto& header = table.getHeader();
    header.addColumn("", colStart, 80, 70, 100, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colTitle, 300, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colArtist, 200, 60, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colLength, 70, 60, 90, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colMove, 64, 64, 64, juce::TableHeaderComponent::notResizableOrSortable);

    addAndMakeVisible(generatorButton);
    UiStyle::setIcon(generatorButton, "sparkle");
    generatorButton.onClick = [this] { openGenerator(); };

    startTimer(500);
    lookAndFeelChanged();
}

PlaylistComponent::~PlaylistComponent() {
    stopTimer();
}

void PlaylistComponent::lookAndFeelChanged() {
    auto& header = table.getHeader();
    header.setColumnName(colStart, TRANS("header_start"));
    header.setColumnName(colTitle, TRANS("header_title"));
    header.setColumnName(colArtist, TRANS("header_artist"));
    header.setColumnName(colLength, TRANS("header_length"));
    header.setColumnName(colMove, "");

    generatorButton.setButtonText(TRANS("generator_btn"));
    generatorButton.setTooltip(TRANS("tip_generator"));
    table.setColour(juce::ListBox::backgroundColourId, ThemeManager::getInstance().getPanelBackgroundColour());
    table.repaint();
    resized();
    repaint();
}

void PlaylistComponent::setDecks(Deck* d1, Deck* d2) {
    deckA = d1;
    deckB = d2;
}

//==============================================================================

PlaylistComponent::NextUp PlaylistComponent::computeNextUp() const {
    NextUp next;

    if (deckA != nullptr && deckB != nullptr) {
        // Un deck cargado y detenido mientras el otro suena es lo próximo que saldrá al aire.
        for (const auto& [playing, waiting, letter] : { std::make_tuple(deckA, deckB, "B"), std::make_tuple(deckB, deckA, "A") }) {
            if (playing->isPlaying() && waiting->isLoaded() && !waiting->isPlaying()) {
                next.track = waiting->getCurrentTrack();
                next.source = TRANS("deck") + " " + letter;
                next.startsIn = QueueTiming::secondsUntilNext(*playing);
                next.valid = true;
                return next;
            }
        }
    }

    if (!trackCache.empty()) {
        next.track = trackCache.front();
        next.startsIn = isSchedulingActive ? (baseStartTime - juce::Time::getCurrentTime()).inSeconds() : -1.0;
        next.valid = true;
    }
    return next;
}

void PlaylistComponent::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    UiStyle::drawPanel(g, getLocalBounds().toFloat().reduced(0.5f));

    // Encabezado: título y resumen de la cola
    auto header = headerArea;
    g.setColour(t.getPrimaryTextColour());
    g.setFont(UiStyle::semibold(17.0f));
    const auto title = TRANS("playlist_title");
    const int titleWidth = (int) juce::GlyphArrangement::getStringWidth(UiStyle::semibold(17.0f), title) + 14;
    g.drawText(title, header.removeFromLeft(titleWidth), juce::Justification::centredLeft, false);

    if (!trackCache.empty()) {
        const double total = cumulativeDurations.back() + trackCache.back().getAirDuration();
        juce::String summary = juce::String((int) trackCache.size()) + " " + TRANS("tracks_count")
                             + juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) + UiStyle::formatLongDuration(total);
        if (isSchedulingActive)
            summary << juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) << TRANS("ends_at") << " "
                    << (baseStartTime + juce::RelativeTime(total)).formatted("%H:%M");
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(13.5f));
        g.drawText(summary, header.withTrimmedRight(generatorButton.getWidth() + 8), juce::Justification::centredLeft, true);
    }

    // Franja "siguiente"
    auto strip = nextArea.toFloat();
    g.setColour(t.getRaisedColour());
    g.fillRoundedRectangle(strip, UiStyle::controlRadius);
    g.setColour(t.getOutlineColour());
    g.drawRoundedRectangle(strip.reduced(0.5f), UiStyle::controlRadius, 1.0f);

    auto content = strip.reduced(10.0f, 0.0f);
    const auto next = computeNextUp();
    const float chipWidth = UiStyle::drawChip(g, content.withSizeKeepingCentre(content.getWidth(), 22.0f),
                                              TRANS("next_up"), t.getHighlightColour().withAlpha(0.18f),
                                              t.getHighlightColour().interpolatedWith(t.getPrimaryTextColour(), 0.25f));
    content.removeFromLeft(chipWidth + 12.0f);

    if (next.valid) {
        auto right = content.removeFromRight(170.0f);
        if (next.startsIn >= 0.0) {
            const auto startTime = juce::Time::getCurrentTime() + juce::RelativeTime(next.startsIn);
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::mono(14.0f, true));
            g.drawText(startTime.formatted("%H:%M:%S"), right, juce::Justification::centredRight, false);
        }
        if (next.source.isNotEmpty()) {
            g.setColour(t.getMutedTextColour());
            g.setFont(UiStyle::caption(11.0f));
            g.drawText(next.source, right.withTrimmedRight(84.0f), juce::Justification::centredRight, false);
        }

        const auto artist = TrackNaming::display(next.track.artist), trackTitle = TrackNaming::display(next.track.title);
        g.setColour(t.getPrimaryTextColour());
        g.setFont(UiStyle::semibold(15.0f));
        g.drawText(artist.isNotEmpty() ? trackTitle + juce::String(juce::CharPointer_UTF8("  \xe2\x80\x94  ")) + artist : trackTitle,
                   content, juce::Justification::centredLeft, true);
    } else {
        g.setColour(t.getMutedTextColour());
        g.setFont(UiStyle::ui(14.0f));
        g.drawText(TRANS("next_up_none"), content, juce::Justification::centredLeft, true);
    }
}

void PlaylistComponent::paintOverChildren(juce::Graphics& g) {
    if (!trackCache.empty()) return;

    auto area = table.getBounds().withTrimmedTop(table.getHeaderHeight() + UiStyle::rowHeight * 2);
    UiStyle::drawEmptyState(g, area, "list", TRANS("playlist_empty_title"), TRANS("playlist_empty_hint"));
}

void PlaylistComponent::resized() {
    auto area = getLocalBounds().reduced(UiStyle::padding, 10);

    headerArea = area.removeFromTop(34);
    const int generatorWidth = generatorButton.getBestWidthForHeight(32) + 24;
    generatorButton.setBounds(headerArea.withLeft(headerArea.getRight() - generatorWidth).withSizeKeepingCentre(generatorWidth, 32));

    area.removeFromTop(8);
    nextArea = area.removeFromTop(40);
    area.removeFromTop(8);

    table.setBounds(area);

    // Título y artista se reparten el ancho libre.
    auto& header = table.getHeader();
    const int fixed = 80 + 70 + 64 + table.getViewport()->getScrollBarThickness();
    const int flexible = juce::jmax(160, table.getWidth() - fixed);
    header.setColumnWidth(colTitle, (int) (flexible * 0.56f));
    header.setColumnWidth(colArtist, flexible - (int) (flexible * 0.56f));
}

//==============================================================================

int PlaylistComponent::getNumRows() {
    return (int)trackCache.size() + 2;
}

void PlaylistComponent::paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    const int numTracks = (int)trackCache.size();

    if (rowNumber == 0 || rowNumber == numTracks + 1) {
        const auto text = rowNumber == 0 ? TRANS("start_marker") : TRANS("end_marker");
        g.setColour(t.getOutlineColour());
        g.fillRect(12, height / 2, width - 24, 1);

        g.setFont(UiStyle::caption(10.5f));
        const int textWidth = (int) juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), text) + 20;
        auto label = juce::Rectangle<int>(width, height).withSizeKeepingCentre(textWidth, height);
        g.setColour(t.getPanelBackgroundColour());
        g.fillRect(label);
        g.setColour(t.getMutedTextColour());
        g.drawText(text, label, juce::Justification::centred, false);
    } else if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber == 1) {
        // La primera pista de la cola es la que AutoDJ tomará a continuación.
        g.fillAll(t.getHighlightColour().withAlpha(0.07f));
        g.setColour(t.getHighlightColour().withAlpha(0.8f));
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2 == 0) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }

    if (insertionIndex != -1 && rowNumber == insertionIndex + 1) {
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, width, 2);
    }
}

void PlaylistComponent::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber == 0 || rowNumber > (int)trackCache.size()) return;

    auto& t = ThemeManager::getInstance();
    const int dataIndex = rowNumber - 1;
    const auto& track = trackCache[(size_t)dataIndex];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    switch (columnId) {
        case colStart: {
            g.setFont(UiStyle::mono(13.0f));
            if (!isSchedulingActive) {
                g.setColour(t.getMutedTextColour());
                g.drawText("--:--:--", area, juce::Justification::centredLeft, false);
            } else {
                juce::Time cellTime = baseStartTime;
                if (dataIndex < (int)cumulativeDurations.size())
                    cellTime += juce::RelativeTime(cumulativeDurations[(size_t)dataIndex]);
                g.setColour(t.getSecondaryTextColour());
                g.drawText(cellTime.formatted("%H:%M:%S"), area, juce::Justification::centredLeft, false);
            }
            break;
        }
        case colTitle:
            g.setColour(t.getPrimaryTextColour());
            g.setFont(rowNumber == 1 ? UiStyle::semibold(14.5f) : UiStyle::ui(14.5f));
            g.drawText(TrackNaming::display(track.title), area, juce::Justification::centredLeft, true);
            break;
        case colArtist:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::ui(14.0f));
            g.drawText(TrackNaming::display(track.artist), area, juce::Justification::centredLeft, true);
            break;
        case colLength:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::mono(13.0f));
            g.drawText(UiStyle::formatTime(track.getAirDuration()), area, juce::Justification::centredRight, false);
            break;
        default:
            break;
    }
}

//==============================================================================

std::vector<int> PlaylistComponent::getSelectedQueueIndices() const {
    std::vector<int> indices;
    auto selected = table.getSelectedRows();
    for (int i = 0; i < selected.size(); ++i) {
        const int index = selected[i] - 1;
        if (index >= 0 && index < (int)trackCache.size())
            indices.push_back(index);
    }
    return indices;
}

void PlaylistComponent::removeSelectedRows() {
    std::vector<Playlist::RowRef> rows;
    for (int index : getSelectedQueueIndices())
        rows.push_back({ index, trackCache[(size_t)index].path });
    if (rows.empty()) return;

    playlist.removeTracksIfMatching(rows);
    updateCache();
    table.deselectAllRows();
}

void PlaylistComponent::moveSelectionToTop() {
    std::vector<Playlist::RowRef> rows;
    for (int index : getSelectedQueueIndices())
        rows.push_back({ index, trackCache[(size_t)index].path });
    if (rows.empty()) return;

    if (playlist.moveTracks(rows, 0)) {
        updateCache();
        table.deselectAllRows();
        for (int i = 0; i < (int)rows.size(); ++i)
            table.selectRow(i + 1, false, i == 0);
    }
}

void PlaylistComponent::playNow(int index) {
    if (index < 0 || index >= (int)trackCache.size() || deckA == nullptr || deckB == nullptr) return;
    const TrackInfo track = trackCache[(size_t)index];

    // Al confirmar, la pista sale de la cola. Mientras la pregunta estaba abierta AutoDJ
    // pudo avanzar: si la fila ya no es esa, se busca la pista por su ruta.
    juce::Component::SafePointer<PlaylistComponent> safeThis(this);
    OnAir::requestPlayNow(*deckA, *deckB, track, [safeThis, index, track] {
        if (safeThis == nullptr) return false;
        const auto queue = safeThis->playlist.getQueue();
        int at = index < (int) queue.size() && queue[(size_t) index].path == track.path ? index : -1;
        for (int i = 0; at == -1 && i < (int) queue.size(); ++i)
            if (queue[(size_t) i].path == track.path) at = i;
        if (at == -1 || safeThis->playlist.removeTracksIfMatching({ { at, track.path } }) == 0)
            return false;  // ya no está en la cola: sonó o la quitaron

        safeThis->updateCache();
        safeThis->table.deselectAllRows();
        return true;
    });
}

bool PlaylistComponent::shiftRows(int top, int bottom, int delta) {
    std::vector<Playlist::RowRef> rows;
    for (int i = top; i <= bottom; ++i) {
        if (i < 0 || i >= (int) trackCache.size()) return false;
        rows.push_back({ i, trackCache[(size_t) i].path });
    }

    // moveTracks inserta antes de la posición dada, contada sobre la cola original.
    const bool moved = playlist.moveTracks(rows, delta < 0 ? top - 1 : bottom + 2);
    updateCache();
    return moved;
}

void PlaylistComponent::deleteKeyPressed(int) {
    removeSelectedRows();
}

void PlaylistComponent::selectedRowsChanged(int) {
    table.repaint();
}

void PlaylistComponent::openTrackDetails(const TrackInfo& track) {
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

    dialog->onSave = [this] { updateCache(); };
    dialog->onCancel = [w] { w->closeButtonPressed(); };
}

void PlaylistComponent::openGenerator() {
    auto* dialog = new PlaylistGeneratorDialog(library, playlist);
    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dialog);
    opt.dialogTitle = TRANS("generator_btn");
    opt.dialogBackgroundColour = ThemeManager::getInstance().getBaseBackgroundColour();
    opt.escapeKeyTriggersCloseButton = true;
    opt.useNativeTitleBar = true;
    opt.resizable = true;

    auto* w = opt.launchAsync();
    w->setResizeLimits(820, 640, 1600, 1200);
    w->centreWithSize(PlaylistGeneratorDialog::defaultWidth, PlaylistGeneratorDialog::defaultHeight);
    dialog->onGenerateSuccess = [this] { updateCache(); };
    dialog->onCancel = [w] { w->closeButtonPressed(); };
}

void PlaylistComponent::cellDoubleClicked(int rowNumber, int, const juce::MouseEvent&) {
    const int index = rowNumber - 1;
    if (index >= 0 && index < (int)trackCache.size())
        openTrackDetails(trackCache[(size_t)index]);
}

void PlaylistComponent::cellClicked(int rowNumber, int, const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu()) return;
    if (rowNumber < 1 || rowNumber > (int)trackCache.size()) return;

    if (!table.isRowSelected(rowNumber))
        table.selectRow(rowNumber);

    const bool single = table.getSelectedRows().size() == 1;
    juce::PopupMenu m;
    if (single) {
        m.addItem(2, TRANS("menu_play_now"));
        m.addSeparator();
    }
    m.addItem(3, TRANS("menu_move_to_top"));
    m.addItem(1, TRANS("menu_remove"), true, false);
    m.addSeparator();
    m.addItem(4, TRANS("menu_clear_queue"));

    juce::Component::SafePointer<PlaylistComponent> safeThis(this);
    m.showMenuAsync(juce::PopupMenu::Options(), [this, safeThis](int result) {
        if (result == 1) removeSelectedRows();
        else if (result == 3) moveSelectionToTop();
        else if (result == 4) {
            juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon,
                TRANS("confirm_clear_queue_title"), TRANS("confirm_clear_queue_msg"),
                TRANS("confirm_clear_queue_btn"), TRANS("cancel"), this,
                juce::ModalCallbackFunction::create([safeThis](int answer) {
                    if (answer == 1 && safeThis != nullptr) {
                        safeThis->playlist.clear();
                        safeThis->table.deselectAllRows();
                        safeThis->updateCache();
                    }
                }));
        }
        else if (result == 2) {
            auto indices = getSelectedQueueIndices();
            if (indices.size() == 1) playNow(indices.front());
        }
    });
}

juce::var PlaylistComponent::getDragSourceDescription(const juce::SparseSet<int>& selectedRows) {
    juce::StringArray items;
    for (int i = 0; i < selectedRows.size(); ++i) {
        const int index = selectedRows[i] - 1;
        if (index >= 0 && index < (int)trackCache.size())
            items.add(juce::String(index) + ":" + juce::String(trackCache[(size_t)index].path));
    }
    if (items.isEmpty()) return {};
    return "playlist_drag_" + items.joinIntoString("|");
}

//==============================================================================

void PlaylistComponent::timerCallback() {
    if (deckA && deckB) {
        const double delaySeconds = QueueTiming::secondsUntilQueueStarts(*deckA, *deckB);
        isSchedulingActive = delaySeconds >= 0.0;
        baseStartTime = juce::Time::getCurrentTime() + juce::RelativeTime(juce::jmax(0.0, delaySeconds));
    }

    if (playlist.getLastModified() != localLastModified)
        updateCache();

    repaint(headerArea);
    repaint(nextArea);
    table.repaint();
}

void PlaylistComponent::updateCache() {
    localLastModified = playlist.getLastModified();
    trackCache = playlist.getQueue();

    cumulativeDurations = QueueTiming::startOffsets(trackCache);

    table.updateContent();
    repaint();
}

//==============================================================================
// Arrastrar y soltar: pistas desde la biblioteca o filas de la propia cola.

bool PlaylistComponent::isInterestedInDragSource(const SourceDetails& dragSourceDetails) {
    const auto desc = dragSourceDetails.description.toString();
    return desc.startsWith("track_list_") || desc.startsWith("playlist_drag_");
}

void PlaylistComponent::itemDragEnter(const SourceDetails&) {
    insertionIndex = -1;
    repaint();
}

void PlaylistComponent::itemDragMove(const SourceDetails& dragSourceDetails) {
    auto pos = dragSourceDetails.localPosition - table.getPosition();

    int rowH = table.getRowHeight();
    if (rowH == 0) rowH = UiStyle::rowHeight;
    const int headerH = table.getHeader().getHeight();
    const int uiRow = table.getRowContainingPosition(pos.x, pos.y);
    const int numRows = getNumRows();

    int newIndex = (int)trackCache.size();  // fuera de la tabla o en la última fila: al final
    if (uiRow >= 0 && uiRow <= 1) {
        newIndex = 0;
    } else if (uiRow > 1 && uiRow < numRows - 1) {
        auto* viewport = table.getViewport();
        const int scrollY = (viewport != nullptr) ? viewport->getViewPositionY() : 0;
        const int rowStart = headerH + (uiRow * rowH) - scrollY;
        const int logicIdx = uiRow - 1;
        newIndex = (pos.y - rowStart < rowH / 2) ? logicIdx : logicIdx + 1;
    }

    insertionIndex = juce::jlimit(0, (int)trackCache.size(), newIndex);
    table.repaint();
}

void PlaylistComponent::itemDragExit(const SourceDetails&) {
    insertionIndex = -1;
    table.repaint();
}

void PlaylistComponent::itemDropped(const SourceDetails& dragSourceDetails) {
    int targetIndex = insertionIndex;
    insertionIndex = -1;
    if (targetIndex < 0 || targetIndex > (int)trackCache.size()) targetIndex = (int)trackCache.size();

    const auto desc = dragSourceDetails.description.toString();

    if (desc.startsWith("track_list_")) {
        std::vector<TrackInfo> tracksToAdd;
        for (const auto& path : juce::StringArray::fromTokens(desc.substring(11), "|", "")) {
             TrackInfo info = library.getTrackByPath(path);
             if (info.id != -1) tracksToAdd.push_back(info);
        }
        if (!tracksToAdd.empty())
            playlist.insertTracks(tracksToAdd, targetIndex);
    }
    else if (desc.startsWith("playlist_drag_")) {
        std::vector<Playlist::RowRef> rows;
        for (const auto& item : juce::StringArray::fromTokens(desc.substring(14), "|", ""))
            rows.push_back({ item.upToFirstOccurrenceOf(":", false, false).getIntValue(),
                             item.fromFirstOccurrenceOf(":", false, false).toStdString() });
        playlist.moveTracks(rows, targetIndex);
        table.deselectAllRows();
    }

    updateCache();
}

//==============================================================================

PlaylistComponent::RowControlComponent::RowControlComponent(PlaylistComponent& p) : owner(p) {
    for (auto* b : { &upButton, &downButton }) {
        addAndMakeVisible(*b);
        UiStyle::setVariant(*b, "ghost");
        b->getProperties().set("radius", 4.0f);
    }
    UiStyle::setIcon(upButton, "chevron-up");
    UiStyle::setIcon(downButton, "chevron-down");
    upButton.setTooltip(TRANS("tip_move_up"));
    downButton.setTooltip(TRANS("tip_move_down"));

    // Con varias filas elegidas se mueve el tramo de la primera a la última; si no, la
    // fila del botón. Las filas de la tabla son índice de la cola + 1.
    auto move = [this](int delta) {
        // Solo las filas elegidas tienen este control: al deseleccionar, la tabla lo borra en
        // ese mismo momento (con el botón que se está pulsando). Lo que hace falta se toma
        // antes, y después solo se usa la cola, que sigue existiendo.
        auto& queue = owner;
        const int row = currentRow;
        const auto selected = queue.table.getSelectedRows();
        const bool block = selected.size() > 1 && selected.contains(row);
        const int top = (block ? selected[0] : row) - 1;
        const int bottom = (block ? selected[selected.size() - 1] : row) - 1;
        if (top < 0 || (delta < 0 && top == 0) || (delta > 0 && bottom >= (int) queue.trackCache.size() - 1))
            return;

        queue.table.deselectAllRows();  // desde aquí este control ya no existe
        if (!queue.shiftRows(top, bottom, delta)) return;  // la cola cambió: no se mueve nada
        if (block) {
            for (int i = 0; i < selected.size(); ++i)
                queue.table.selectRow(selected[i] + delta, false, false);
        } else {
            queue.table.selectRow(row + delta, false, true);
        }
    };
    upButton.onClick = [move] { move(-1); };
    downButton.onClick = [move] { move(+1); };
}

void PlaylistComponent::RowControlComponent::resized() {
    auto area = getLocalBounds().reduced(2, 3);
    if (upButton.isVisible() && downButton.isVisible()) {
        upButton.setBounds(area.removeFromLeft(area.getWidth() / 2).reduced(1, 0));
        downButton.setBounds(area.reduced(1, 0));
    } else {
        upButton.setBounds(area);
        downButton.setBounds(area);
    }
}

void PlaylistComponent::RowControlComponent::configure(bool showUp, bool showDown) {
    upButton.setVisible(showUp);
    downButton.setVisible(showDown);
    resized();
}

void PlaylistComponent::RowControlComponent::setRow(int row) {
    currentRow = row;
}

juce::Component* PlaylistComponent::refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) {
    const bool isTrackRow = rowNumber >= 1 && rowNumber <= (int)trackCache.size();

    if (columnId != colMove || !isTrackRow || !isRowSelected) {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* comp = static_cast<RowControlComponent*>(existingComponentToUpdate);
    if (comp == nullptr)
        comp = new RowControlComponent(*this);
    comp->setRow(rowNumber);

    // Con varias filas seleccionadas, la primera mueve el bloque hacia arriba y la última hacia abajo.
    bool showUp = true, showDown = true;
    auto selected = table.getSelectedRows();
    if (selected.size() > 1 && selected.contains(rowNumber)) {
        showUp = rowNumber == selected[0];
        showDown = rowNumber == selected[selected.size() - 1];
    }
    comp->configure(showUp, showDown);
    return comp;
}
