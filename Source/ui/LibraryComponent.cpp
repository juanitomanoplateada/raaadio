#include <JuceHeader.h>
#include "LibraryComponent.h"
#include "AdvancedImportDialog.h"
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

    const char* tabIcons[] = { "list", "history", "megaphone", "grid" };
    for (int i = 0; i < 4; ++i)
        if (auto* tab = tabs.getTabbedButtonBar().getTabButton(i))
            tab->getProperties().set("icon", tabIcons[i]);

    // Botonera: se asigna arrastrando una pista hasta un cartucho (la pestaña se abre sola
    // al pasar por encima), soltándola sobre la pestaña (primer cartucho libre) o con el
    // clic derecho de la biblioteca y de la lista de cuñas.
    const int cartwallTab = tabs.getNumTabs() - 1;
    tabs.onDropOnTab = [this, cartwallTab](int index, const juce::String& description) {
        if (index == cartwallTab) cartwallView.assignToFirstFree(description);
    };
    libraryView.setCartPlayer(&carts);
    adsView.setCartPlayer(&carts);

    // Se agregan después de las pestañas para quedar por encima de su barra.
    addAndMakeVisible(advancedImportButton);
    UiStyle::setVariant(advancedImportButton, "primary");
    UiStyle::setIcon(advancedImportButton, "import");
    advancedImportButton.onClick = [this] { openAdvancedImportDialog(); };

    addAndMakeVisible(importAnunciosButton);
    UiStyle::setIcon(importAnunciosButton, "megaphone");
    importAnunciosButton.onClick = [this] { importAnuncios(); };

    addChildComponent(progressBar);
    addChildComponent(cancelButton);
    UiStyle::setVariant(cancelButton, "ghost");
    cancelButton.onClick = [this] { library.cancelImport(); };

    startTimer(150);
    lookAndFeelChanged();
}

LibraryComponent::~LibraryComponent() {
    stopTimer();
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
    auto place = [&](juce::TextButton& b, int width) {
        b.setBounds(actions.removeFromRight(width).withSizeKeepingCentre(width, h));
        actions.removeFromRight(6);
    };

    // Si las pestañas y los botones no caben, "Importar anuncios" queda solo con su icono
    // (la ayuda emergente dice qué hace).
    auto* lastTab = tabs.getTabbedButtonBar().getTabButton(tabs.getNumTabs() - 1);
    const int tabsRight = lastTab != nullptr ? lastTab->getRight() + 16 : 360;
    importAnunciosButton.setButtonText(TRANS("import_anuncios_btn"));
    const int importWidth = advancedImportButton.getBestWidthForHeight(h) + 26;
    const int adsWidth = importAnunciosButton.getBestWidthForHeight(h) + 26;
    const bool compact = tabsRight + importWidth + adsWidth + 12 > actions.getRight() - actions.getX();
    if (compact) importAnunciosButton.setButtonText({});

    place(advancedImportButton, importWidth);
    place(importAnunciosButton, compact ? h + 8 : adsWidth);

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
    cancelButton.setButtonText(TRANS("cancel"));

    tabs.setTabName(0, TRANS("tab_library"));
    tabs.setTabName(1, TRANS("tab_history"));
    tabs.setTabName(2, TRANS("tab_ads"));
    tabs.setTabName(3, TRANS("tab_cartwall"));

    libraryView.lookAndFeelChanged();
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
        importSummaryText = TRANS("import_done") + ": " + importSummaryShortText;
        importSummaryUntil = juce::Time::getMillisecondCounter() + 15000;
    }

    if (importing) {
        // Solo se lee el progreso (atómico): el texto de estado lo escribe el hilo de importación.
        progress = library.getProgress();
        setStatusText(TRANS("importing") + " " + juce::String(juce::roundToInt(progress * 100.0)) + "%");
    } else if (juce::Time::getMillisecondCounter() < importSummaryUntil) {
        setStatusText(importSummaryText, importSummaryShortText);
    } else if (const int pending = library.getPendingAnalysisCount(); pending > 0) {
        setStatusText(TRANS("analysing_library") + ": " + juce::String(pending) + " " + TRANS("analysis_pending"),
                      TRANS("analysing_short") + ": " + juce::String(pending));
    } else {
        setStatusText({});
    }
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
}

void LibraryComponent::refreshLibrary() {
    libraryView.refresh();
}

void LibraryComponent::setPlaylist(Playlist* p) {
    libraryView.setPlaylist(p);
    adsView.setPlaylist(p);
}

void LibraryComponent::importAnuncios() {
    fileChooser.reset(new juce::FileChooser(TRANS("select_files"), juce::File::getSpecialLocation(juce::File::userHomeDirectory), library.getSupportedFileWildcard()));
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems,
        [this](const juce::FileChooser& fc) {
            auto files = fc.getResults();
            if (!files.isEmpty()) {
                library.importFiles(files, library.getSystemCategory(CategoryRole::Spots).name);
            }
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
