#include <JuceHeader.h>
#include "LibraryComponent.h"
#include "AdvancedImportDialog.h"
#include "FileChoosers.h"
#include "../network/OnlineIdentifier.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

namespace {
    class DropSwitchingTabButton : public juce::TabBarButton, public juce::DragAndDropTarget {
    public:
        DropSwitchingTabButton(const juce::String& name, juce::TabbedButtonBar& bar, DropSwitchingTabs& owner)
            : juce::TabBarButton(name, bar), tabs(owner) {}

        bool isInterestedInDragSource(const SourceDetails& details) override {
            return DragFormats::firstPath(details.description.toString()).isNotEmpty();
        }
        void itemDragEnter(const SourceDetails&) override {
            getTabbedButtonBar().setCurrentTabIndex(getIndex());
        }
        void itemDropped(const SourceDetails& details) override {
            if (tabs.onDropOnTab) tabs.onDropOnTab(getIndex(), details.description.toString());
        }

    private:
        DropSwitchingTabs& tabs;
    };
}

juce::TabBarButton* DropSwitchingTabs::createTabButton(const juce::String& tabName, int) {
    return new DropSwitchingTabButton(tabName, getTabbedButtonBar(), *this);
}

LibraryComponent::LibraryComponent(MusicLibrary& lib, HistoryManager& hm, AdvertisingManager& ads, CartPlayer& carts)
    : library(lib),
      historyManager(hm),
      adsManager(ads),
      libraryView(lib),
      historyView(hm),
      adsView(lib, ads),
      cartwallView(carts, lib),
      temporariesView(lib),
      progressBar(progress)
{
    addAndMakeVisible(tabs);
    tabs.setTabBarDepth(42);
    tabs.setOutline(0);
    tabs.setIndent(0);
    tabs.addTab(TRANS("tab_library"), juce::Colours::transparentBlack, &libraryView, false);
    tabs.addTab(TRANS("tab_history"), juce::Colours::transparentBlack, &historyView, false);
    tabs.addTab(TRANS("tab_ads"), juce::Colours::transparentBlack, &adsView, false);
    tabs.addTab(TRANS("tab_cartwall"), juce::Colours::transparentBlack, &cartwallView, false);
    tabs.addTab(TRANS("tab_temporaries"), juce::Colours::transparentBlack, &temporariesView, false);
    jassert(tabs.getNumTabs() == temporariesTab + 1);

    const char* tabIcons[] = { "list", "history", "megaphone", "grid", "hourglass" };
    for (int i = 0; i < tabs.getNumTabs(); ++i)
        if (auto* tab = tabs.getTabbedButtonBar().getTabButton(i))
            tab->getProperties().set("icon", tabIcons[i]);

    // Botonera: se asigna arrastrando una pista hasta un cartucho (la pestaña se abre sola
    // al pasar por encima), soltándola sobre la pestaña (primer cartucho libre) o con el
    // clic derecho de la biblioteca, de la lista de cuñas y de los temporales.
    tabs.onDropOnTab = [this](int index, const juce::String& description) {
        if (index == cartwallTab) cartwallView.assignToFirstFree(description);
    };
    // La pestaña abierta siempre muestra su nombre: al cambiar, se reparte de nuevo la fila.
    tabs.onTabChanged = [this](int) { resized(); };
    libraryView.setCartPlayer(&carts);
    adsView.setCartPlayer(&carts);
    temporariesView.setCartPlayer(&carts);
    temporariesView.onImportRequested = [this] { importTemporaries(); };
    libraryView.onSearchOnline = [this](const juce::String& query) { openOnlineSearch(query); };

    // Se agregan después de las pestañas para quedar por encima de su barra.
    addAndMakeVisible(advancedImportButton);
    UiStyle::setVariant(advancedImportButton, "primary");
    UiStyle::setIcon(advancedImportButton, "import");
    advancedImportButton.onClick = [this] { openAdvancedImportDialog(); };

    addAndMakeVisible(importAnunciosButton);
    UiStyle::setIcon(importAnunciosButton, "megaphone");
    importAnunciosButton.onClick = [this] { importAnuncios(); };

    addAndMakeVisible(importTemporalButton);
    UiStyle::setIcon(importTemporalButton, "hourglass");
    importTemporalButton.onClick = [this] { importTemporaries(); };

    addChildComponent(reviewButton);
    UiStyle::setVariant(reviewButton, "warning");
    UiStyle::setIcon(reviewButton, "list");
    reviewButton.onClick = [this] {
        openReview(pendingCount > 0 ? ImportReview::Scope::NewImports : ImportReview::Scope::Library);
    };

    addChildComponent(progressBar);
    addChildComponent(cancelButton);
    UiStyle::setVariant(cancelButton, "ghost");
    cancelButton.onClick = [this] { library.cancelImport(); };

    startTimer(150);
    lookAndFeelChanged();
}

LibraryComponent::~LibraryComponent() {
    stopTimer();
    reviewWindow = nullptr;
    onlineSearchWindow = nullptr;
}

void LibraryComponent::paint(juce::Graphics& g) {
    UiStyle::drawPanel(g, getLocalBounds().toFloat().reduced(0.5f));

    if (statusText.isNotEmpty()) {
        const auto font = UiStyle::ui(12.5f);
        const bool fits = juce::GlyphArrangement::getStringWidth(font, statusText) <= (float) statusArea.getWidth();
        g.setColour(ThemeManager::getInstance().getSecondaryTextColour());
        g.setFont(font);
        g.drawText(fits || statusShortText.isEmpty() ? statusText : statusShortText, statusArea,
                   juce::Justification::centredRight, true);
    }
}

void LibraryComponent::resized() {
    auto area = getLocalBounds().reduced(UiStyle::padding, 6);
    tabs.setBounds(area);

    // Acciones alineadas a la derecha, sobre la barra de pestañas.
    auto actions = area.removeFromTop(tabs.getTabBarDepth()).withTrimmedBottom(4);
    const int h = 32;
    const int available = actions.getWidth();
    auto& bar = tabs.getTabbedButtonBar();
    const int reviewCount = pendingCount > 0 ? pendingCount : unreviewedCount;
    auto widthOf = [h](juce::TextButton& b) {
        return b.getButtonText().isEmpty() ? h + 8 : b.getBestWidthForHeight(h) + 26;
    };

    // Si no cabe todo, por niveles (la ayuda emergente dice qué es cada cosa):
    // 1. "Importar cuñas" e "Importar temporal" solo con su icono, "Por revisar (N)" solo con N;
    // 2. las pestañas que no están abiertas, solo con su icono;
    // 3. todas las pestañas con su icono.
    int tabsRight = 0;
    for (int level = 0; level <= 3; ++level) {
        importAnunciosButton.setButtonText(level >= 1 ? juce::String() : TRANS("import_anuncios_btn"));
        importTemporalButton.setButtonText(level >= 1 ? juce::String() : TRANS("import_temporal_btn"));
        reviewButton.setButtonText(level >= 1 ? juce::String(reviewCount)
                                              : TRANS(pendingCount > 0 ? "review_button_pending" : "review_button_library")
                                                    .replace("%1", juce::String(reviewCount)));
        const auto names = tabs.getTabNames();
        for (int i = 0; i < tabs.getNumTabs(); ++i) {
            if (auto* tab = bar.getTabButton(i)) {
                const bool compact = level >= 3 || (level >= 2 && i != tabs.getCurrentTabIndex());
                tab->getProperties().set("compact", compact);
                tab->setTooltip(compact ? names[i] : juce::String());
            }
        }
        bar.resized();

        auto* lastTab = bar.getTabButton(tabs.getNumTabs() - 1);
        tabsRight = lastTab != nullptr ? lastTab->getRight() + 16 : 360;
        int used = tabsRight + widthOf(advancedImportButton) + 6 + widthOf(importAnunciosButton) + 6 + widthOf(importTemporalButton);
        if (reviewButton.isVisible()) used += 6 + reviewButton.getBestWidthForHeight(h) + 30;
        if (used <= available) break;
    }

    auto place = [&](juce::TextButton& b, int width) {
        b.setBounds(actions.removeFromRight(width).withSizeKeepingCentre(width, h));
        actions.removeFromRight(6);
    };
    place(advancedImportButton, widthOf(advancedImportButton));
    place(importAnunciosButton, widthOf(importAnunciosButton));
    place(importTemporalButton, widthOf(importTemporalButton));
    if (reviewButton.isVisible()) place(reviewButton, reviewButton.getBestWidthForHeight(h) + 30);

    // Progreso de importación y estado, a la izquierda de las acciones.
    actions.removeFromLeft(tabsRight);
    if (progressBar.isVisible()) {
        cancelButton.setBounds(actions.removeFromRight(90).withSizeKeepingCentre(90, 28));
        actions.removeFromRight(6);
        progressBar.setBounds(actions.removeFromRight(juce::jmin(160, actions.getWidth())).withSizeKeepingCentre(juce::jmin(160, actions.getWidth()), 10));
        actions.removeFromRight(8);
    }
    statusArea = actions;
}

void LibraryComponent::lookAndFeelChanged() {
    advancedImportButton.setButtonText(TRANS("advanced_import_btn"));
    advancedImportButton.setTooltip(TRANS("tip_import"));
    importAnunciosButton.setButtonText(TRANS("import_anuncios_btn"));
    importAnunciosButton.setTooltip(TRANS("tip_import_ads"));
    importTemporalButton.setButtonText(TRANS("import_temporal_btn"));
    importTemporalButton.setTooltip(TRANS("tip_import_temporal"));
    reviewButton.setTooltip(TRANS("tip_review_button"));
    cancelButton.setButtonText(TRANS("cancel"));

    tabs.setTabName(0, TRANS("tab_library"));
    tabs.setTabName(1, TRANS("tab_history"));
    tabs.setTabName(2, TRANS("tab_ads"));
    tabs.setTabName(cartwallTab, TRANS("tab_cartwall"));
    tabs.setTabName(temporariesTab, TRANS("tab_temporaries"));

    libraryView.lookAndFeelChanged();
    temporariesView.sendLookAndFeelChange();
    resized();
    repaint();
}

void LibraryComponent::timerCallback() {
    const bool importing = library.isImporting();

    if (importing != progressBar.isVisible()) {
        progressBar.setVisible(importing);
        cancelButton.setVisible(importing);
        resized();
        repaint();
    }

    // Una importación terminó (aunque haya durado menos que un tick): se refresca la
    // lista y se muestra el resumen unos segundos.
    if (!importing && library.getImportCount() != importsSummarised) {
        importsSummarised = library.getImportCount();
        libraryView.refresh();

        const auto summary = library.getLastImportSummary();
        const juce::String separator(juce::CharPointer_UTF8("  \xc2\xb7  "));
        importSummaryShortText = juce::String(summary.added) + " " + TRANS("import_added");
        if (summary.duplicates > 0)
            importSummaryShortText << separator << summary.duplicates << " " << TRANS("import_duplicates");
        if (summary.failed > 0)
            importSummaryShortText << separator << summary.failed << " " << TRANS("import_failed");
        if (summary.toReview > 0)
            importSummaryShortText << separator << summary.toReview << " " << TRANS("import_to_review");
        if (summary.needFfmpeg > 0)
            importSummaryShortText << separator << summary.needFfmpeg << " " << TRANS("import_need_ffmpeg");
        importSummaryText = TRANS("import_done") + ": " + importSummaryShortText;
        importSummaryUntil = juce::Time::getMillisecondCounter() + 15000;

        // La música nueva espera que alguien confirme artista, título y categoría.
        juce::Component::SafePointer<LibraryComponent> safe(this);
        std::function<void()> review;
        if (summary.toReview > 0) {
            if (onlineIdentifier != nullptr) onlineIdentifier->enqueuePending();  // busca en línea lo nuevo
            review = [safe] { if (safe != nullptr) safe->openReview(ImportReview::Scope::NewImports); };
        }
        if (summary.needFfmpeg > 0) offerFfmpeg(review);
        else if (review) review();
    }

    if (library.getCatalogVersion() != catalogVersionSeen) updateReviewButton();

    // FFmpeg pedido por la importación: instalado, se importa lo que lo esperaba.
    SongDownloader::Status tools;
    if (!ffmpegImport.files.isEmpty() && songDownloader != nullptr) {
        tools = songDownloader->getStatus();
        if (!tools.installing && !importing) {
            if (library.isConverterAvailable()) library.importFiles(ffmpegImport.files, ffmpegImport.category);
            else if (tools.installError.isNotEmpty()) {
                importSummaryText = importSummaryShortText = TRANS("online_tools_failed").replace("%1", tools.installErrorStep)
                                                             .replace("%2", TRANS("online_tools_error_" + tools.installError));
                importSummaryUntil = juce::Time::getMillisecondCounter() + 15000;
            }
            ffmpegImport = {};
        }
    }

    if (importing) {
        // Solo se lee el progreso (atómico): el texto de estado lo escribe el hilo de importación.
        progress = library.getProgress();
        setStatusText(TRANS("importing") + " " + juce::String(juce::roundToInt(progress * 100.0)) + "%");
    } else if (!ffmpegImport.files.isEmpty() && tools.installing) {
        auto text = TRANS("online_tools_installing").replace("%1", tools.installStep);
        if (tools.installProgress >= 0.0) text << "  " << juce::roundToInt(tools.installProgress * 100) << " %";
        setStatusText(text);
    } else if (juce::Time::getMillisecondCounter() < importSummaryUntil) {
        setStatusText(importSummaryText, importSummaryShortText);
    } else if (const int pending = library.getPendingAnalysisCount(); pending > 0) {
        setStatusText(TRANS("analysing_library") + ": " + juce::String(pending) + " " + TRANS("analysis_pending"),
                      TRANS("analysing_short") + ": " + juce::String(pending));
    } else {
        setStatusText({});
    }
}

void LibraryComponent::offerFfmpeg(const std::function<void()>& then) {
    const auto waiting = library.getFilesWaitingForFfmpeg();
    if (songDownloader == nullptr || waiting.files.isEmpty()) {
        if (then) then();
        return;
    }

    juce::Component::SafePointer<LibraryComponent> safe(this);
    auto install = [safe, waiting] {
        if (safe == nullptr || safe->songDownloader == nullptr) return;
        safe->ffmpegImport = waiting;
        safe->songDownloader->installTools({ DownloadTools::ffmpegName });  // si ya se está instalando, se espera
    };

    std::vector<DownloadTools::Asset> missing;
    for (const auto& asset : songDownloader->getMissingTools())
        if (asset.name == DownloadTools::ffmpegName) missing.push_back(asset);
    if (missing.empty() || songDownloader->getStatus().installing) {
        install();
        if (then) then();
        return;
    }

    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("import_ffmpeg_title"),
        TRANS("import_ffmpeg_text").replace("%1", juce::String(waiting.files.size()))
                                   .replace("%2", juce::String(DownloadTools::totalMegabytes(missing)))
                                   .replace("%3", DownloadTools::defaultFolder().getFullPathName()),
        TRANS("import_ffmpeg_install"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([install, then](int answer) {
            if (answer == 1) install();
            if (then) then();
        }));
}

void LibraryComponent::setStatusText(const juce::String& text, const juce::String& shortText) {
    if (text != statusText || shortText != statusShortText) {
        statusText = text;
        statusShortText = shortText;
        repaint(statusArea);
    }
}

void LibraryComponent::setDecks(Deck* dA, Deck* dB) {
    libraryView.setDecks(dA, dB);
    adsView.setDecks(dA, dB);
    temporariesView.setDecks(dA, dB);
}

void LibraryComponent::updateReviewButton() {
    catalogVersionSeen = library.getCatalogVersion();
    pendingCount = library.getPendingCount();
    unreviewedCount = library.getUnreviewedCount();
    reviewButton.setVisible(pendingCount > 0 || unreviewedCount > 0);
    UiStyle::setVariant(reviewButton, pendingCount > 0 ? "warning" : "");
    resized();
    repaint();
}

void LibraryComponent::openReview(ImportReview::Scope scope) {
    if (reviewWindow != nullptr) {
        reviewWindow->showReview(scope);
        reviewWindow->toFront(true);
        return;
    }

    reviewWindow = std::make_unique<ImportReviewWindow>(library, previewPlayer, scope);
    reviewWindow->setOnlineIdentifier(onlineIdentifier);
    reviewWindow->onLibraryChanged = [this] {
        libraryView.refresh();
        updateReviewButton();
    };
    // Se cierra después del clic: la ventana no se puede borrar dentro de su propio evento.
    juce::Component::SafePointer<LibraryComponent> safe(this);
    reviewWindow->onClosed = [safe] {
        juce::MessageManager::callAsync([safe] {
            if (safe == nullptr) return;
            safe->reviewWindow = nullptr;
            safe->libraryView.refresh();
            safe->updateReviewButton();
        });
    };
}

void LibraryComponent::openOnlineSearch(const juce::String& query) {
    if (onlineSearchWindow != nullptr) {
        onlineSearchWindow->search(query);
        onlineSearchWindow->toFront(true);
        return;
    }

    onlineSearchWindow = std::make_unique<OnlineSearchWindow>(library, songDownloader, query);
    // Se cierra después del clic: la ventana no se puede borrar dentro de su propio evento.
    juce::Component::SafePointer<LibraryComponent> safe(this);
    onlineSearchWindow->onOpenReview = [safe] { if (safe != nullptr) safe->openReview(ImportReview::Scope::NewImports); };
    onlineSearchWindow->onClosed = [safe] {
        juce::MessageManager::callAsync([safe] {
            if (safe != nullptr) safe->onlineSearchWindow = nullptr;
        });
    };
}

void LibraryComponent::refreshLibrary() {
    libraryView.refresh();
}

void LibraryComponent::setPlaylist(Playlist* p) {
    libraryView.setPlaylist(p);
    adsView.setPlaylist(p);
    temporariesView.setPlaylist(p);
}

void LibraryComponent::importAnuncios() {
    FileChoosers::chooseFiles(fileChooser, TRANS("select_files"), juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                              library.getSupportedFileWildcard(),
        [this](const juce::Array<juce::File>& files) {
            if (!files.isEmpty()) {
                library.importFiles(files, library.getSystemCategory(CategoryRole::Spots).name);
            }
        });
}

void LibraryComponent::importTemporaries() {
    FileChoosers::chooseFiles(fileChooser, TRANS("select_files"), juce::File::getSpecialLocation(juce::File::userHomeDirectory),
                              library.getSupportedFileWildcard(),
        [this](const juce::Array<juce::File>& files) {
            if (files.isEmpty()) return;
            library.importTemporaries(files);
            tabs.setCurrentTabIndex(temporariesTab);  // ahí aparecen al terminar
        });
}

void LibraryComponent::openAdvancedImportDialog() {
    auto* dialog = new AdvancedImportDialog(library);

    juce::DialogWindow::LaunchOptions opt;
    opt.content.setOwned(dialog);
    opt.dialogTitle = TRANS("dialog_advanced_import");
    opt.dialogBackgroundColour = ThemeManager::getInstance().getBaseBackgroundColour();
    opt.escapeKeyTriggersCloseButton = true;
    opt.resizable = false;
    opt.useNativeTitleBar = true;

    if (auto* dw = opt.launchAsync()) {
        dw->centreWithSize(520, 240);
    }
}
