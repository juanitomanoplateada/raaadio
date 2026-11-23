#include "ArtistsView.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

namespace {
    enum Columns { colName = 1, colMain, colFeatured, colComposer, colNote };
    constexpr int padding = 16, detailWidth = 390;

    void styleSegment(juce::TextButton& b, int edges) {
        b.getProperties().set("toggleStyle", "soft");
        b.setConnectedEdges(edges);
    }

    juce::String str(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

    // La clave sin un artículo al inicio, para agrupar "Los Hermanos Rosario" con "Hermanos Rosario".
    juce::String bareKey(const juce::String& key) {
        for (auto* article : { "los ", "las ", "la ", "el ", "the " })
            if (key.startsWith(article)) return key.substring((int) strlen(article));
        return key;
    }

    int uses(const Database::ArtistOverview& a) { return a.main + a.featured + a.composer; }
}

ArtistsView::ArtistsView(MusicLibrary& lib) : library(lib)
{
    for (auto* b : { &newScopeButton, &libraryScopeButton, &artistsScopeButton }) addAndMakeVisible(*b);
    styleSegment(newScopeButton, juce::Button::ConnectedOnRight);
    styleSegment(libraryScopeButton, juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    styleSegment(artistsScopeButton, juce::Button::ConnectedOnLeft);
    newScopeButton.onClick = [this] { if (onShowReview) onShowReview(ImportReview::Scope::NewImports); };
    libraryScopeButton.onClick = [this] { if (onShowReview) onShowReview(ImportReview::Scope::Library); };
    artistsScopeButton.onClick = [this] { reload(); };

    addAndMakeVisible(searchBox);
    searchBox.setIndents(34, 0);
    searchBox.setJustification(juce::Justification::centredLeft);
    searchBox.onTextChange = [this] { rebuildVisibleRows(); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); rebuildVisibleRows(); };

    juce::TextButton* filters[] = { &filterAll, &filterPerformers, &filterComposers, &filterSimilar };
    for (int i = 0; i < 4; ++i) {
        addAndMakeVisible(*filters[i]);
        styleSegment(*filters[i], i == 0 ? juce::Button::ConnectedOnRight
                                         : i == 3 ? juce::Button::ConnectedOnLeft
                                                  : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        const auto value = (Filter) i;
        filters[i]->onClick = [this, value] { filter = value; rebuildVisibleRows(); };
    }

    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);
    table.setRowHeight(30);
    table.setMultipleSelectionEnabled(true);
    auto& header = table.getHeader();
    header.addColumn("", colName, 220, 120, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colMain, 90, 70, 110, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colFeatured, 90, 70, 110, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colComposer, 100, 70, 120, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colNote, 200, 100, -1, juce::TableHeaderComponent::notSortable);

    addAndMakeVisible(nameEditor);
    nameEditor.setIndents(10, 0);
    nameEditor.setJustification(juce::Justification::centredLeft);
    nameEditor.onReturnKey = [this] { rename(); };
    nameEditor.onTextChange = [this] { renameButton.setEnabled(true); };
    for (auto* b : { &renameButton, &mergeSuggestedButton, &notSameButton, &mergeSelectedButton }) addChildComponent(*b);
    renameButton.setVisible(true);
    UiStyle::setVariant(notSameButton, "ghost");
    renameButton.onClick = [this] { rename(); };
    mergeSuggestedButton.onClick = [this] {
        if (detailIndex < 0) return;
        const auto it = similarTo.find(artists[(size_t) detailIndex].id);
        if (it != similarTo.end()) merge({ artists[(size_t) detailIndex].id }, it->second);
    };
    notSameButton.onClick = [this] { markNotSame(); };
    mergeSelectedButton.onClick = [this] {
        if (detailIndex < 0) return;
        const int into = artists[(size_t) detailIndex].id;
        std::vector<int> from;
        for (int i : selectedIndices())
            if (artists[(size_t) i].id != into) from.push_back(artists[(size_t) i].id);
        merge(from, into);
    };

    addAndMakeVisible(songList);
    songList.setModel(&songs);
    songList.setRowHeight(26);

    addAndMakeVisible(closeButton);
    UiStyle::setVariant(closeButton, "ghost");
    closeButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };

    lookAndFeelChanged();
}

ArtistsView::~ArtistsView() {
    table.setModel(nullptr);
    songList.setModel(nullptr);
}

void ArtistsView::setScopeCounts(int pending, int unreviewed) {
    pendingCount = pending;
    unreviewedCount = unreviewed;
    refreshControls();
}

juce::String ArtistsView::pairKey(const std::string& a, const std::string& b) {
    return a < b ? str(a) + "|" + str(b) : str(b) + "|" + str(a);
}

//==============================================================================
// Datos

void ArtistsView::reload() {
    const int keepId = detailIndex >= 0 && detailIndex < (int) artists.size() ? artists[(size_t) detailIndex].id : -1;

    artists = library.getArtists();
    std::sort(artists.begin(), artists.end(), [](const Database::ArtistOverview& a, const Database::ArtistOverview& b) {
        return TrackNaming::lower(str(a.name)).compareNatural(TrackNaming::lower(str(b.name))) < 0;
    });
    distinctPairs.clear();
    const auto saved = library.withDatabase([](Database& db) { return db.getSetting("artists.distinct"); });
    for (const auto& line : juce::StringArray::fromLines(str(saved)))
        if (line.isNotEmpty()) distinctPairs.insert(line);
    findSimilar();

    detailIndex = -1;
    rebuildVisibleRows();
    const int index = indexOfId(keepId);
    for (int row = 0; row < (int) visibleRows.size(); ++row)
        if (visibleRows[(size_t) row] == index) { table.selectRow(row); break; }
    if (table.getNumSelectedRows() == 0 && !visibleRows.empty()) table.selectRow(0);
    refreshControls();
}

void ArtistsView::findSimilar() {
    similarTo.clear();
    // Se comparan solo los que empiezan igual (sin artículo): no hace falta todos con todos.
    std::map<juce::juce_wchar, std::vector<size_t>> byInitial;
    for (size_t i = 0; i < artists.size(); ++i) {
        const auto bare = bareKey(str(artists[i].key)).removeCharacters(" ");
        if (bare.isNotEmpty()) byInitial[bare[0]].push_back(i);
    }
    for (const auto& [initial, group] : byInitial) {
        for (size_t x = 0; x < group.size(); ++x) {
            for (size_t y = x + 1; y < group.size(); ++y) {
                const auto& a = artists[group[x]];
                const auto& b = artists[group[y]];
                if (!TrackNaming::similarKeys(str(a.key), str(b.key)) || distinctPairs.count(pairKey(a.key, b.key)) > 0) continue;
                // Se sugiere pasar el menos usado al más usado.
                const auto& from = uses(a) < uses(b) || (uses(a) == uses(b) && a.id > b.id) ? a : b;
                const auto& into = &from == &a ? b : a;
                const auto existing = similarTo.find(from.id);
                if (existing == similarTo.end() || uses(artists[(size_t) indexOfId(existing->second)]) < uses(into))
                    similarTo[from.id] = into.id;
            }
        }
    }
}

void ArtistsView::rebuildVisibleRows() {
    const auto query = TrackNaming::key(searchBox.getText());
    visibleRows.clear();
    for (int i = 0; i < (int) artists.size(); ++i) {
        const auto& a = artists[(size_t) i];
        if (filter == Filter::Performers && a.main + a.featured == 0) continue;
        if (filter == Filter::Composers && a.composer == 0) continue;
        if (filter == Filter::Similar && similarTo.count(a.id) == 0) continue;
        if (query.isNotEmpty()) {
            bool found = str(a.key).contains(query);
            for (const auto& alias : a.aliases) found = found || str(alias).contains(query);
            if (!found) continue;
        }
        visibleRows.push_back(i);
    }
    table.updateContent();
    table.repaint();
    refreshControls();
}

int ArtistsView::indexOfId(int artistId) const {
    for (int i = 0; i < (int) artists.size(); ++i)
        if (artists[(size_t) i].id == artistId) return i;
    return -1;
}

std::vector<int> ArtistsView::selectedIndices() const {
    std::vector<int> indices;
    const auto rows = table.getSelectedRows();
    for (int i = 0; i < rows.size(); ++i)
        if (rows[i] >= 0 && rows[i] < (int) visibleRows.size()) indices.push_back(visibleRows[(size_t) rows[i]]);
    return indices;
}

//==============================================================================
// Detalle

void ArtistsView::showDetail(int artistIndex) {
    detailIndex = artistIndex >= 0 && artistIndex < (int) artists.size() ? artistIndex : -1;
    aliasButtons.clear();
    songs.tracks.clear();

    const bool has = detailIndex >= 0;
    nameEditor.setVisible(has);
    renameButton.setVisible(has);
    songList.setVisible(has);
    if (has) {
        const auto& a = artists[(size_t) detailIndex];
        nameEditor.setText(str(a.name), false);
        renameButton.setEnabled(false);
        songs.tracks = library.getTracksOfArtist(a.id);
        for (const auto& alias : a.aliases) {
            auto* b = aliasButtons.add(new juce::TextButton(str(alias) + juce::String::fromUTF8("  \xc3\x97")));
            UiStyle::setVariant(*b, "ghost");
            b->setTooltip(TRANS("artists_tip_remove_alias"));
            const auto key = str(alias);
            juce::Component::SafePointer<ArtistsView> safeView(this);
            b->onClick = [safeView, key] {
                // Después del clic: el botón se borra al recargar.
                juce::MessageManager::callAsync([safe = safeView, key] {
                    if (safe == nullptr) return;
                    safe->library.removeArtistAlias(key);
                    safe->reload();
                });
            };
            addAndMakeVisible(b);
        }
    }
    songList.updateContent();
    refreshControls();
}

void ArtistsView::refreshControls() {
    const auto similar = detailIndex >= 0 ? similarTo.find(artists[(size_t) detailIndex].id) : similarTo.end();
    const bool hasSuggestion = similar != similarTo.end();
    mergeSuggestedButton.setVisible(hasSuggestion);
    notSameButton.setVisible(hasSuggestion);
    if (hasSuggestion) {
        const int target = indexOfId(similar->second);
        mergeSuggestedButton.setButtonText(TRANS("artists_merge_into").replace("%1", target >= 0 ? str(artists[(size_t) target].name) : juce::String()));
    }

    int others = 0;
    for (int i : selectedIndices()) others += i != detailIndex ? 1 : 0;
    mergeSelectedButton.setVisible(detailIndex >= 0 && others > 0);
    mergeSelectedButton.setButtonText(TRANS("artists_merge_selected").replace("%1", juce::String(others)));

    newScopeButton.setButtonText(TRANS("review_scope_new").replace("%1", juce::String(pendingCount)));
    libraryScopeButton.setButtonText(TRANS("review_scope_library").replace("%1", juce::String(unreviewedCount)));
    artistsScopeButton.setButtonText(TRANS("review_scope_artists").replace("%1", juce::String((int) artists.size())));
    newScopeButton.setToggleState(false, juce::dontSendNotification);
    libraryScopeButton.setToggleState(false, juce::dontSendNotification);
    artistsScopeButton.setToggleState(true, juce::dontSendNotification);

    filterAll.setButtonText(TRANS("review_filter_all"));
    filterPerformers.setButtonText(TRANS("artists_filter_performers"));
    filterComposers.setButtonText(TRANS("artists_filter_composers"));
    filterSimilar.setButtonText(TRANS("artists_filter_similar").replace("%1", juce::String((int) similarTo.size())));
    juce::TextButton* filters[] = { &filterAll, &filterPerformers, &filterComposers, &filterSimilar };
    for (int i = 0; i < 4; ++i) filters[i]->setToggleState((int) filter == i, juce::dontSendNotification);

    resized();
    repaint();
}

void ArtistsView::SongModel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || row >= (int) tracks.size()) return;
    auto& t = ThemeManager::getInstance();
    if (selected) g.fillAll(t.getSelectionColour());
    const auto& track = tracks[(size_t) row];
    auto area = juce::Rectangle<int>(width, height).reduced(8, 0);
    g.setColour(t.getPrimaryTextColour());
    g.setFont(UiStyle::ui(13.5f));
    g.drawText(TrackNaming::display(track.title), area.removeFromLeft(area.getWidth() / 2), juce::Justification::centredLeft, true);
    g.setColour(t.getSecondaryTextColour());
    g.drawText(TrackNaming::display(track.artist), area.withTrimmedLeft(8), juce::Justification::centredLeft, true);
}

//==============================================================================
// Acciones

void ArtistsView::report(const MusicLibrary::ArtistResult& result, const juce::String& done) {
    auto& t = ThemeManager::getInstance();
    footerMessage = done.replace("%1", juce::String(result.tracksUpdated));
    footerColour = t.getSuccessColour();
    if (result.movesDeferred > 0) {
        footerMessage << "  " << TRANS("review_deferred").replace("%1", juce::String(result.movesDeferred));
        footerColour = t.getWarningColour();
    }
    if (onLibraryChanged) onLibraryChanged();
}

void ArtistsView::rename() {
    if (detailIndex < 0) return;
    const auto& artist = artists[(size_t) detailIndex];
    const auto newName = nameEditor.getText().trim();
    if (newName.isEmpty() || newName == str(artist.name)) return;

    const int id = artist.id;
    const auto result = library.renameArtist(id, newName);
    if (result.ok) {
        report(result, TRANS("artists_renamed"));
        reload();
        return;
    }
    if (result.conflictId == -1) {
        footerMessage = TRANS("artists_rename_failed");
        footerColour = ThemeManager::getInstance().getWarningColour();
        repaint(footerArea);
        return;
    }

    // Ya hay un artista con ese nombre: seguramente es el mismo.
    const int other = indexOfId(result.conflictId);
    const auto otherName = other >= 0 ? str(artists[(size_t) other].name) : newName;
    juce::Component::SafePointer<ArtistsView> safe(this);
    const int into = result.conflictId;
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("artists_merge_title"),
        TRANS("artists_rename_conflict").replace("%1", otherName).replace("%2", str(artist.name)),
        TRANS("artists_merge_btn"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([safe, id, into](int answer) {
            if (answer == 1 && safe != nullptr) safe->merge({ id }, into);
        }));
}

void ArtistsView::merge(const std::vector<int>& fromIds, int intoId) {
    if (fromIds.empty()) return;
    const int into = indexOfId(intoId);
    if (into < 0) return;

    juce::StringArray names;
    for (int id : fromIds)
        if (const int i = indexOfId(id); i >= 0) names.add(str(artists[(size_t) i].name));
    const auto intoName = str(artists[(size_t) into].name);

    juce::Component::SafePointer<ArtistsView> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("artists_merge_title"),
        TRANS("artists_merge_text").replace("%1", names.joinIntoString(", ")).replace("%2", intoName),
        TRANS("artists_merge_btn"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([safe, fromIds, intoId](int answer) {
            if (answer != 1 || safe == nullptr) return;
            MusicLibrary::ArtistResult total;
            total.ok = true;
            for (int id : fromIds) {
                const auto r = safe->library.mergeArtists(id, intoId);
                total.ok = total.ok && r.ok;
                total.tracksUpdated += r.tracksUpdated;
                total.movesDeferred += r.movesDeferred;
            }
            safe->report(total, TRANS("artists_merged"));
            // El detalle queda en el artista que recibió las canciones.
            safe->artists.clear();
            safe->detailIndex = -1;
            safe->reload();
            const int index = safe->indexOfId(intoId);
            for (int row = 0; row < (int) safe->visibleRows.size(); ++row)
                if (safe->visibleRows[(size_t) row] == index) { safe->table.selectRow(row); break; }
        }));
}

void ArtistsView::markNotSame() {
    if (detailIndex < 0) return;
    const auto& artist = artists[(size_t) detailIndex];
    const auto it = similarTo.find(artist.id);
    const int other = it != similarTo.end() ? indexOfId(it->second) : -1;
    if (other < 0) return;

    distinctPairs.insert(pairKey(artist.key, artists[(size_t) other].key));
    juce::StringArray lines;
    for (const auto& p : distinctPairs) lines.add(p);
    library.withDatabase([&lines](Database& db) { return db.setSetting("artists.distinct", lines.joinIntoString("\n").toStdString()); });
    findSimilar();
    rebuildVisibleRows();
    showDetail(detailIndex);
}

//==============================================================================
// Tabla

int ArtistsView::getNumRows() { return (int) visibleRows.size(); }

void ArtistsView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }
}

void ArtistsView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber < 0 || rowNumber >= (int) visibleRows.size()) return;
    auto& t = ThemeManager::getInstance();
    const auto& a = artists[(size_t) visibleRows[(size_t) rowNumber]];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);
    auto number = [&](int n) {
        g.setColour(n > 0 ? t.getSecondaryTextColour() : t.getMutedTextColour().withAlpha(0.5f));
        g.setFont(UiStyle::mono(12.5f));
        g.drawText(n > 0 ? juce::String(n) : juce::String("-"), area, juce::Justification::centredRight, false);
    };

    switch (columnId) {
        case colName:
            g.setColour(t.getPrimaryTextColour());
            g.setFont(UiStyle::ui(14.0f));
            g.drawText(str(a.name), area, juce::Justification::centredLeft, true);
            break;
        case colMain:     number(a.main); break;
        case colFeatured: number(a.featured); break;
        case colComposer: number(a.composer); break;
        case colNote: {
            const auto it = similarTo.find(a.id);
            if (it == similarTo.end()) break;
            const int other = indexOfId(it->second);
            g.setColour(t.getWarningColour());
            g.setFont(UiStyle::ui(12.5f));
            g.drawText(TRANS("artists_note_similar").replace("%1", other >= 0 ? str(artists[(size_t) other].name) : juce::String()),
                       area, juce::Justification::centredLeft, true);
            break;
        }
        default: break;
    }
}

void ArtistsView::selectedRowsChanged(int lastRowSelected) {
    if (lastRowSelected >= 0 && lastRowSelected < (int) visibleRows.size()) showDetail(visibleRows[(size_t) lastRowSelected]);
    else if (table.getNumSelectedRows() == 0) showDetail(-1);
    else refreshControls();
}

//==============================================================================
// Dibujo y medidas

void ArtistsView::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    g.fillAll(t.getBaseBackgroundColour());

    UiStyle::drawPanel(g, detailArea.toFloat());
    auto inner = detailArea.reduced(14);
    UiStyle::drawSectionTitle(g, inner.removeFromTop(22), TRANS("artists_detail_title"));

    if (detailIndex < 0) {
        UiStyle::drawEmptyState(g, detailArea.withTrimmedTop(40), "list", TRANS("artists_pick"), {});
    } else {
        const auto& a = artists[(size_t) detailIndex];
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(13.0f));
        g.drawFittedText(TRANS("artists_summary").replace("%1", juce::String(a.main)).replace("%2", juce::String(a.featured))
                                                 .replace("%3", juce::String(a.composer)),
                         summaryArea, juce::Justification::centredLeft, 2, 0.85f);

        if (!suggestionArea.isEmpty()) {
            const auto tone = t.getWarningColour();
            g.setColour(tone.withAlpha(0.12f));
            g.fillRoundedRectangle(suggestionArea.toFloat(), 8.0f);
            g.setColour(tone.withAlpha(0.5f));
            g.drawRoundedRectangle(suggestionArea.toFloat().reduced(0.5f), 8.0f, 1.0f);
            const auto it = similarTo.find(a.id);
            const int other = it != similarTo.end() ? indexOfId(it->second) : -1;
            if (other >= 0) {
                const auto& o = artists[(size_t) other];
                g.setColour(t.getPrimaryTextColour());
                g.setFont(UiStyle::ui(13.0f));
                g.drawFittedText(TRANS("artists_suggestion").replace("%1", str(o.name)).replace("%2", juce::String(uses(o)))
                                                            .replace("%3", str(a.name)),
                                 suggestionArea.reduced(12, 10).withTrimmedBottom(38), juce::Justification::topLeft, 3, 0.85f);
            }
        }

        if (!aliasCaptionArea.isEmpty())
            UiStyle::drawSectionTitle(g, aliasCaptionArea, TRANS("artists_aliases"));
        UiStyle::drawSectionTitle(g, songsCaptionArea, TRANS("artists_songs").replace("%1", juce::String((int) songs.tracks.size())));
    }

    // Cuántos hay y cuántos parecen repetidos.
    auto chips = countsArea.toFloat();
    auto chip = [&](const juce::String& label, juce::Colour colour) {
        const auto text = UiStyle::toUpper(label);
        const float w = UiStyle::getChipWidth(text, 24.0f, true);
        UiStyle::drawChip(g, chips.removeFromRight(w).withSizeKeepingCentre(w, 24.0f), text, colour.withAlpha(0.16f), colour, true);
        chips.removeFromRight(8.0f);
    };
    if (!similarTo.empty()) chip(TRANS("artists_count_similar").replace("%1", juce::String((int) similarTo.size())), t.getWarningColour());

    if (visibleRows.empty())
        UiStyle::drawEmptyState(g, tableArea.withTrimmedTop(table.getHeaderHeight()), "list",
                                TRANS(artists.empty() ? "artists_empty" : "review_empty_filter"),
                                artists.empty() ? TRANS("artists_empty_hint") : juce::String());

    g.setColour(footerMessage.isNotEmpty() ? footerColour : t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    g.drawFittedText(footerMessage.isNotEmpty() ? footerMessage : TRANS("artists_footer_hint"), footerArea,
                     juce::Justification::centredLeft, 2, 0.85f);
}

void ArtistsView::paintOverChildren(juce::Graphics& g) {
    UiStyle::drawIcon(g, "search", searchBox.getBounds().toFloat().removeFromLeft(34.0f).withSizeKeepingCentre(15.0f, 15.0f),
                      ThemeManager::getInstance().getMutedTextColour());
}

void ArtistsView::resized() {
    auto area = getLocalBounds().reduced(padding);

    auto header = area.removeFromTop(34);
    const int scopeW = juce::jmax(newScopeButton.getBestWidthForHeight(34), libraryScopeButton.getBestWidthForHeight(34),
                                  artistsScopeButton.getBestWidthForHeight(34)) + 28;
    for (auto* b : { &newScopeButton, &libraryScopeButton, &artistsScopeButton }) b->setBounds(header.removeFromLeft(scopeW));
    header.removeFromLeft(16);
    countsArea = header;
    area.removeFromTop(12);

    auto toolbar = area.removeFromTop(UiStyle::controlHeight);
    searchBox.setBounds(toolbar.removeFromLeft(juce::jmin(340, toolbar.getWidth() / 3)));
    toolbar.removeFromLeft(12);
    for (auto* b : { &filterAll, &filterPerformers, &filterComposers, &filterSimilar })
        b->setBounds(toolbar.removeFromLeft(b->getBestWidthForHeight(UiStyle::controlHeight) + 22));
    area.removeFromTop(10);

    auto footer = area.removeFromBottom(38);
    area.removeFromBottom(10);
    closeButton.setBounds(footer.removeFromRight(closeButton.getBestWidthForHeight(38) + 26));
    footer.removeFromRight(12);
    footerArea = footer;

    detailArea = area.removeFromRight(juce::jmin(detailWidth, area.getWidth() / 2));
    area.removeFromRight(12);
    tableArea = area;
    table.setBounds(area);
    const int fixed = 90 + 90 + 100 + table.getViewport()->getScrollBarThickness();
    const int flexible = juce::jmax(240, table.getWidth() - fixed);
    table.getHeader().setColumnWidth(colName, (int) (flexible * 0.55f));
    table.getHeader().setColumnWidth(colNote, flexible - (int) (flexible * 0.55f));

    // Detalle
    auto d = detailArea.reduced(14);
    d.removeFromTop(22 + 10);
    auto nameRow = d.removeFromTop(UiStyle::controlHeight);
    renameButton.setBounds(nameRow.removeFromRight(juce::jmax(110, renameButton.getBestWidthForHeight(UiStyle::controlHeight) + 24)));
    nameRow.removeFromRight(8);
    nameEditor.setBounds(nameRow);
    d.removeFromTop(6);
    summaryArea = d.removeFromTop(20);
    d.removeFromTop(10);

    suggestionArea = {};
    if (mergeSuggestedButton.isVisible()) {
        suggestionArea = d.removeFromTop(112);
        auto buttons = suggestionArea.reduced(12, 10).removeFromBottom(30);
        const int w = juce::jmin(buttons.getWidth() - 110, mergeSuggestedButton.getBestWidthForHeight(30) + 22);
        mergeSuggestedButton.setBounds(buttons.removeFromLeft(w));
        buttons.removeFromLeft(6);
        notSameButton.setBounds(buttons.removeFromLeft(juce::jmin(buttons.getWidth(), notSameButton.getBestWidthForHeight(30) + 20)));
        d.removeFromTop(10);
    }
    if (mergeSelectedButton.isVisible()) {
        mergeSelectedButton.setBounds(d.removeFromTop(32));
        d.removeFromTop(10);
    }

    aliasCaptionArea = {};
    if (!aliasButtons.isEmpty()) {
        aliasCaptionArea = d.removeFromTop(18);
        d.removeFromTop(4);
        auto row = d.removeFromTop(26);
        for (auto* b : aliasButtons) {
            const int w = b->getBestWidthForHeight(26) + 18;
            if (w > row.getWidth()) {
                d.removeFromTop(4);
                row = d.removeFromTop(26);
            }
            b->setBounds(row.removeFromLeft(juce::jmin(w, row.getWidth())));
            row.removeFromLeft(6);
        }
        d.removeFromTop(10);
    }
    songsCaptionArea = d.removeFromTop(18);
    d.removeFromTop(4);
    songList.setBounds(d);
}

void ArtistsView::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();
    for (auto* b : { &newScopeButton, &libraryScopeButton, &artistsScopeButton, &filterAll, &filterPerformers,
                     &filterComposers, &filterSimilar })
        b->setColour(juce::TextButton::buttonOnColourId, t.getHighlightColour());
    searchBox.setFont(UiStyle::ui(14.0f));
    searchBox.setTextToShowWhenEmpty(TRANS("artists_search"), t.getMutedTextColour());
    nameEditor.applyFontToAllText(UiStyle::semibold(15.5f));
    renameButton.setButtonText(TRANS("artists_rename"));
    renameButton.setTooltip(TRANS("artists_tip_rename"));
    notSameButton.setButtonText(TRANS("artists_not_same"));
    closeButton.setButtonText(TRANS("review_close"));
    mergeSelectedButton.setTooltip(TRANS("artists_tip_merge_selected"));

    auto& header = table.getHeader();
    header.setColumnName(colName, TRANS("header_artist"));
    header.setColumnName(colMain, TRANS("artists_col_main"));
    header.setColumnName(colFeatured, TRANS("artists_col_featured"));
    header.setColumnName(colComposer, TRANS("artists_col_composer"));
    header.setColumnName(colNote, TRANS("review_col_note"));
    table.setColour(juce::ListBox::backgroundColourId, t.getPanelBackgroundColour());
    songList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    refreshControls();
}
