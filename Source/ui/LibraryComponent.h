#pragma once

#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "../library/HistoryManager.h"
#include "LibraryView.h"
#include "HistoryView.h"
#include "../library/AdvertisingManager.h"
#include "AdvertisingComponent.h"
#include "CartwallView.h"
#include "ImportReviewView.h"
#include "OnlineSearchView.h"

class Deck;
class Playlist;
class PreviewPlayer;

// Pestañas que se abren solas al pasar por encima arrastrando una pista: así una canción
// de la biblioteca llega a la botonera, que está en otra pestaña del mismo panel.
// Soltarla sobre la pestaña misma avisa con onDropOnTab.
class DropSwitchingTabs : public juce::TabbedComponent {
public:
    DropSwitchingTabs() : juce::TabbedComponent(juce::TabbedButtonBar::TabsAtTop) {}
    std::function<void(int tabIndex, const juce::String& dragDescription)> onDropOnTab;

protected:
    juce::TabBarButton* createTabButton(const juce::String& tabName, int tabIndex) override;
};

// Panel derecho: biblioteca, historial, pauta publicitaria y botonera en pestañas, con las
// acciones de importación en la misma fila de las pestañas.
class LibraryComponent : public juce::Component, public juce::Timer {
public:
    LibraryComponent(MusicLibrary& library, HistoryManager& historyManager, AdvertisingManager& adsManager,
                     CartPlayer& cartPlayer);
    ~LibraryComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void timerCallback() override;

    void setDecks(Deck* dA, Deck* dB);
    void setPlaylist(Playlist* p);
    void refreshLibrary();  // tras cambios hechos fuera del panel (p. ej., borrar la biblioteca)
    void setPreviewPlayer(PreviewPlayer* player) { previewPlayer = player; }
    void setOnlineIdentifier(OnlineIdentifier* identifier) { onlineIdentifier = identifier; }
    void setSongDownloader(SongDownloader* downloader) { songDownloader = downloader; }
    // Abre la revisión de canciones (o la trae al frente si ya estaba abierta).
    void openReview(ImportReview::Scope scope);
    // Abre la búsqueda en internet (o la trae al frente) y busca query, si no está vacía.
    void openOnlineSearch(const juce::String& query);

private:
    MusicLibrary& library;
    HistoryManager& historyManager;
    AdvertisingManager& adsManager;

    DropSwitchingTabs tabs;
    LibraryView libraryView;
    HistoryView historyView;
    AdvertisingComponent adsView;
    CartwallView cartwallView;

    juce::TextButton advancedImportButton;
    juce::TextButton importAnunciosButton;
    // "Por revisar (N)": lo importado que espera, o la biblioteca que nadie ha confirmado.
    juce::TextButton reviewButton;
    int pendingCount = 0, unreviewedCount = 0, catalogVersionSeen = -1;
    PreviewPlayer* previewPlayer = nullptr;
    OnlineIdentifier* onlineIdentifier = nullptr;
    SongDownloader* songDownloader = nullptr;
    std::unique_ptr<ImportReviewWindow> reviewWindow;
    std::unique_ptr<OnlineSearchWindow> onlineSearchWindow;
    void updateReviewButton();

    double progress = 0.0;
    juce::ProgressBar progressBar;
    juce::TextButton cancelButton;
    // Texto junto a las pestañas: progreso de la importación, su resumen al terminar
    // (durante unos segundos) o el análisis de la biblioteca en segundo plano.
    juce::String statusText, statusShortText;  // la corta se usa si la larga no cabe
    juce::Rectangle<int> statusArea;
    juce::String importSummaryText, importSummaryShortText;
    juce::uint32 importSummaryUntil = 0;
    int importsSummarised = 0;

    void setStatusText(const juce::String& text, const juce::String& shortText = {});

    std::unique_ptr<juce::FileChooser> fileChooser;

    void importAnuncios();
    void openAdvancedImportDialog();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryComponent)
};
