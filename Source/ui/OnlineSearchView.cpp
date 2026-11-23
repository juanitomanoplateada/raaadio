#include "OnlineSearchView.h"
#include "../helpers/External.h"
#include "../helpers/LanguageManager.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"
#include "../library/TrackNaming.h"

namespace {
    enum Columns { colTitle = 1, colArtist, colAlbum, colLength, colOwned };
    constexpr int padding = 16, detailWidth = 440, coverSize = 112, videoRowHeight = 52;
    constexpr int resolveDelayMs = 350;  // al recorrer la lista con las flechas, solo se pide la que queda

    juce::String dot() { return juce::String::fromUTF8("  \xc2\xb7  "); }
    juce::String str(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }
}

OnlineSearchView::OnlineSearchView(MusicLibrary& lib, SongDownloader* songDownloader, HttpTransport::Transport transport)
    : library(lib), downloader(songDownloader), searcher(std::move(transport), LanguageManager::getInstance().getCurrentLanguage())
{
    addAndMakeVisible(searchBox);
    searchBox.setIndents(34, 0);
    searchBox.setJustification(juce::Justification::centredLeft);
    searchBox.onReturnKey = [this] { startSearch(); };
    searchBox.onEscapeKey = [this] { searchBox.clear(); };

    addAndMakeVisible(searchButton);
    UiStyle::setVariant(searchButton, "primary");
    UiStyle::setIcon(searchButton, "search");
    searchButton.onClick = [this] { startSearch(); };

    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);
    table.setRowHeight(30);
    auto& header = table.getHeader();
    header.addColumn("", colTitle, 220, 120, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colArtist, 160, 90, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colAlbum, 160, 80, -1, juce::TableHeaderComponent::notSortable);
    header.addColumn("", colLength, 70, 60, 80, juce::TableHeaderComponent::notResizableOrSortable);
    header.addColumn("", colOwned, 120, 100, 140, juce::TableHeaderComponent::notResizableOrSortable);

    addAndMakeVisible(videoList);
    videoList.setModel(&videoModel);
    videoList.setRowHeight(videoRowHeight);

    addAndMakeVisible(linkField);
    linkField.setReadOnly(true);
    linkField.setCaretVisible(false);
    linkField.setIndents(10, 0);
    linkField.setJustification(juce::Justification::centredLeft);

    for (auto* b : { &downloadButton, &copyButton, &youTubeButton, &musicButton, &deezerButton, &spotifyButton, &searchYouTubeButton })
        addChildComponent(*b);
    UiStyle::setVariant(downloadButton, "primary");
    UiStyle::setIcon(downloadButton, "import");
    UiStyle::setIcon(copyButton, "link");
    UiStyle::setIcon(searchYouTubeButton, "search");
    downloadButton.onClick = [this] { startDownload(); };
    copyButton.onClick = [this] { copyLink(); };
    youTubeButton.onClick = [this] {
        if (chosenVideo >= 0) External::openUrl(SongSearch::youTubeUrl(links.videos[(size_t) chosenVideo].id));
    };
    musicButton.onClick = [this] {
        if (chosenVideo >= 0) External::openUrl(SongSearch::youTubeMusicUrl(links.videos[(size_t) chosenVideo].id));
    };
    deezerButton.onClick = [this] {
        if (hasLinks) External::openUrl(links.track.link.isNotEmpty() ? links.track.link
                                                                       : "https://www.deezer.com/track/" + juce::String(links.track.id));
    };
    spotifyButton.onClick = [this] { if (hasLinks) External::openUrl(SongSearch::spotifySearchUrl(links.track)); };
    searchYouTubeButton.onClick = [this] { if (hasLinks) External::openUrl(SongSearch::youTubeSearchPageUrl(links.track)); };

    addAndMakeVisible(closeButton);
    UiStyle::setVariant(closeButton, "ghost");
    closeButton.onClick = [this] { if (onCloseRequested) onCloseRequested(); };

    addChildComponent(downloadBar);
    downloadBar.setPercentageDisplay(false);  // el porcentaje va en el texto
    addChildComponent(downloadActionButton);
    UiStyle::setVariant(downloadActionButton, "ghost");
    downloadActionButton.onClick = [this] { if (downloadAction) downloadAction(); };

    searcher.addChangeListener(this);
    if (downloader != nullptr) {
        downloader->addChangeListener(this);
        ticker.tick = [this] { refreshDownloads(); };
        ticker.startTimer(1000);
    }
    setWantsKeyboardFocus(true);
    lookAndFeelChanged();
    refreshDownloads();
}

OnlineSearchView::~OnlineSearchView() {
    stopTimer();
    ticker.stopTimer();
    searcher.removeChangeListener(this);
    if (downloader != nullptr) downloader->removeChangeListener(this);
    table.setModel(nullptr);
    videoList.setModel(nullptr);
}

//==============================================================================
// Búsqueda

void OnlineSearchView::search(const juce::String& query) {
    searchBox.setText(query.trim(), false);
    startSearch();
}

void OnlineSearchView::startSearch() {
    const auto query = searchBox.getText().trim();
    if (query.isEmpty()) {
        searchBox.grabKeyboardFocus();
        return;
    }
    searcher.search(query);
    repaint();
}

void OnlineSearchView::changeListenerCallback(juce::ChangeBroadcaster* source) {
    if (downloader != nullptr && source == downloader) {
        refreshDownloads();
        return;
    }
    const auto latest = searcher.getResults();
    if (latest.generation != results.generation) {
        results = latest;
        showResults();
    } else if (latest.working != results.working) {
        results.working = latest.working;
        repaint();
    }
    refreshLinks();
}

void OnlineSearchView::showResults() {
    // Las claves de la biblioteca, para marcar lo que ya está (se recargan si cambió).
    if (library.getCatalogVersion() != libraryKeysVersion) {
        libraryKeysVersion = library.getCatalogVersion();
        libraryKeys = library.withDatabase([](Database& db) { return db.getMusicKeys(); });
    }
    ownedIds.clear();
    for (const auto& t : results.tracks) ownedIds.push_back(SongSearch::findInLibrary(t, libraryKeys));

    detailRow = -1;
    hasLinks = false;
    chosenVideo = -1;
    table.updateContent();
    table.deselectAllRows();
    if (!results.tracks.empty()) table.selectRow(0);  // la más probable, con su enlace de una vez
    else showDetail(-1);
    repaint();
}

void OnlineSearchView::showDetail(int row) {
    detailRow = row >= 0 && row < (int) results.tracks.size() ? row : -1;
    hasLinks = false;
    chosenVideo = -1;
    links = {};
    if (detailRow >= 0) {
        links.track = results.tracks[(size_t) detailRow];
        hasLinks = true;
        // Se pide un momento después: si se recorre la lista con las flechas, no se pide cada una.
        startTimer(resolveDelayMs);
    }
    refreshLinks();
}

void OnlineSearchView::timerCallback() {
    stopTimer();
    if (detailRow >= 0 && detailRow < (int) results.tracks.size())
        searcher.resolve(results.tracks[(size_t) detailRow]);
    refreshLinks();
}

void OnlineSearchView::refreshLinks() {
    if (detailRow >= 0) {
        const auto id = results.tracks[(size_t) detailRow].id;
        SongSearcher::Links latest;
        if (searcher.getLinks(id, latest)) {
            links = latest;
            // El mejor, salvo que el operador haya elegido otro para esta canción.
            if (chosenForTrack != id || chosenVideo >= (int) links.videos.size())
                chosenVideo = links.videos.empty() ? -1 : 0;
        }
    }
    videoList.updateContent();
    if (chosenVideo >= 0) videoList.selectRow(chosenVideo, false, true);
    else videoList.deselectAllRows();
    videoList.repaint();
    refreshButtons();
}

void OnlineSearchView::chooseVideo(int index) {
    if (!hasLinks || index < 0 || index >= (int) links.videos.size() || index == chosenVideo) return;
    chosenVideo = index;
    chosenForTrack = links.track.id;
    refreshButtons();
}

juce::String OnlineSearchView::chosenLink() const {
    return hasLinks && chosenVideo >= 0 && chosenVideo < (int) links.videos.size()
         ? SongSearch::youTubeUrl(links.videos[(size_t) chosenVideo].id) : juce::String();
}

void OnlineSearchView::refreshButtons() {
    const bool video = chosenLink().isNotEmpty();
    const bool done = hasLinks && links.state != SongSearcher::Links::State::Pending;
    if (linkField.getText() != chosenLink()) {
        linkField.setText(chosenLink(), false);
        linkField.setCaretPosition(0);  // que se vea desde "https://"
    }
    linkField.setVisible(hasLinks);
    for (auto* b : { &copyButton, &youTubeButton, &musicButton }) {
        b->setVisible(hasLinks);
        b->setEnabled(video);
    }
    deezerButton.setVisible(hasLinks);
    spotifyButton.setVisible(hasLinks);
    searchYouTubeButton.setVisible(done && links.videos.empty());

    // Descargar, o lo que corresponde según cómo va la descarga de esta canción.
    downloadButton.setVisible(hasLinks && downloader != nullptr);
    if (downloadButton.isVisible()) {
        const auto job = currentJob();
        auto text = TRANS("online_download_btn");
        auto variant = juce::String("primary");
        auto icon = juce::String("import");
        bool enabled = video;
        if (job.has_value()) {
            switch (job->state) {
                case SongDownloader::State::Queued:
                case SongDownloader::State::Downloading:
                    text = TRANS("online_download_cancel");
                    variant = "";
                    icon = "close";
                    enabled = true;
                    break;
                case SongDownloader::State::Importing:
                    text = TRANS("online_download_importing");
                    variant = "";
                    enabled = false;
                    break;
                case SongDownloader::State::Done:
                    text = TRANS("online_download_review");
                    variant = "success";
                    icon = "list";
                    enabled = true;
                    break;
                case SongDownloader::State::Duplicate:
                    text = TRANS("online_download_duplicate");
                    variant = "";
                    icon = "list";
                    enabled = false;
                    break;
                case SongDownloader::State::Failed:
                    text = TRANS("online_download_retry");
                    variant = "warning";
                    icon = "refresh";
                    enabled = true;
                    break;
                case SongDownloader::State::Cancelled:
                    break;
            }
        }
        downloadButton.setButtonText(text);
        UiStyle::setVariant(downloadButton, variant);
        UiStyle::setIcon(downloadButton, icon);
        downloadButton.setEnabled(enabled);
    }
    resized();
    repaint();
}

void OnlineSearchView::copyLink() {
    const auto link = chosenLink();
    if (link.isEmpty()) return;
    External::copyText(link);
    flash(TRANS("online_search_copied").replace("%1", link), ThemeManager::getInstance().getSuccessColour());
}

void OnlineSearchView::flash(const juce::String& message, juce::Colour colour) {
    footerMessage = message;
    footerColour = colour;
    footerUntil = juce::Time::getMillisecondCounter() + 5000;
    repaint(footerArea);
    juce::Component::SafePointer<OnlineSearchView> safe(this);
    juce::Timer::callAfterDelay(5100, [safe] {
        if (safe != nullptr && juce::Time::getMillisecondCounter() >= safe->footerUntil) {
            safe->footerMessage = {};
            safe->repaint(safe->footerArea);
        }
    });
}

//==============================================================================
// Descargas

std::optional<SongDownloader::Job> OnlineSearchView::currentJob() const {
    if (downloader == nullptr || !hasLinks) return std::nullopt;
    auto job = downloader->jobForTrack(links.track.id);
    if (job.has_value() && job->state == SongDownloader::State::Cancelled) return std::nullopt;
    return job;
}

void OnlineSearchView::startDownload() {
    if (downloader == nullptr || !hasLinks) return;
    if (const auto job = currentJob(); job.has_value()) {
        switch (job->state) {
            case SongDownloader::State::Queued:
            case SongDownloader::State::Downloading: downloader->cancel(job->id); return;
            case SongDownloader::State::Done:        if (onOpenReview) onOpenReview(); return;
            case SongDownloader::State::Failed:      downloader->retry(job->id); return;
            default: return;
        }
    }
    confirmDownload(0);
}

void OnlineSearchView::confirmDownload(int step) {
    if (downloader == nullptr || !hasLinks || chosenVideo < 0 || chosenVideo >= (int) links.videos.size()) return;
    juce::Component::SafePointer<OnlineSearchView> safe(this);
    // Cada pregunta, si se acepta, sigue con la siguiente.
    auto ask = [this, safe](juce::MessageBoxIconType icon, const juce::String& title, const juce::String& text,
                            const juce::String& ok, const std::function<void()>& onOk) {
        juce::AlertWindow::showOkCancelBox(icon, title, text, ok, TRANS("cancel"), this,
            juce::ModalCallbackFunction::create([safe, onOk](int answer) { if (safe != nullptr && answer == 1) onOk(); }));
    };
    const auto video = links.videos[(size_t) chosenVideo];

    // 1. El aviso de derechos y términos de uso, una vez por emisora.
    if (step <= 0 && !downloader->isAcknowledged()) {
        ask(juce::AlertWindow::WarningIcon, TRANS("online_download_notice_title"), TRANS("online_download_notice"),
            TRANS("online_download_notice_ok"), [safe] { safe->downloader->acknowledge(); safe->confirmDownload(1); });
        return;
    }
    // 2. Las herramientas, si faltan: se instalan y la canción espera su turno.
    if (step <= 1) {
        const auto missing = downloader->getMissingTools();
        if (!missing.empty() && !downloader->getStatus().installing) {
            juce::StringArray names;
            for (const auto& a : missing) names.add(a.name);
            ask(juce::AlertWindow::QuestionIcon, TRANS("online_tools_title"),
                TRANS("online_tools_text").replace("%1", names.joinIntoString(", "))
                                          .replace("%2", juce::String(DownloadTools::totalMegabytes(missing)))
                                          .replace("%3", DownloadTools::defaultFolder().getFullPathName()),
                TRANS("online_tools_install"), [safe] { safe->downloader->installTools(); safe->confirmDownload(2); });
            return;
        }
    }
    // 3. Ya la tiene la emisora.
    const int owned = detailRow >= 0 && detailRow < (int) ownedIds.size() ? ownedIds[(size_t) detailRow] : -1;
    if (step <= 2 && owned >= 0) {
        ask(juce::AlertWindow::QuestionIcon, TRANS("online_download_owned_title"), TRANS("online_download_owned"),
            TRANS("online_download_btn"), [safe] { safe->confirmDownload(3); });
        return;
    }
    // 4. El video no es seguro que sea esa grabación.
    if (step <= 3 && video.score < SongSearch::possibleMatch) {
        ask(juce::AlertWindow::QuestionIcon, TRANS("online_download_doubtful_title"), TRANS("online_download_doubtful"),
            TRANS("online_download_btn"), [safe] { safe->confirmDownload(4); });
        return;
    }

    if (downloader->enqueue(links.track, video) == 0)
        flash(TRANS("online_download_already"), ThemeManager::getInstance().getWarningColour());
    refreshDownloads();
}

juce::String OnlineSearchView::problemText(SongDownload::Problem problem, const juce::String& detail) const {
    using P = SongDownload::Problem;
    switch (problem) {
        case P::RateLimited:   return TRANS("online_problem_rate_limited");
        case P::BotCheck:      return TRANS("online_problem_bot_check");
        case P::Unavailable:   return TRANS("online_problem_unavailable");
        case P::AgeRestricted: return TRANS("online_problem_age");
        case P::Region:        return TRANS("online_problem_region");
        case P::Network:       return TRANS("online_problem_network");
        case P::Tools:         return TRANS("online_problem_tools");
        case P::Other:
            return detail == "import" ? TRANS("online_problem_import")
                                      : TRANS("online_problem_other").replace("%1", detail.substring(0, 140));
        case P::None: break;
    }
    return {};
}

juce::String OnlineSearchView::jobChip(const SongDownloader::Job& job, juce::Colour& colour) const {
    auto& t = ThemeManager::getInstance();
    switch (job.state) {
        case SongDownloader::State::Queued:
            colour = t.getSecondaryTextColour();
            return TRANS("online_chip_queued");
        case SongDownloader::State::Downloading:
            colour = t.getHighlightColour();
            return job.progress >= 0.999 ? TRANS("online_chip_converting")
                                         : TRANS("online_chip_downloading").replace("%1", juce::String(juce::roundToInt(job.progress * 100)));
        case SongDownloader::State::Importing:
            colour = t.getHighlightColour();
            return TRANS("online_chip_importing");
        case SongDownloader::State::Done:
            colour = t.getSuccessColour();
            return TRANS("online_chip_done");
        case SongDownloader::State::Duplicate:
            colour = t.getSuccessColour();
            return TRANS("online_search_owned_chip");
        case SongDownloader::State::Failed:
            colour = t.getErrorColour();
            return TRANS("online_chip_failed");
        case SongDownloader::State::Cancelled:
            break;
    }
    return {};
}

void OnlineSearchView::refreshDownloads() {
    if (downloader == nullptr) return;
    auto& t = ThemeManager::getInstance();
    const auto s = downloader->getStatus();
    const auto jobs = downloader->getJobs();
    const auto now = juce::Time::getCurrentTime();

    const SongDownloader::Job* active = nullptr;
    const SongDownloader::Job* lastFailed = nullptr;
    int done = 0, failed = 0, duplicates = 0;
    for (const auto& j : jobs) {
        if (j.id == s.activeJob) active = &j;
        if (j.state == SongDownloader::State::Done) ++done;
        if (j.state == SongDownloader::State::Duplicate) ++duplicates;
        if (j.state == SongDownloader::State::Failed) { ++failed; lastFailed = &j; }
    }
    const auto queued = s.queued > 0 ? dot() + TRANS("online_download_queued").replace("%1", juce::String(s.queued)) : juce::String();

    juce::String text, action;
    std::function<void()> act;
    auto colour = t.getSecondaryTextColour();
    bool bar = false;
    double progress = 0.0;

    if (s.installing) {
        text = TRANS("online_tools_installing").replace("%1", s.installStep);
        if (s.installProgress >= 0.0) text << "  " << juce::roundToInt(s.installProgress * 100) << " %";
        bar = true;
        progress = s.installProgress;
        colour = t.getHighlightColour();
    } else if (s.installError.isNotEmpty()) {
        text = TRANS("online_tools_failed").replace("%1", s.installErrorStep).replace("%2", TRANS("online_tools_error_" + s.installError));
        colour = t.getErrorColour();
        action = TRANS("online_tools_retry");
        act = [this] { downloader->installTools(); };
    } else if (active != nullptr) {
        const auto title = "\"" + TrackNaming::display(active->track.title) + "\"";
        if (active->state == SongDownloader::State::Importing)
            text = TRANS("online_download_status_importing").replace("%1", title);
        else if (active->progress >= 0.999)
            text = TRANS("online_download_status_converting").replace("%1", title);
        else
            text = TRANS("online_download_status_downloading").replace("%1", title)
                                                              .replace("%2", juce::String(juce::roundToInt(active->progress * 100)));
        text << queued;
        bar = true;
        progress = active->state == SongDownloader::State::Downloading && active->progress < 0.999 ? active->progress : -1.0;
        colour = t.getPrimaryTextColour();
        if (active->state == SongDownloader::State::Downloading) {
            action = TRANS("online_download_cancel_short");
            act = [this, id = active->id] { downloader->cancel(id); };
        }
    } else if (s.pausedUntil > now) {
        text = TRANS(s.pauseReason == SongDownload::Problem::BotCheck ? "online_download_paused_bot" : "online_download_paused")
                   .replace("%1", s.pausedUntil.formatted("%H:%M")) + queued;
        colour = t.getWarningColour();
    } else if (s.queued > 0) {
        const int seconds = s.nextStart > now ? (int) std::ceil((s.nextStart - now).inSeconds()) : 0;
        text = seconds > 0 ? TRANS("online_download_next").replace("%1", UiStyle::formatTime(seconds)) + queued
                           : TRANS("online_download_starting") + queued;
    } else if (done + failed + duplicates > 0) {
        juce::StringArray parts;
        if (done > 0) parts.add(TRANS("online_download_done_count").replace("%1", juce::String(done)));
        if (duplicates > 0) parts.add(TRANS("online_download_duplicate_count").replace("%1", juce::String(duplicates)));
        if (failed > 0) parts.add(TRANS("online_download_failed_count").replace("%1", juce::String(failed)));
        text = parts.joinIntoString(dot());
        if (lastFailed != nullptr) text << ". " << problemText(lastFailed->problem, lastFailed->detail);
        colour = failed > 0 ? t.getWarningColour() : t.getSuccessColour();
        if (done > 0) {
            action = TRANS("online_download_review_short");
            act = [this] { if (onOpenReview) onOpenReview(); };
        }
    }

    const bool layoutChanged = bar != downloadBar.isVisible() || action.isNotEmpty() != downloadActionButton.isVisible()
                            || action != downloadActionButton.getButtonText();
    downloadText = text;
    downloadColour = colour;
    downloadProgress = progress;
    downloadBar.setVisible(bar);
    downloadActionButton.setVisible(action.isNotEmpty());
    downloadActionButton.setButtonText(action);
    downloadAction = act;
    table.repaint();
    if (layoutChanged) resized();
    refreshButtons();
}

bool OnlineSearchView::keyPressed(const juce::KeyPress& key) {
    if (key == juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0)) {
        copyLink();
        return true;
    }
    return false;
}

//==============================================================================
// Textos

juce::String OnlineSearchView::errorText(const juce::String& code) const {
    if (code == "offline") return TRANS("online_search_offline");
    if (code == "youtube_wait") return TRANS("online_search_youtube_wait");
    return code;
}

juce::String OnlineSearchView::statusText() const {
    if (results.working) return TRANS("online_search_status_working");
    if (results.generation == 0 || results.error.isNotEmpty()) return {};
    const int n = (int) results.tracks.size();
    return n == 1 ? TRANS("online_search_status_one") : TRANS("online_search_status_count").replace("%1", juce::String(n));
}

juce::String OnlineSearchView::matchLabel(double score) const {
    return TRANS(score >= SongSearch::goodMatch ? "online_search_match_good"
               : score >= SongSearch::possibleMatch ? "online_search_match_possible" : "online_search_match_doubtful");
}

juce::Colour OnlineSearchView::matchColour(double score) const {
    auto& t = ThemeManager::getInstance();
    return score >= SongSearch::goodMatch ? t.getSuccessColour()
         : score >= SongSearch::possibleMatch ? t.getWarningColour() : t.getMutedTextColour();
}

juce::String OnlineSearchView::kindLabel(const SongSearch::Video& video) const {
    switch (video.kind) {
        case SongSearch::Video::Kind::OfficialAudio: return TRANS("online_search_kind_audio");
        case SongSearch::Video::Kind::OfficialVideo: return TRANS("online_search_kind_video");
        case SongSearch::Video::Kind::UserUpload:    return TRANS("online_search_kind_user");
        default: return video.fromMusic ? juce::String() : video.channel;
    }
}

//==============================================================================
// Tabla de resultados

int OnlineSearchView::getNumRows() { return (int) results.tracks.size(); }

void OnlineSearchView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int height, bool rowIsSelected) {
    auto& t = ThemeManager::getInstance();
    if (rowIsSelected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    } else if (rowNumber % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }
}

void OnlineSearchView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool) {
    if (rowNumber < 0 || rowNumber >= (int) results.tracks.size()) return;
    auto& t = ThemeManager::getInstance();
    const auto& track = results.tracks[(size_t) rowNumber];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    switch (columnId) {
        case colTitle:
            g.setColour(t.getPrimaryTextColour());
            g.setFont(UiStyle::ui(14.0f));
            g.drawText(TrackNaming::display(track.title), area, juce::Justification::centredLeft, true);
            break;
        case colArtist:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::ui(13.5f));
            g.drawText(TrackNaming::display(track.artist), area, juce::Justification::centredLeft, true);
            break;
        case colAlbum:
            g.setColour(t.getMutedTextColour());
            g.setFont(UiStyle::ui(13.0f));
            g.drawText(track.album, area, juce::Justification::centredLeft, true);
            break;
        case colLength:
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::mono(12.5f));
            g.drawText(track.duration > 0 ? UiStyle::formatTime(track.duration) : juce::String("-"), area,
                       juce::Justification::centredRight, false);
            break;
        case colOwned: {
            // Cómo va su descarga; si no se está bajando, si ya está en la biblioteca.
            juce::String text;
            auto colour = t.getSuccessColour();
            if (downloader != nullptr)
                if (const auto job = downloader->jobForTrack(track.id); job.has_value())
                    text = jobChip(*job, colour);
            if (text.isEmpty() && rowNumber < (int) ownedIds.size() && ownedIds[(size_t) rowNumber] >= 0) {
                text = TRANS("online_search_owned_chip");
                colour = t.getSuccessColour();
            }
            if (text.isNotEmpty()) {
                text = UiStyle::toUpper(text);
                const float w = juce::jmin((float) area.getWidth(), UiStyle::getChipWidth(text, 22.0f));
                UiStyle::drawChip(g, area.toFloat().removeFromLeft(w).withSizeKeepingCentre(w, 22.0f), text,
                                  colour.withAlpha(0.16f), colour);
            }
            break;
        }
        default: break;
    }
}

void OnlineSearchView::selectedRowsChanged(int lastRowSelected) {
    showDetail(lastRowSelected);
}

void OnlineSearchView::returnKeyPressed(int) {
    copyLink();
}

//==============================================================================
// Lista de videos

int OnlineSearchView::VideoModel::getNumRows() {
    return owner.hasLinks ? (int) owner.links.videos.size() : 0;
}

void OnlineSearchView::VideoModel::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) {
    if (row < 0 || row >= getNumRows()) return;
    auto& t = ThemeManager::getInstance();
    const auto& v = owner.links.videos[(size_t) row];

    if (selected) {
        g.fillAll(t.getSelectionColour());
        g.setColour(t.getHighlightColour());
        g.fillRect(0, 0, 3, height);
    }
    g.setColour(t.getOutlineColour());
    g.fillRect(0, height - 1, width, 1);

    auto area = juce::Rectangle<int>(width, height).reduced(10, 6);

    // A la derecha: qué tanto coincide y de dónde viene.
    auto right = area.removeFromRight(100);
    area.removeFromRight(8);
    const auto label = UiStyle::toUpper(owner.matchLabel(v.score));
    const auto colour = owner.matchColour(v.score);
    const float w = juce::jmin((float) right.getWidth(), UiStyle::getChipWidth(label, 20.0f, true));
    UiStyle::drawChip(g, right.removeFromTop(20).toFloat().removeFromRight(w), label, colour.withAlpha(0.16f), colour, true);
    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::caption(10.5f));
    g.drawText(UiStyle::toUpper(TRANS(v.fromMusic ? "online_search_source_music" : "online_search_source_web")),
               right.withTrimmedTop(4), juce::Justification::centredRight, true);

    g.setColour(t.getPrimaryTextColour());
    g.setFont(selected ? UiStyle::semibold(13.5f) : UiStyle::ui(13.5f));
    g.drawText(v.title, area.removeFromTop(20), juce::Justification::centredLeft, true);

    juce::StringArray parts;
    if (v.artists.isNotEmpty()) parts.add(v.artists);
    if (v.duration > 0) parts.add(UiStyle::formatTime(v.duration));
    if (const auto kind = owner.kindLabel(v); kind.isNotEmpty() && kind != v.artists) parts.add(kind);
    g.setColour(t.getSecondaryTextColour());
    g.setFont(UiStyle::ui(12.5f));
    g.drawText(parts.joinIntoString(dot()), area.withTrimmedTop(2), juce::Justification::centredLeft, true);
}

void OnlineSearchView::VideoModel::selectedRowsChanged(int lastRowSelected) {
    owner.chooseVideo(lastRowSelected);
}

void OnlineSearchView::VideoModel::listBoxItemDoubleClicked(int row, const juce::MouseEvent&) {
    if (owner.hasLinks && row >= 0 && row < (int) owner.links.videos.size())
        External::openUrl(SongSearch::youTubeUrl(owner.links.videos[(size_t) row].id));
}

//==============================================================================
// Dibujo y medidas

void OnlineSearchView::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    g.fillAll(t.getBaseBackgroundColour());

    // Estado de la búsqueda, a la derecha del campo.
    g.setColour(results.working ? t.getHighlightColour() : t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    g.drawText(statusText(), statusArea, juce::Justification::centredRight, true);

    // Detalle de la canción elegida.
    UiStyle::drawPanel(g, detailArea.toFloat());
    if (!hasLinks) {
        UiStyle::drawEmptyState(g, detailArea, "music", TRANS("online_search_pick"), {});
    } else {
        const auto& track = links.track;

        // Carátula (o un recuadro con una nota mientras llega).
        const auto cover = coverArea.toFloat();
        if (links.cover.isValid()) {
            juce::Graphics::ScopedSaveState state(g);
            juce::Path clip;
            clip.addRoundedRectangle(cover, 8.0f);
            g.reduceClipRegion(clip);
            g.drawImage(links.cover, cover, juce::RectanglePlacement::fillDestination);
        } else {
            g.setColour(t.getRaisedColour());
            g.fillRoundedRectangle(cover, 8.0f);
            UiStyle::drawIcon(g, "music", cover.withSizeKeepingCentre(40.0f, 40.0f), t.getMutedTextColour());
        }

        // El título ocupa dos líneas solo si no cabe en una.
        auto info = infoArea;
        const auto titleFont = UiStyle::semibold(17.0f);
        const auto title = TrackNaming::display(track.title);
        const bool oneLine = juce::GlyphArrangement::getStringWidth(titleFont, title) <= (float) info.getWidth();
        g.setColour(t.getPrimaryTextColour());
        g.setFont(titleFont);
        g.drawFittedText(title, info.removeFromTop(oneLine ? 26 : 44), juce::Justification::topLeft, 2, 0.9f);
        g.setColour(t.getSecondaryTextColour());
        g.setFont(UiStyle::ui(14.0f));
        g.drawText(TrackNaming::display(TrackNaming::creditLine(track.artist, track.featured)), info.removeFromTop(20),
                   juce::Justification::centredLeft, true);
        g.setColour(t.getMutedTextColour());
        g.setFont(UiStyle::ui(13.0f));
        g.drawText(track.album + (track.year > 0 ? dot() + juce::String(track.year) : juce::String()), info.removeFromTop(20),
                   juce::Justification::centredLeft, true);
        juce::String numbers = track.duration > 0 ? UiStyle::formatTime(track.duration) : juce::String();
        if (track.isrc.isNotEmpty()) numbers << (numbers.isNotEmpty() ? dot() : juce::String()) << TRANS("online_search_isrc").replace("%1", track.isrc);
        g.setFont(UiStyle::mono(12.0f));
        g.drawText(numbers, info.removeFromTop(20), juce::Justification::centredLeft, true);

        // ¿Ya la tiene la emisora?
        const int owned = detailRow >= 0 && detailRow < (int) ownedIds.size() ? ownedIds[(size_t) detailRow] : -1;
        auto ownedRow = ownedArea.toFloat();
        if (owned >= 0) {
            const auto text = UiStyle::toUpper(TRANS("online_search_owned"));
            const float w = UiStyle::getChipWidth(text, 22.0f, true);
            UiStyle::drawChip(g, ownedRow.removeFromLeft(w).withSizeKeepingCentre(w, 22.0f), text,
                              t.getSuccessColour().withAlpha(0.16f), t.getSuccessColour(), true);
            ownedRow.removeFromLeft(8.0f);
            juce::String name;
            for (const auto& row : libraryKeys)
                if (row.id == owned) name = TrackNaming::display(str(row.artist)) + juce::String::fromUTF8(" \xe2\x80\x93 ") + TrackNaming::display(str(row.title));
            g.setColour(t.getSecondaryTextColour());
            g.setFont(UiStyle::ui(13.0f));
            g.drawText(name, ownedRow.toNearestInt(), juce::Justification::centredLeft, true);
        } else {
            g.setColour(t.getMutedTextColour());
            g.setFont(UiStyle::ui(13.0f));
            g.drawText(TRANS("online_search_not_owned"), ownedRow.toNearestInt(), juce::Justification::centredLeft, true);
        }

        UiStyle::drawSectionTitle(g, videosCaptionArea, TRANS("online_search_links"));
    }

    // Pie: lo último que pasó o de dónde salen los datos.
    // Si hay descargas, cómo va la fila.
    const bool showFlash = footerMessage.isNotEmpty() && juce::Time::getMillisecondCounter() < footerUntil;
    g.setColour(showFlash ? footerColour : downloadText.isNotEmpty() ? downloadColour : t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    g.drawFittedText(showFlash ? footerMessage : downloadText.isNotEmpty() ? downloadText : TRANS("online_search_footer"),
                     footerArea, juce::Justification::centredLeft, 2, 0.85f);
}

void OnlineSearchView::paintOverChildren(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    UiStyle::drawIcon(g, "search", searchBox.getBounds().toFloat().removeFromLeft(34.0f).withSizeKeepingCentre(15.0f, 15.0f),
                      t.getMutedTextColour());

    // La tabla vacía dice por qué.
    if (results.tracks.empty()) {
        const auto area = tableArea.withTrimmedTop(table.getHeaderHeight());
        if (results.working)
            UiStyle::drawEmptyState(g, area, "search", TRANS("online_search_status_working"), "\"" + searchBox.getText().trim() + "\"");
        else if (results.error.isNotEmpty())
            UiStyle::drawEmptyState(g, area, "globe", TRANS("online_search_error").replace("%1", errorText(results.error)), {});
        else if (results.generation > 0)
            UiStyle::drawEmptyState(g, area, "search", TRANS("online_search_no_results"), TRANS("online_search_no_results_hint"));
        else
            UiStyle::drawEmptyState(g, area, "globe", TRANS("online_search_empty_title"), TRANS("online_search_empty_hint"));
    }

    // La lista de videos vacía: buscando, error o nada.
    if (hasLinks && links.videos.empty()) {
        const bool pending = links.state == SongSearcher::Links::State::Pending;
        const auto title = pending ? TRANS("online_search_links_working")
                         : links.error.isNotEmpty() ? errorText(links.error) : TRANS("online_search_links_none");
        UiStyle::drawEmptyState(g, videoMessageArea, pending ? "search" : "globe", title,
                                pending ? juce::String() : TRANS("online_search_links_none_hint"));
    }
}

void OnlineSearchView::resized() {
    auto area = getLocalBounds().reduced(padding);

    auto top = area.removeFromTop(UiStyle::controlHeight + 2);
    searchBox.setBounds(top.removeFromLeft(juce::jmin(560, top.getWidth() - 260)));
    top.removeFromLeft(10);
    searchButton.setBounds(top.removeFromLeft(searchButton.getBestWidthForHeight(top.getHeight()) + 30));
    top.removeFromLeft(12);
    statusArea = top;
    area.removeFromTop(12);

    auto footer = area.removeFromBottom(38);
    area.removeFromBottom(10);
    closeButton.setBounds(footer.removeFromRight(closeButton.getBestWidthForHeight(38) + 26));
    footer.removeFromRight(12);
    if (downloadActionButton.isVisible()) {
        downloadActionButton.setBounds(footer.removeFromRight(downloadActionButton.getBestWidthForHeight(34) + 30).withSizeKeepingCentre(
            downloadActionButton.getBestWidthForHeight(34) + 30, 34));
        footer.removeFromRight(10);
    }
    if (downloadBar.isVisible()) {
        downloadBar.setBounds(footer.removeFromRight(juce::jmin(180, footer.getWidth() / 4)).withSizeKeepingCentre(juce::jmin(180, footer.getWidth() / 4), 10));
        footer.removeFromRight(12);
    }
    footerArea = footer;

    detailArea = area.removeFromRight(juce::jmin(detailWidth, area.getWidth() * 45 / 100));
    area.removeFromRight(12);
    tableArea = area;
    table.setBounds(area);
    // En una ventana angosta el álbum no cabe (se ve en el detalle).
    auto& header = table.getHeader();
    const bool roomy = table.getWidth() >= 640;
    if (header.isColumnVisible(colAlbum) != roomy) header.setColumnVisible(colAlbum, roomy);
    const int fixed = 70 + 120 + table.getViewport()->getScrollBarThickness();
    const int flexible = juce::jmax(160, table.getWidth() - fixed);
    if (roomy) {
        header.setColumnWidth(colTitle, flexible * 40 / 100);
        header.setColumnWidth(colArtist, flexible * 30 / 100);
        header.setColumnWidth(colAlbum, flexible - flexible * 40 / 100 - flexible * 30 / 100);
    } else {
        header.setColumnWidth(colTitle, flexible * 55 / 100);
        header.setColumnWidth(colArtist, flexible - flexible * 55 / 100);
    }

    // Detalle: carátula y datos, si ya la tiene, los videos y las acciones abajo.
    auto d = detailArea.reduced(14);
    auto head = d.removeFromTop(coverSize);
    coverArea = head.removeFromLeft(coverSize);
    head.removeFromLeft(14);
    infoArea = head;
    d.removeFromTop(10);
    ownedArea = d.removeFromTop(24);
    d.removeFromTop(12);
    videosCaptionArea = d.removeFromTop(18);
    d.removeFromTop(6);

    // Abajo: el enlace a todo lo ancho, descargarlo o copiarlo, y abrirlo en cada servicio
    // (cada botón del ancho de su texto).
    const int h = UiStyle::controlHeight;
    auto columns = [h](juce::Rectangle<int> row, std::vector<juce::TextButton*> buttons, bool evenly) {
        std::vector<int> widths;
        int total = 0;
        for (auto* b : buttons) {
            widths.push_back(evenly ? 1 : b->getBestWidthForHeight(h) + 20);
            total += widths.back();
        }
        const int free = row.getWidth() - 8 * ((int) buttons.size() - 1);
        for (size_t i = 0; i < buttons.size(); ++i) {
            const int w = i + 1 == buttons.size() ? row.getWidth() : free * widths[i] / juce::jmax(1, total);
            buttons[i]->setBounds(row.removeFromLeft(w));
            row.removeFromLeft(8);
        }
    };
    columns(d.removeFromBottom(h), { &youTubeButton, &musicButton, &deezerButton, &spotifyButton }, false);
    d.removeFromBottom(6);
    if (downloadButton.isVisible()) columns(d.removeFromBottom(h), { &downloadButton, &copyButton }, true);
    else columns(d.removeFromBottom(h), { &copyButton }, true);
    d.removeFromBottom(8);
    linkField.setBounds(d.removeFromBottom(h));
    d.removeFromBottom(10);

    videoList.setBounds(d);
    videoMessageArea = d;
    searchYouTubeButton.setBounds(d.withSizeKeepingCentre(juce::jmin(d.getWidth(), searchYouTubeButton.getBestWidthForHeight(h) + 40), h)
                                   .translated(0, d.getHeight() / 2 - h - 6));
}

void OnlineSearchView::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();
    searchBox.setFont(UiStyle::ui(14.5f));
    searchBox.setTextToShowWhenEmpty(TRANS("online_search_placeholder"), t.getMutedTextColour());
    searchButton.setButtonText(TRANS("online_search_btn"));
    linkField.setFont(UiStyle::mono(12.5f));
    linkField.applyFontToAllText(UiStyle::mono(12.5f));
    copyButton.setButtonText(TRANS("online_search_copy"));
    copyButton.setTooltip(TRANS("tip_online_copy"));
    downloadButton.setTooltip(TRANS("tip_online_download"));
    youTubeButton.setButtonText(TRANS("online_search_open_youtube"));
    youTubeButton.setTooltip(TRANS("tip_online_youtube"));
    musicButton.setButtonText(TRANS("online_search_open_music"));
    musicButton.setTooltip(TRANS("tip_online_music"));
    deezerButton.setButtonText(TRANS("online_search_open_deezer"));
    deezerButton.setTooltip(TRANS("tip_online_deezer"));
    spotifyButton.setButtonText(TRANS("online_search_spotify"));
    spotifyButton.setTooltip(TRANS("tip_online_spotify"));
    searchYouTubeButton.setButtonText(TRANS("online_search_search_youtube"));
    closeButton.setButtonText(TRANS("review_close"));

    auto& header = table.getHeader();
    header.setColumnName(colTitle, TRANS("header_title"));
    header.setColumnName(colArtist, TRANS("header_artist"));
    header.setColumnName(colAlbum, TRANS("online_search_col_album"));
    header.setColumnName(colLength, TRANS("header_length"));
    header.setColumnName(colOwned, TRANS("online_search_col_owned"));
    table.setColour(juce::ListBox::backgroundColourId, t.getPanelBackgroundColour());
    videoList.setColour(juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    resized();
    repaint();
}

//==============================================================================

OnlineSearchWindow::OnlineSearchWindow(MusicLibrary& library, SongDownloader* downloader, const juce::String& query)
    : juce::DocumentWindow(TRANS("online_search_window_title"), ThemeManager::getInstance().getBaseBackgroundColour(),
                           juce::DocumentWindow::allButtons)
{
    setUsingNativeTitleBar(true);
    view = new OnlineSearchView(library, downloader);
    view->onCloseRequested = [this] { closeButtonPressed(); };
    view->onOpenReview = [this] { if (onOpenReview) onOpenReview(); };
    setContentOwned(view, false);
    setResizable(true, true);
    setResizeLimits(900, 560, 8000, 6000);

    // Que quepa en la pantalla (1366×768 incluida).
    const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    const auto screen = display != nullptr ? display->userArea : juce::Rectangle<int>(0, 0, 1366, 768);
    centreWithSize(juce::jmin(1180, screen.getWidth() - 40), juce::jmin(740, screen.getHeight() - 40));
    setVisible(true);
    search(query);
}

void OnlineSearchWindow::search(const juce::String& query) {
    if (query.trim().isNotEmpty()) view->search(query);
    else view->grabKeyboardFocus();
}

void OnlineSearchWindow::closeButtonPressed() {
    if (onClosed) onClosed();
}
