#include "ImportReviewView.h"
#include "../audio/PreviewPlayer.h"
#include "../network/OnlineIdentifier.h"
#include "../helpers/PersistenceManager.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

namespace {
    enum Columns { colState = 1, colArtist, colTitle, colCategory, colLength, colNote };

    constexpr int padding = 16, detailWidth = 390;

    void styleSegment(juce::TextButton& b, int edges) {
        b.getProperties().set("toggleStyle", "soft");
        b.setConnectedEdges(edges);
    }

    juce::String relativeToLab(const juce::File& file) {
        const auto library = PersistenceManager::getLibraryFolder();
        return file.isAChildOf(library) ? file.getRelativePathFrom(library).replaceCharacter('\\', '/') : file.getFullPathName();
    }
}

//==============================================================================

ImportReviewView::ImportReviewView(MusicLibrary& lib, PreviewPlayer* previewPlayer, ImportReview::Scope scope)
    : library(lib), preview(previewPlayer), review(lib)
{
    addAndMakeVisible(newScopeButton);
    addAndMakeVisible(libraryScopeButton);
    styleSegment(newScopeButton, juce::Button::ConnectedOnRight);
    styleSegment(libraryScopeButton, juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    addAndMakeVisible(artistsScopeButton);
    styleSegment(artistsScopeButton, juce::Button::ConnectedOnLeft);
    artistsScopeButton.onClick = [this] { commitEdit(false); if (onShowArtists) onShowArtists(); };
    addChildComponent(undoImportButton);
    UiStyle::setVariant(undoImportButton, "ghost");
    UiStyle::setIcon(undoImportButton, "history");
    undoImportButton.onClick = [this] { showUndoMenu(); };
    newScopeButton.onClick = [this] { showScope(ImportReview::Scope::NewImports); };
    libraryScopeButton.onClick = [this] { showScope(ImportReview::Scope::Library); };

    addAndMakeVisible(groupCombo);
    groupCombo.onChange = [this] {
        const int index = groupCombo.getSelectedId() - 2;
        const auto& groups = review.getGroups();
        groupFilter = index >= 0 && index < (int) groups.size() ? groups[(size_t) index].key : juce::String();
        rebuildVisibleRows();
        refreshControls();
    };

    for (auto* b : { &artistTitleButton, &titleArtistButton }) addAndMakeVisible(*b);
    styleSegment(artistTitleButton, juce::Button::ConnectedOnRight);
    styleSegment(titleArtistButton, juce::Button::ConnectedOnLeft);
    auto chooseFormat = [this](TrackNaming::Orientation orientation) {
        if (const auto* group = currentGroup()) {
            commitEdit(false);
            review.setGroupOrientation(group->key, orientation);
            refreshControls();
            table.repaint();
            showDetail(detailRow);
        }
    };
    artistTitleButton.onClick = [chooseFormat] { chooseFormat(TrackNaming::Orientation::ArtistTitle); };
    titleArtistButton.onClick = [chooseFormat] { chooseFormat(TrackNaming::Orientation::TitleArtist); };

    juce::TextButton* filters[] = { &filterAll, &filterCheck, &filterDuplicates, &filterReady };
    for (int i = 0; i < 4; ++i) {
        addAndMakeVisible(*filters[i]);
        styleSegment(*filters[i], i == 0 ? juce::Button::ConnectedOnRight
                                         : i == 3 ? juce::Button::ConnectedOnLeft
                                                  : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        const auto value = (Filter) i;
        filters[i]->onClick = [this, value] {
            commitEdit(false);
            filter = value;
            rebuildVisibleRows();
            refreshControls();
        };
    }

    for (auto* b : { &swapButton, &categoryButton, &caseButton, &listenButton, &discardButton }) {
        addAndMakeVisible(*b);
        UiStyle::setVariant(*b, "ghost");
    }
    UiStyle::setIcon(listenButton, "headphones");
    swapButton.onClick = [this] { commitEdit(false); review.swap(selectedRows()); refreshControls(); table.repaint(); showDetail(detailRow); };
    categoryButton.onClick = [this] { showCategoryMenu(); };
    caseButton.onClick = [this] { showCaseMenu(); };
    listenButton.onClick = [this] { togglePreview(); };
    discardButton.onClick = [this] { discardRows(selectedRows()); };

    addChildComponent(useOnlineButton);
    UiStyle::setVariant(useOnlineButton, "success");
    UiStyle::setIcon(useOnlineButton, "sparkle");
    useOnlineButton.onClick = [this] {
        commitEdit(false);
        review.acceptOnline(strongOnlineRows());
        refreshControls();
        table.repaint();
        showDetail(detailRow);
    };

    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);
    table.setRowHeight(30);
    table.setMultipleSelectionEnabled(true);
    auto& header = table.getHeader();
    header.addColumn("", colState, 30, 30, 30, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colArtist, 200, 100, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colTitle, 200, 100, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colCategory, 120, 70, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colLength, 88, 80, 96, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colNote, 170, 80, -1, juce::TableHeaderComponent::notSortable);

    addAndMakeVisible(detailViewport);
    detailViewport.setViewedComponent(&detail, false);
    detailViewport.setScrollBarsShown(true, false);
    detail.painter = [this](juce::Graphics& g) { paintDetail(g); };
    detail.addAndMakeVisible(editor);
    detail.addAndMakeVisible(saveEditButton);
    detail.addChildComponent(issueYesButton);
    detail.addChildComponent(issueNoButton);
    editor.onCommit = [this] { commitEdit(true); };
    editor.onEdited = [this] { saveEditButton.setEnabled(true); };
    saveEditButton.onClick = [this] { commitEdit(true); };
    issueYesButton.onClick = [this] { answerIssue(true); };
    issueNoButton.onClick = [this] { answerIssue(false); };
    UiStyle::setVariant(issueNoButton, "ghost");

    for (auto* b : { &closeButton, &confirmSelectionButton, &confirmReadyButton }) addAndMakeVisible(*b);
    UiStyle::setVariant(closeButton, "ghost");
    UiStyle::setVariant(confirmReadyButton, "primary");
    closeButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };
    confirmSelectionButton.onClick = [this] { startConfirm(selectedRows()); };
    confirmReadyButton.onClick = [this] {
        // "Listas" de lo que se ve: con una carpeta o un filtro elegidos, solo de ahí.
        std::vector<int> rows;
        for (int row : visibleRows)
            if (review.getItems()[(size_t) row].state == ImportReview::State::Ready) rows.push_back(row);
        startConfirm(rows);
    };

    setWantsKeyboardFocus(true);
    lookAndFeelChanged();
    showScope(scope);
    startTimer(250);
}

ImportReviewView::~ImportReviewView() {
    stopTimer();
    if (identifier != nullptr) identifier->removeChangeListener(this);
    if (preview != nullptr) preview->stop();
    table.setModel(nullptr);
}

void ImportReviewView::setOnlineIdentifier(OnlineIdentifier* onlineIdentifier) {
    if (identifier != nullptr) identifier->removeChangeListener(this);
    identifier = onlineIdentifier;
    if (identifier != nullptr) identifier->addChangeListener(this);
    refreshControls();
}

void ImportReviewView::changeListenerCallback(juce::ChangeBroadcaster*) {
    // Llegó algo de la búsqueda en línea: se suma sin tocar lo que se está escribiendo.
    if (busy() || !review.updateOnline()) return;
    refreshControls();
    table.repaint();
    if (!editor.hasChanges()) showDetail(detailRow);
}

std::vector<int> ImportReviewView::strongOnlineRows() const {
    std::vector<int> rows;
    for (int row : review.onlineRows(true))
        if (std::find(visibleRows.begin(), visibleRows.end(), row) != visibleRows.end()) rows.push_back(row);
    return rows;
}

juce::String ImportReviewView::onlineStatusText() const {
    if (identifier == nullptr) return {};
    const auto s = identifier->getStatus();
    const int pending = s.queued + (s.working ? 1 : 0);
    return pending > 0 ? TRANS("review_online_searching").replace("%1", juce::String(pending)) : juce::String();
}

void ImportReviewView::showScope(ImportReview::Scope scope) {
    if (busy()) return;
    commitEdit(false);
    if (preview != nullptr) preview->stop();
    review.load(scope);
    groupFilter = {};
    filter = Filter::All;
    footerMessage = {};
    reload();
}

//==============================================================================
// Datos

void ImportReviewView::reload() {
    // La revisión ya se recargó: las filas de antes no valen (ni lo que hubiera en el detalle).
    detailRow = -1;
    editor.clearChanges();
    pendingCount = library.getPendingCount();
    unreviewedCount = library.getUnreviewedCount();

    categories = library.getCategories();
    refreshGroupCombo();
    rebuildVisibleRows();

    table.deselectAllRows();
    int first = -1;
    if (!visibleRows.empty()) {
        // La primera que necesita que alguien la mire.
        first = 0;
        for (int i = 0; i < (int) visibleRows.size(); ++i)
            if (review.getItems()[(size_t) visibleRows[(size_t) i]].state != ImportReview::State::Ready) { first = i; break; }
        table.selectRow(first);
    }
    showDetail(reviewRowAt(first));
    refreshControls();
}

void ImportReviewView::rebuildVisibleRows() {
    visibleRows.clear();
    const auto& items = review.getItems();
    for (int i = 0; i < (int) items.size(); ++i) {
        const auto& item = items[(size_t) i];
        if (groupFilter.isNotEmpty() && item.groupKey != groupFilter) continue;
        if (filter == Filter::Check && item.state != ImportReview::State::Check) continue;
        if (filter == Filter::Duplicates && item.state != ImportReview::State::Duplicate) continue;
        if (filter == Filter::Ready && item.state != ImportReview::State::Ready) continue;
        visibleRows.push_back(i);
    }
    table.updateContent();
    table.repaint();
}

void ImportReviewView::refreshGroupCombo() {
    groupCombo.clear(juce::dontSendNotification);
    const auto& groups = review.getGroups();
    groupCombo.addItem(TRANS("review_all_folders").replace("%1", juce::String((int) review.getItems().size())), 1);
    int selected = 1;
    for (int i = 0; i < (int) groups.size(); ++i) {
        const auto& g = groups[(size_t) i];
        auto text = g.name + "  (" + juce::String(g.items) + ")";
        if (g.needsLook()) text << "  -  " << TRANS("review_folder_needs_format");
        groupCombo.addItem(text, i + 2);
        if (g.key == groupFilter) selected = i + 2;
    }
    if (selected == 1) groupFilter = {};
    groupCombo.setSelectedId(selected, juce::dontSendNotification);
}

const ImportReview::Group* ImportReviewView::currentGroup() const {
    if (groupFilter.isNotEmpty()) return review.findGroup(groupFilter);
    const auto& groups = review.getGroups();
    return groups.size() == 1 ? &groups.front() : nullptr;
}

void ImportReviewView::refreshControls() {
    const bool idle = !busy();
    const bool pendingScope = review.getScope() == ImportReview::Scope::NewImports;
    const int pending = pendingCount, unreviewed = unreviewedCount;

    newScopeButton.setButtonText(TRANS("review_scope_new").replace("%1", juce::String(pending)));
    libraryScopeButton.setButtonText(TRANS("review_scope_library").replace("%1", juce::String(unreviewed)));
    newScopeButton.setToggleState(pendingScope, juce::dontSendNotification);
    libraryScopeButton.setToggleState(!pendingScope, juce::dontSendNotification);
    artistsScopeButton.setButtonText(TRANS("review_scope_artists").replace("%1", juce::String(artistCount)));
    artistsScopeButton.setToggleState(false, juce::dontSendNotification);
    undoImportButton.setVisible(pendingScope && pending > 0);

    const auto* group = currentGroup();
    for (auto* b : { &artistTitleButton, &titleArtistButton }) b->setVisible(group != nullptr && group->format.files > 0);
    if (group != nullptr) {
        const bool ta = group->format.orientation == TrackNaming::Orientation::TitleArtist;
        artistTitleButton.setToggleState(!ta, juce::dontSendNotification);
        titleArtistButton.setToggleState(ta, juce::dontSendNotification);
    }

    const int nCheck = review.count(ImportReview::State::Check), nDup = review.count(ImportReview::State::Duplicate),
              nReady = review.count(ImportReview::State::Ready);
    filterAll.setButtonText(TRANS("review_filter_all"));
    filterCheck.setButtonText(TRANS("review_filter_check").replace("%1", juce::String(nCheck)));
    filterDuplicates.setButtonText(TRANS("review_filter_duplicates").replace("%1", juce::String(nDup)));
    filterReady.setButtonText(TRANS("review_filter_ready").replace("%1", juce::String(nReady)));
    juce::TextButton* filters[] = { &filterAll, &filterCheck, &filterDuplicates, &filterReady };
    for (int i = 0; i < 4; ++i) filters[i]->setToggleState((int) filter == i, juce::dontSendNotification);

    int visibleReady = 0;
    for (int row : visibleRows)
        if (review.getItems()[(size_t) row].state == ImportReview::State::Ready) ++visibleReady;
    const int selected = (int) selectedRows().size();
    confirmReadyButton.setButtonText(TRANS("review_confirm_ready").replace("%1", juce::String(visibleReady)));
    confirmSelectionButton.setButtonText(TRANS("review_confirm_selection").replace("%1", juce::String(selected)));
    confirmReadyButton.setEnabled(idle && visibleReady > 0);
    confirmSelectionButton.setEnabled(idle && selected > 0);

    for (juce::Component* c : std::initializer_list<juce::Component*> {
             &newScopeButton, &libraryScopeButton, &artistsScopeButton, &undoImportButton, &groupCombo, &artistTitleButton, &titleArtistButton, &filterAll,
             &filterCheck, &filterDuplicates, &filterReady, &categoryButton, &caseButton, &closeButton, &table, &detail })
        c->setEnabled(idle);
    for (auto* b : { &swapButton, &discardButton, &listenButton }) b->setEnabled(idle && selected > 0);
    const int strong = (int) strongOnlineRows().size();
    useOnlineButton.setVisible(strong > 0);
    useOnlineButton.setEnabled(idle);
    useOnlineButton.setButtonText(TRANS("review_use_online").replace("%1", juce::String(strong)));
    useOnlineButton.setTooltip(TRANS("tip_review_use_online"));
    categoryButton.setEnabled(idle && selected > 0);
    caseButton.setEnabled(idle && selected > 0);

    resized();
    repaint();
}

std::vector<int> ImportReviewView::selectedRows() const {
    std::vector<int> rows;
    const auto selection = table.getSelectedRows();
    for (int i = 0; i < selection.size(); ++i) {
        const int row = reviewRowAt(selection[i]);
        if (row >= 0) rows.push_back(row);
    }
    return rows;
}

int ImportReviewView::reviewRowAt(int tableRow) const {
    return tableRow >= 0 && tableRow < (int) visibleRows.size() ? visibleRows[(size_t) tableRow] : -1;
}

void ImportReviewView::selectReviewRow(int reviewRow) {
    for (int i = 0; i < (int) visibleRows.size(); ++i) {
        if (visibleRows[(size_t) i] == reviewRow) {
            table.selectRow(i);
            table.scrollToEnsureRowIsOnscreen(i);
            return;
        }
    }
}

//==============================================================================
// Detalle

ImportReview::Issue ImportReviewView::mainIssue(const ImportReview::Item& item) const {
    using I = ImportReview::Issue;
    for (auto issue : { I::Duplicate, I::OnlineDiffers, I::NoArtist, I::FormatUnverified, I::Opposite, I::FeaturedGuess, I::DidYouMean })
        if (item.has(issue)) return issue;
    return I::None;
}

juce::String ImportReviewView::issueText(const ImportReview::Item& item, ImportReview::Issue issue) const {
    using I = ImportReview::Issue;
    switch (issue) {
        case I::Duplicate:        return TRANS(item.duplicateBySound ? "review_issue_duplicate_sound" : "review_issue_duplicate").replace("%1", item.duplicateText);
        case I::NoArtist:         return TRANS("review_issue_no_artist");
        case I::FormatUnverified: return TRANS("review_issue_format").replace("%1", item.groupName);
        case I::Opposite:         return TRANS("review_issue_opposite");
        case I::FeaturedGuess:    return TRANS("review_issue_featured").replace("%1", item.proposal.featured.joinIntoString(", "))
                                                                       .replace("%2", item.proposal.joinedArtist);
        case I::DidYouMean:       return TRANS("review_issue_did_you_mean").replace("%1", item.didYouMean);
        case I::OnlineDiffers: {
            auto found = item.onlineProposal.credit() + " - " + item.onlineProposal.fullTitle();
            if (item.onlineProposal.year > 0) found << " (" << item.onlineProposal.year << ")";
            return TRANS("review_issue_online").replace("%1", found).replace("%2", juce::String(juce::roundToInt(item.online.score * 100.0)));
        }
        default:                  return {};
    }
}

juce::String ImportReviewView::shortNote(const ImportReview::Item& item) const {
    using I = ImportReview::Issue;
    if (item.has(I::Duplicate)) return TRANS(item.duplicateBySound ? "review_note_duplicate_sound" : "review_note_duplicate");
    if (item.has(I::OnlineDiffers))
        return TRANS("review_note_online").replace("%1", item.onlineProposal.credit() + " - " + item.onlineProposal.fullTitle());
    if (item.has(I::NoArtist)) return TRANS("review_note_no_artist");
    if (item.has(I::FormatUnverified)) return TRANS("review_note_format");
    if (item.has(I::Opposite)) return TRANS("review_note_opposite");
    if (item.has(I::FeaturedGuess)) return TRANS("review_note_featured").replace("%1", item.proposal.featured.joinIntoString(", "));
    if (item.has(I::DidYouMean)) return TRANS("review_note_did_you_mean").replace("%1", item.didYouMean);
    if (item.onlineAgrees) return TRANS("review_note_online_agrees");
    if (item.manual) return TRANS("review_note_edited");
    return {};
}

juce::Colour ImportReviewView::stateColour(ImportReview::State state) const {
    auto& t = ThemeManager::getInstance();
    switch (state) {
        case ImportReview::State::Ready:     return t.getSuccessColour();
        case ImportReview::State::Duplicate: return t.getErrorColour();
        default:                             return t.getWarningColour();
    }
}

juce::String ImportReviewView::categoryNames(const std::vector<int>& ids) const {
    juce::StringArray names;
    for (int id : ids)
        for (const auto& c : categories)
            if (c.id == id) names.add(MusicLibrary::getDisplayName(c));
    return names.joinIntoString(", ");
}

void ImportReviewView::showDetail(int reviewRow) {
    const auto& items = review.getItems();
    detailRow = reviewRow >= 0 && reviewRow < (int) items.size() ? reviewRow : -1;

    const bool has = detailRow >= 0;
    editor.setVisible(has);
    saveEditButton.setVisible(has);
    if (has) {
        const auto& item = items[(size_t) detailRow];
        editor.setContext(&review.getContext());
        editor.setValues(item.proposal);
        saveEditButton.setEnabled(false);

        using I = ImportReview::Issue;
        const auto issue = mainIssue(item);
        juce::String yes, no;
        switch (issue) {
            case I::Duplicate:        yes = TRANS("review_btn_remove_copy"); no = TRANS("review_btn_keep_both"); break;
            case I::OnlineDiffers:    yes = TRANS("review_btn_use_online"); no = TRANS("review_btn_keep_mine"); break;
            case I::FormatUnverified: yes = TRANS("review_btn_format_ok");  no = TRANS("review_btn_swap_folder"); break;
            case I::Opposite:         yes = TRANS("review_btn_its_fine");   no = TRANS("review_btn_swap"); break;
            case I::FeaturedGuess:    yes = TRANS("review_btn_featured_yes"); no = TRANS("review_btn_featured_no"); break;
            case I::DidYouMean:       yes = TRANS("review_btn_use_known").replace("%1", item.didYouMean); no = TRANS("review_btn_other_artist"); break;
            default: break;
        }
        issueYesButton.setButtonText(yes);
        issueNoButton.setButtonText(no);
        issueYesButton.setVisible(yes.isNotEmpty());
        issueNoButton.setVisible(no.isNotEmpty());
    } else {
        issueYesButton.setVisible(false);
        issueNoButton.setVisible(false);
    }
    layoutDetail();
    detail.repaint();
}

void ImportReviewView::layoutDetail() {
    const int width = juce::jmax(200, detailViewport.getWidth() - detailViewport.getScrollBarThickness());
    auto area = juce::Rectangle<int>(0, 0, width, 2000).reduced(14);

    detailTitleArea = area.removeFromTop(22);
    area.removeFromTop(8);
    // El nombre original primero: es lo que se mira al corregir.
    sourceArea = area.removeFromTop(38);
    area.removeFromTop(8);

    issueArea = {};
    if (detailRow >= 0) {
        const auto& item = review.getItems()[(size_t) detailRow];
        const auto issue = mainIssue(item);
        const auto text = issueText(item, issue);
        if (text.isNotEmpty()) {
            juce::AttributedString s;
            s.append(text, UiStyle::ui(13.5f));
            juce::TextLayout layout;
            layout.createLayout(s, (float) (area.getWidth() - 24));
            const bool buttons = issueYesButton.isVisible() || issueNoButton.isVisible();
            const int height = (int) std::ceil(layout.getHeight()) + 20 + (buttons ? 40 : 0);
            issueArea = area.removeFromTop(height);
            area.removeFromTop(12);

            auto row = issueArea.reduced(10).removeFromBottom(30);
            if (issueYesButton.isVisible()) {
                const int w = juce::jmin(row.getWidth(), issueYesButton.getBestWidthForHeight(30) + 20);
                issueYesButton.setBounds(row.removeFromLeft(w));
                row.removeFromLeft(6);
            }
            if (issueNoButton.isVisible())
                issueNoButton.setBounds(row.removeFromLeft(juce::jmin(row.getWidth(), issueNoButton.getBestWidthForHeight(30) + 20)));
        }
    }

    editor.setBounds(area.removeFromTop(editor.getIdealHeight()));
    auto buttonRow = area.removeFromTop(32);
    saveEditButton.setBounds(buttonRow.removeFromRight(juce::jmax(140, saveEditButton.getBestWidthForHeight(32) + 24)));
    area.removeFromTop(12);
    destinationArea = area.removeFromTop(38);

    detail.setSize(width, juce::jmax(detailViewport.getHeight(), destinationArea.getBottom() + 14));
}

void ImportReviewView::DetailPanel::paint(juce::Graphics& g) {
    if (painter) painter(g);
}

void ImportReviewView::paintDetail(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    UiStyle::drawSectionTitle(g, detailTitleArea, TRANS("review_detail_title"));

    if (detailRow < 0) {
        UiStyle::drawEmptyState(g, detail.getLocalBounds().withTrimmedTop(40), "list", TRANS("review_pick_row"), {});
        return;
    }
    const auto& item = review.getItems()[(size_t) detailRow];

    // Estado de la fila, a la derecha del título.
    const auto state = UiStyle::toUpper(item.state == ImportReview::State::Ready ? TRANS("review_state_ready")
                     : item.state == ImportReview::State::Duplicate ? TRANS("review_state_duplicate")
                                                                    : TRANS("review_state_check"));
    const auto colour = stateColour(item.state);
    const float chipWidth = UiStyle::getChipWidth(state, 20.0f, true);
    UiStyle::drawChip(g, detailTitleArea.toFloat().removeFromRight(chipWidth).withSizeKeepingCentre(chipWidth, 20.0f),
                      state, colour.withAlpha(0.16f), colour, true);

    if (!issueArea.isEmpty()) {
        const auto tone = item.state == ImportReview::State::Duplicate ? t.getErrorColour() : t.getWarningColour();
        g.setColour(tone.withAlpha(0.12f));
        g.fillRoundedRectangle(issueArea.toFloat(), 8.0f);
        g.setColour(tone.withAlpha(0.5f));
        g.drawRoundedRectangle(issueArea.toFloat().reduced(0.5f), 8.0f, 1.0f);

        juce::AttributedString s;
        s.append(issueText(item, mainIssue(item)), UiStyle::ui(13.5f), t.getPrimaryTextColour());
        juce::TextLayout layout;
        layout.createLayout(s, (float) (issueArea.getWidth() - 24));
        layout.draw(g, issueArea.reduced(12, 10).toFloat());
    }

    auto caption = [&](juce::Rectangle<int> area, const juce::String& title, const juce::String& value) {
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::caption(11.5f));
        g.drawText(UiStyle::toUpper(title), area.removeFromTop(16), juce::Justification::centredLeft, false);
        g.setColour(t.getMutedTextColour());
        g.setFont(UiStyle::ui(12.5f));
        g.drawFittedText(value, area, juce::Justification::topLeft, 2, 0.8f);
    };

    caption(sourceArea, TRANS("review_source"),
            juce::String::fromUTF8(item.source.name.c_str()) + "   ·   " + item.groupName);

    const juce::File current(juce::String::fromUTF8(item.track.path.c_str()));
    const auto destination = review.destinationFor(detailRow);
    caption(destinationArea, TRANS("review_destination"),
            current.isAChildOf(PersistenceManager::getLibraryFolder()) ? relativeToLab(destination) : TRANS("review_stays_outside"));
}

void ImportReviewView::commitEdit(bool moveToNext) {
    if (detailRow < 0 || busy()) return;
    const int row = detailRow;
    if (editor.hasChanges()) {
        const auto values = editor.getValues();
        if (values.title.isEmpty()) {
            footerMessage = TRANS("review_need_title");
            footerColour = ThemeManager::getInstance().getWarningColour();
            repaint(footerStatusArea);
            return;
        }
        review.edit(row, values);
        editor.clearChanges();
        refreshControls();
        table.repaint();
        showDetail(row);
    }
    if (moveToNext) selectNextToReview(row);
}

void ImportReviewView::selectNextToReview(int after) {
    const auto& items = review.getItems();
    int position = -1;
    for (int i = 0; i < (int) visibleRows.size(); ++i)
        if (visibleRows[(size_t) i] == after) position = i;

    for (int step = 1; step <= (int) visibleRows.size(); ++step) {
        const int i = (position + step) % (int) visibleRows.size();
        if (items[(size_t) visibleRows[(size_t) i]].state != ImportReview::State::Ready) {
            table.selectRow(i);
            table.scrollToEnsureRowIsOnscreen(i);
            return;
        }
    }
    if (position + 1 < (int) visibleRows.size()) table.selectRow(position + 1);
}

void ImportReviewView::answerIssue(bool yes) {
    if (detailRow < 0 || busy()) return;
    const int row = detailRow;
    const auto item = review.getItems()[(size_t) row];
    using I = ImportReview::Issue;

    switch (mainIssue(item)) {
        case I::Duplicate:
            if (yes) {
                if (preview != nullptr) preview->stop();
                review.discard({ row });
                footerMessage = TRANS("review_removed").replace("%1", "1");
                footerColour = ThemeManager::getInstance().getSecondaryTextColour();
                reload();
                if (onLibraryChanged) onLibraryChanged();
                return;
            }
            review.keepBoth(row);
            break;
        case I::FormatUnverified: {
            const auto orientation = yes ? item.effective
                                         : (item.effective == TrackNaming::Orientation::ArtistTitle ? TrackNaming::Orientation::TitleArtist
                                                                                                    : TrackNaming::Orientation::ArtistTitle);
            review.setGroupOrientation(item.groupKey, orientation);
            break;
        }
        case I::Opposite:
            if (yes) review.edit(row, item.proposal);
            else review.swap({ row });
            break;
        case I::FeaturedGuess: review.resolveFeatured(row, yes); break;
        case I::OnlineDiffers:
            if (yes) review.acceptOnline({ row });
            else review.rejectOnline(row);
            break;
        case I::DidYouMean:
            if (yes) review.acceptDidYouMean(row);
            else review.edit(row, item.proposal);
            break;
        default: break;
    }
    refreshGroupCombo();
    refreshControls();
    table.repaint();
    showDetail(row);
}

//==============================================================================
// Acciones

void ImportReviewView::startConfirm(std::vector<int> rows) {
    if (busy()) return;
    commitEdit(false);
    if (rows.empty()) {
        footerMessage = TRANS("review_nothing_ready");
        footerColour = ThemeManager::getInstance().getWarningColour();
        repaint(footerStatusArea);
        return;
    }
    if (preview != nullptr) preview->stop();  // un archivo que suena no se puede mover

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    review.beginConfirm(rows);
    pendingConfirm = rows;
    confirmIndex = 0;
    confirmOutcome = {};
    refreshControls();
    startTimer(15);
}

void ImportReviewView::finishConfirm() {
    review.endConfirm();
    pendingConfirm.clear();
    startTimer(250);

    auto& t = ThemeManager::getInstance();
    footerMessage = TRANS("review_confirmed").replace("%1", juce::String(confirmOutcome.confirmed));
    footerColour = t.getSuccessColour();
    if (confirmOutcome.deferred > 0)
        footerMessage << "  " << TRANS("review_deferred").replace("%1", juce::String(confirmOutcome.deferred));
    if (confirmOutcome.failed > 0) {
        footerMessage << "  " << TRANS("review_failed").replace("%1", juce::String(confirmOutcome.failed));
        footerColour = t.getWarningColour();
    }
    reload();
    if (onLibraryChanged) onLibraryChanged();
}

void ImportReviewView::discardRows(const std::vector<int>& rows) {
    if (rows.empty() || busy()) return;
    commitEdit(false);
    juce::Component::SafePointer<ImportReviewView> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, TRANS("review_discard_title"),
        TRANS("review_discard_text").replace("%1", juce::String((int) rows.size())),
        TRANS("review_discard_btn"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([safe, rows](int answer) {
            if (answer != 1 || safe == nullptr) return;
            if (safe->preview != nullptr) safe->preview->stop();
            const int removed = safe->review.discard(rows);
            safe->footerMessage = TRANS("review_removed").replace("%1", juce::String(removed));
            safe->footerColour = ThemeManager::getInstance().getSecondaryTextColour();
            safe->reload();
            if (safe->onLibraryChanged) safe->onLibraryChanged();
        }));
}

void ImportReviewView::showCategoryMenu() {
    const auto rows = selectedRows();
    if (rows.empty()) return;

    juce::PopupMenu menu;
    menu.addSectionHeader(TRANS("review_category_header"));
    for (const auto& c : categories) {
        if (c.isSystem()) continue;
        bool all = true;
        for (int row : rows) {
            const auto& ids = review.getItems()[(size_t) row].categories;
            all = all && std::find(ids.begin(), ids.end(), c.id) != ids.end();
        }
        menu.addItem(c.id + 1, MusicLibrary::getDisplayName(c), true, all);
    }
    if (menu.getNumItems() <= 1) menu.addItem(-1, TRANS("review_no_categories"), false, false);

    juce::Component::SafePointer<ImportReviewView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&categoryButton), [safe, rows](int result) {
        if (safe == nullptr || result <= 0) return;
        const int categoryId = result - 1;
        bool all = true;
        for (int row : rows) {
            const auto& ids = safe->review.getItems()[(size_t) row].categories;
            all = all && std::find(ids.begin(), ids.end(), categoryId) != ids.end();
        }
        safe->review.setCategory(rows, categoryId, !all);
        safe->table.repaint();
    });
}

void ImportReviewView::showCaseMenu() {
    const auto rows = selectedRows();
    if (rows.empty()) return;
    juce::PopupMenu menu;
    menu.addItem(1, TRANS("review_case_title"));
    menu.addItem(2, TRANS("review_case_upper"));
    juce::Component::SafePointer<ImportReviewView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&caseButton), [safe, rows](int result) {
        if (safe == nullptr || result <= 0) return;
        safe->commitEdit(false);
        safe->review.applyCase(rows, result == 2 ? ImportReview::CaseStyle::Upper : ImportReview::CaseStyle::Title);
        safe->refreshControls();
        safe->table.repaint();
        safe->showDetail(safe->detailRow);
    });
}

void ImportReviewView::showRowMenu(int reviewRow) {
    juce::PopupMenu menu;
    menu.addItem(1, TRANS("review_listen"), preview != nullptr);
    menu.addItem(2, TRANS("review_swap"));
    menu.addItem(5, TRANS("review_search_online"), identifier != nullptr && identifier->canSearch());
    menu.addSeparator();
    menu.addItem(3, TRANS("review_confirm_selection").replace("%1", juce::String((int) selectedRows().size())));
    menu.addItem(4, TRANS("review_discard_btn"));
    juce::Component::SafePointer<ImportReviewView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options(), [safe, reviewRow](int result) {
        if (safe == nullptr) return;
        if (result == 1) safe->togglePreview();
        else if (result == 2) { safe->review.swap(safe->selectedRows()); safe->refreshControls(); safe->table.repaint(); safe->showDetail(reviewRow); }
        else if (result == 3) safe->startConfirm(safe->selectedRows());
        else if (result == 4) safe->discardRows(safe->selectedRows());
        else if (result == 5 && safe->identifier != nullptr) {
            std::vector<int> ids;
            for (int row : safe->selectedRows()) ids.push_back(safe->review.getItems()[(size_t) row].track.id);
            safe->identifier->enqueue(ids);
        }
    });
}

void ImportReviewView::togglePreview() {
    if (preview == nullptr) return;
    const auto rows = selectedRows();
    const int row = detailRow >= 0 ? detailRow : (rows.empty() ? -1 : rows.front());
    if (row < 0) return;
    const auto& track = review.getItems()[(size_t) row].track;
    if (preview->isPlaying() && preview->getTrackId() == track.id) preview->stop();
    else if (!preview->play(track)) {
        footerMessage = TRANS("review_listen_failed");
        footerColour = ThemeManager::getInstance().getWarningColour();
    }
    timerCallback();
    repaint(footerStatusArea);
}

void ImportReviewView::timerCallback() {
    if (busy()) {
        // Por tandas de unos 40 ms: la ventana sigue dibujándose mientras se ordenan los archivos.
        const auto start = juce::Time::getMillisecondCounter();
        while (confirmIndex < pendingConfirm.size() && juce::Time::getMillisecondCounter() - start < 40)
            review.confirmRow(pendingConfirm[confirmIndex++], confirmOutcome);
        footerMessage = TRANS("review_confirming").replace("%1", juce::String((int) confirmIndex))
                                                  .replace("%2", juce::String((int) pendingConfirm.size()));
        footerColour = ThemeManager::getInstance().getSecondaryTextColour();
        repaint(footerStatusArea);
        if (confirmIndex >= pendingConfirm.size()) finishConfirm();
        return;
    }

    // Lo que va de la búsqueda en línea, en el pie.
    if (const auto online = onlineStatusText(); online != onlineStatusShown) {
        onlineStatusShown = online;
        repaint(footerStatusArea);
    }

    const bool playing = preview != nullptr && preview->isPlaying();
    const auto text = playing ? TRANS("review_stop_listening") : TRANS("review_listen");
    if (listenButton.getButtonText() != text) {
        listenButton.setButtonText(text);
        listenButton.setToggleState(playing, juce::dontSendNotification);
        resized();
    }
}

bool ImportReviewView::keyPressed(const juce::KeyPress& key) {
    const auto mods = key.getModifiers();
    if (key.getKeyCode() == juce::KeyPress::returnKey && mods.isCommandDown()) {
        startConfirm(selectedRows());
        return true;
    }
    if (mods.isCommandDown() && (key.getKeyCode() == 'I' || key.getKeyCode() == 'i')) {
        commitEdit(false);
        review.swap(selectedRows());
        refreshControls();
        table.repaint();
        showDetail(detailRow);
        return true;
    }
    if (key == juce::KeyPress::spaceKey && table.hasKeyboardFocus(true)) {
        togglePreview();
        return true;
    }
    return false;
}

//==============================================================================
// Tabla

int ImportReviewView::getNumRows() { return (int) visibleRows.size(); }

void ImportReviewView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }
}

void ImportReviewView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    const int row = reviewRowAt(rowNumber);
    if (row < 0) return;
    auto& t = ThemeManager::getInstance();
    const auto& item = review.getItems()[(size_t) row];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    switch (columnId) {
        case colState: {
            const float d = 9.0f;
            g.setColour(stateColour(item.state));
            g.fillEllipse(juce::Rectangle<float>(d, d).withCentre({ width * 0.5f, height * 0.5f }));
            break;
        }
        case colArtist: {
            const auto text = item.proposal.credit();
            g.setColour(text.isEmpty() ? t.getMutedTextColour() : t.getPrimaryTextColour());
            g.setFont(UiStyle::ui(14.0f));
            g.drawText(text.isEmpty() ? TRANS("review_no_artist_cell") : text, area, juce::Justification::centredLeft, true);
            break;
        }
        case colTitle:
            g.setColour(t.getPrimaryTextColour());
            g.setFont(UiStyle::ui(14.0f));
            g.drawText(item.proposal.fullTitle(), area, juce::Justification::centredLeft, true);
            break;
        case colCategory:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::ui(13.0f));
            g.drawText(categoryNames(item.categories), area, juce::Justification::centredLeft, true);
            break;
        case colLength:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::mono(12.5f));
            g.drawText(UiStyle::formatTime(item.track.duration), area, juce::Justification::centredRight, false);
            break;
        case colNote: {
            const auto note = shortNote(item);
            g.setColour(item.state == ImportReview::State::Ready ? t.getMutedTextColour() : stateColour(item.state));
            g.setFont(UiStyle::ui(12.5f));
            g.drawText(note, area, juce::Justification::centredLeft, true);
            break;
        }
        default: break;
    }
}

void ImportReviewView::selectedRowsChanged(int lastRowSelected) {
    // Lo que se estaba escribiendo en otra fila no se pierde.
    if (detailRow >= 0 && editor.hasChanges() && reviewRowAt(lastRowSelected) != detailRow) {
        const int previous = detailRow;
        const auto values = editor.getValues();
        if (values.title.isNotEmpty()) review.edit(previous, values);
        editor.clearChanges();
    }
    if (lastRowSelected >= 0) showDetail(reviewRowAt(lastRowSelected));
    else if (table.getNumSelectedRows() == 0) showDetail(-1);
    refreshControls();
}

void ImportReviewView::cellClicked(int rowNumber, int, const juce::MouseEvent& e) {
    if (!e.mods.isPopupMenu() || busy()) return;
    if (!table.isRowSelected(rowNumber)) table.selectRow(rowNumber);
    showRowMenu(reviewRowAt(rowNumber));
}

void ImportReviewView::returnKeyPressed(int) {
    editor.focusArtist();
}

void ImportReviewView::deleteKeyPressed(int) {
    discardRows(selectedRows());
}

//==============================================================================
// Dibujo y medidas

void ImportReviewView::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    g.fillAll(t.getBaseBackgroundColour());

    // Cuántas hay de cada estado.
    auto chips = countsArea.toFloat();
    auto chip = [&](const juce::String& label, juce::Colour colour) {
        const auto text = UiStyle::toUpper(label);
        const float w = UiStyle::getChipWidth(text, 24.0f, true);
        UiStyle::drawChip(g, chips.removeFromRight(w).withSizeKeepingCentre(w, 24.0f), text, colour.withAlpha(0.16f), colour, true);
        chips.removeFromRight(8.0f);
    };
    chip(TRANS("review_count_duplicates").replace("%1", juce::String(review.count(ImportReview::State::Duplicate))), t.getErrorColour());
    chip(TRANS("review_count_check").replace("%1", juce::String(review.count(ImportReview::State::Check))), t.getWarningColour());
    chip(TRANS("review_count_ready").replace("%1", juce::String(review.count(ImportReview::State::Ready))), t.getSuccessColour());

    // Formato de la carpeta elegida.
    juce::String status;
    auto statusColour = t.getSecondaryTextColour();
    if (const auto* group = currentGroup()) {
        const auto& f = group->format;
        if (f.files == 0) status = TRANS("review_format_tags");
        else if (group->checkedByUser) status = TRANS("review_format_chosen");
        else if (f.verified()) {
            status = TRANS("review_format_confirmed").replace("%1", juce::String(f.confirming));
            if (f.contradicting > 0) status << ", " << TRANS("review_format_against").replace("%1", juce::String(f.contradicting));
        } else {
            status = TRANS("review_format_unknown");
            statusColour = t.getWarningColour();
        }
    } else if (!review.getGroups().empty()) {
        int needLook = 0;
        for (const auto& group : review.getGroups()) needLook += group.needsLook() ? 1 : 0;
        status = TRANS("review_folders").replace("%1", juce::String((int) review.getGroups().size()));
        if (needLook > 0) {
            status << "  ·  " << TRANS("review_folders_need_format").replace("%1", juce::String(needLook));
            statusColour = t.getWarningColour();
        }
    }
    g.setColour(statusColour);
    g.setFont(UiStyle::ui(13.0f));
    g.drawFittedText(status, formatStatusArea, juce::Justification::centredLeft, 2, 0.85f);

    // Panel del detalle.
    UiStyle::drawPanel(g, detailViewport.getBounds().toFloat().expanded(1.0f));

    if (visibleRows.empty()) {
        const auto area = tableArea.withTrimmedTop(table.getHeaderHeight());
        if (review.getItems().empty()) {
            const bool pendingScope = review.getScope() == ImportReview::Scope::NewImports;
            UiStyle::drawEmptyState(g, area, pendingScope ? "import" : "sparkle",
                                    TRANS(pendingScope ? "review_empty_new" : "review_empty_library"),
                                    TRANS(pendingScope ? "review_empty_new_hint" : "review_empty_library_hint"));
        } else {
            UiStyle::drawEmptyState(g, area, "search", TRANS("review_empty_filter"), {});
        }
    }

    g.setColour(footerMessage.isNotEmpty() ? footerColour : t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    g.drawFittedText(footerMessage.isNotEmpty() ? footerMessage : onlineStatusShown.isNotEmpty() ? onlineStatusShown : TRANS("review_footer_hint"), footerStatusArea,
                     juce::Justification::centredLeft, 2, 0.85f);
}

void ImportReviewView::resized() {
    auto area = getLocalBounds().reduced(padding);

    // Qué se revisa y cuántas hay de cada estado.
    auto header = area.removeFromTop(34);
    const int scopeW = juce::jmax(newScopeButton.getBestWidthForHeight(34), libraryScopeButton.getBestWidthForHeight(34),
                                  artistsScopeButton.getBestWidthForHeight(34)) + 28;
    newScopeButton.setBounds(header.removeFromLeft(scopeW));
    libraryScopeButton.setBounds(header.removeFromLeft(scopeW));
    artistsScopeButton.setBounds(header.removeFromLeft(scopeW));
    if (undoImportButton.isVisible()) {
        header.removeFromLeft(12);
        undoImportButton.setBounds(header.removeFromLeft(undoImportButton.getBestWidthForHeight(34) + 46).withSizeKeepingCentre(undoImportButton.getBestWidthForHeight(34) + 46, 32));
    }
    header.removeFromLeft(16);
    countsArea = header;
    area.removeFromTop(12);

    // Carpeta y su formato.
    auto groupRow = area.removeFromTop(UiStyle::controlHeight);
    groupCombo.setBounds(groupRow.removeFromLeft(juce::jmin(340, groupRow.getWidth() / 3)));
    groupRow.removeFromLeft(12);
    if (artistTitleButton.isVisible()) {
        const int w = juce::jmax(artistTitleButton.getBestWidthForHeight(UiStyle::controlHeight),
                                 titleArtistButton.getBestWidthForHeight(UiStyle::controlHeight)) + 24;
        artistTitleButton.setBounds(groupRow.removeFromLeft(w));
        titleArtistButton.setBounds(groupRow.removeFromLeft(w));
        groupRow.removeFromLeft(12);
    }
    formatStatusArea = groupRow;
    area.removeFromTop(10);

    // Filtros y acciones sobre la selección.
    auto toolbar = area.removeFromTop(UiStyle::controlHeight);
    for (auto* b : { &filterAll, &filterCheck, &filterDuplicates, &filterReady })
        b->setBounds(toolbar.removeFromLeft(b->getBestWidthForHeight(UiStyle::controlHeight) + 22));
    toolbar.removeFromLeft(12);
    for (auto* b : { &discardButton, &listenButton, &caseButton, &categoryButton, &swapButton, &useOnlineButton }) {
        if (!b->isVisible()) continue;
        const int w = b->getBestWidthForHeight(UiStyle::controlHeight) + (b == &listenButton || b == &useOnlineButton ? 46 : 26);
        if (w > toolbar.getWidth()) { b->setBounds({}); continue; }
        b->setBounds(toolbar.removeFromRight(w));
        toolbar.removeFromRight(4);
    }
    area.removeFromTop(10);

    // Pie: estado y confirmar.
    auto footer = area.removeFromBottom(38);
    area.removeFromBottom(10);
    confirmReadyButton.setBounds(footer.removeFromRight(juce::jmax(180, confirmReadyButton.getBestWidthForHeight(38) + 30)));
    footer.removeFromRight(8);
    confirmSelectionButton.setBounds(footer.removeFromRight(confirmSelectionButton.getBestWidthForHeight(38) + 26));
    footer.removeFromRight(8);
    closeButton.setBounds(footer.removeFromRight(closeButton.getBestWidthForHeight(38) + 26));
    footer.removeFromRight(12);
    footerStatusArea = footer;

    // Tabla y detalle.
    auto detailArea = area.removeFromRight(juce::jmin(detailWidth, area.getWidth() / 2));
    area.removeFromRight(12);
    tableArea = area;
    table.setBounds(area);
    detailViewport.setBounds(detailArea.reduced(1));

    const int fixed = 30 + 88 + table.getViewport()->getScrollBarThickness();
    const int flexible = juce::jmax(300, table.getWidth() - fixed);
    auto& header2 = table.getHeader();
    header2.setColumnWidth(colArtist, (int) (flexible * 0.27f));
    header2.setColumnWidth(colTitle, (int) (flexible * 0.29f));
    header2.setColumnWidth(colCategory, (int) (flexible * 0.18f));
    header2.setColumnWidth(colNote, flexible - (int) (flexible * 0.27f) - (int) (flexible * 0.29f) - (int) (flexible * 0.18f));

    layoutDetail();
}

void ImportReviewView::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();
    for (auto* b : { &newScopeButton, &libraryScopeButton, &artistsScopeButton, &artistTitleButton, &titleArtistButton, &filterAll, &filterCheck,
                     &filterDuplicates, &filterReady, &listenButton })
        b->setColour(juce::TextButton::buttonOnColourId, t.getHighlightColour());

    artistTitleButton.setButtonText(TRANS("review_format_artist_title"));
    titleArtistButton.setButtonText(TRANS("review_format_title_artist"));
    artistTitleButton.setTooltip(TRANS("tip_review_format"));
    titleArtistButton.setTooltip(TRANS("tip_review_format"));
    swapButton.setButtonText(TRANS("review_swap"));
    undoImportButton.setButtonText(TRANS("review_undo_import"));
    undoImportButton.setTooltip(TRANS("tip_review_undo_import"));
    swapButton.setTooltip(TRANS("tip_review_swap"));
    categoryButton.setButtonText(TRANS("review_category"));
    caseButton.setButtonText(TRANS("review_case"));
    caseButton.setTooltip(TRANS("tip_review_case"));
    listenButton.setButtonText(TRANS("review_listen"));
    listenButton.setTooltip(TRANS("tip_review_listen"));
    discardButton.setButtonText(TRANS("review_discard"));
    discardButton.setTooltip(TRANS("tip_review_discard"));
    discardButton.getProperties().set("textColour", t.getErrorColour().toString());
    saveEditButton.setButtonText(TRANS("review_save_edit"));
    saveEditButton.setTooltip(TRANS("tip_review_save_edit"));
    closeButton.setButtonText(TRANS("review_close"));
    confirmSelectionButton.setTooltip(TRANS("tip_review_confirm_selection"));
    confirmReadyButton.setTooltip(TRANS("tip_review_confirm_ready"));

    auto& header = table.getHeader();
    header.setColumnName(colArtist, TRANS("header_artist"));
    header.setColumnName(colTitle, TRANS("header_title"));
    header.setColumnName(colCategory, TRANS("review_col_category"));
    header.setColumnName(colLength, TRANS("header_length"));
    header.setColumnName(colNote, TRANS("review_col_note"));
    table.setColour(juce::ListBox::backgroundColourId, t.getPanelBackgroundColour());

    editor.sendLookAndFeelChange();
    refreshControls();
    showDetail(detailRow);
}


void ImportReviewView::setArtistCount(int count) {
    artistCount = count;
    refreshControls();
}

void ImportReviewView::showUndoMenu() {
    if (busy()) return;
    commitEdit(false);

    juce::PopupMenu menu;
    menu.addSectionHeader(TRANS("review_undo_header"));
    const auto batches = library.getImportBatches();
    for (const auto& b : batches) {
        if (b.pending == 0) continue;
        // De dónde vino: la carpeta o el primer archivo elegido, y cuántos más.
        const auto sources = juce::StringArray::fromLines(juce::String::fromUTF8(b.source.c_str()));
        auto where = sources.isEmpty() ? juce::String() : juce::File::isAbsolutePath(sources[0]) ? juce::File(sources[0]).getFileName() : sources[0];
        if (sources.size() > 1) where << " +" << (sources.size() - 1);
        menu.addItem(b.id, TRANS("review_undo_item").replace("%1", parseLocalTimestamp(b.createdAt).formatted("%d/%m %H:%M"))
                                                    .replace("%2", where).replace("%3", juce::String(b.pending))
                                                    .replace("%4", juce::String(b.confirmed)));
    }
    if (menu.getNumItems() <= 1) menu.addItem(-1, TRANS("review_undo_none"), false, false);

    juce::Component::SafePointer<ImportReviewView> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&undoImportButton), [safe, batches](int id) {
        if (safe == nullptr || id <= 0) return;
        const auto batch = std::find_if(batches.begin(), batches.end(), [id](const Database::BatchInfo& b) { return b.id == id; });
        if (batch == batches.end()) return;
        juce::AlertWindow::showOkCancelBox(juce::AlertWindow::WarningIcon, TRANS("review_undo_title"),
            TRANS("review_undo_text").replace("%1", juce::String(batch->pending)).replace("%2", juce::String(batch->confirmed)),
            TRANS("review_undo_btn"), TRANS("cancel"), safe.getComponent(),
            juce::ModalCallbackFunction::create([safe, id](int answer) {
                if (answer != 1 || safe == nullptr) return;
                if (safe->preview != nullptr) safe->preview->stop();
                const int removed = safe->library.undoImportBatch(id);
                safe->review.reload();
                safe->footerMessage = TRANS("review_undone").replace("%1", juce::String(removed));
                safe->footerColour = ThemeManager::getInstance().getSecondaryTextColour();
                safe->reload();
                if (safe->onLibraryChanged) safe->onLibraryChanged();
            }));
    });
}

//==============================================================================

ImportReviewWindow::Content::Content(MusicLibrary& library, PreviewPlayer* preview, ImportReview::Scope scope)
    : review(library, preview, scope), artists(library)
{
    addAndMakeVisible(review);
    addChildComponent(artists);
}

void ImportReviewWindow::Content::resized() {
    review.setBounds(getLocalBounds());
    artists.setBounds(getLocalBounds());
}

ImportReviewWindow::ImportReviewWindow(MusicLibrary& lib, PreviewPlayer* preview, ImportReview::Scope scope)
    : juce::DocumentWindow(TRANS("review_window_title"), ThemeManager::getInstance().getBaseBackgroundColour(),
                           juce::DocumentWindow::allButtons),
      library(lib)
{
    setUsingNativeTitleBar(true);
    content = new Content(library, preview, scope);
    auto& review = content->review;
    auto& artists = content->artists;
    review.onCloseRequested = [this] { closeButtonPressed(); };
    artists.onCloseRequested = [this] { closeButtonPressed(); };
    review.onShowArtists = [this] { showArtists(); };
    artists.onShowReview = [this](ImportReview::Scope s) { showReview(s); };
    review.onLibraryChanged = [this] {
        updateArtistCount();
        if (onLibraryChanged) onLibraryChanged();
    };
    artists.onLibraryChanged = [this] { if (onLibraryChanged) onLibraryChanged(); };
    updateArtistCount();

    setContentOwned(content, false);
    setResizable(true, true);
    setResizeLimits(960, 600, 8000, 6000);

    // Que quepa en la pantalla (1366×768 incluida).
    const auto screen = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay() != nullptr
                      ? juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()->userArea
                      : juce::Rectangle<int>(0, 0, 1366, 768);
    centreWithSize(juce::jmin(1220, screen.getWidth() - 40), juce::jmin(780, screen.getHeight() - 40));
    setVisible(true);
}

void ImportReviewWindow::updateArtistCount() {
    content->review.setArtistCount((int) library.getArtists().size());
}

void ImportReviewWindow::showReview(ImportReview::Scope scope) {
    content->artists.setVisible(false);
    content->review.setVisible(true);
    updateArtistCount();
    content->review.showScope(scope);
}

void ImportReviewWindow::showArtists() {
    content->artists.setScopeCounts(content->review.getPendingCount(), content->review.getUnreviewedCount());
    content->artists.reload();
    content->review.setVisible(false);
    content->artists.setVisible(true);
}

void ImportReviewWindow::closeButtonPressed() {
    if (onClosed) onClosed();
}
