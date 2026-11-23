#pragma once
#include <JuceHeader.h>
#include "../library/ImportReview.h"
#include "CreditsEditor.h"
#include "ArtistsView.h"

class PreviewPlayer;
class OnlineIdentifier;

// Ventana de revisión: la lista de canciones con su estado (lista, por revisar, duplicada),
// el formato de cada carpeta, acciones para varias filas a la vez y el detalle de la fila
// elegida con sus dudas. Lo que se corrige queda guardado aunque se cierre la ventana;
// confirmar ordena los archivos en Library/Musica y las canciones pasan a sonar en automático.
class ImportReviewView : public juce::Component,
                         public juce::TableListBoxModel,
                         private juce::Timer,
                         private juce::ChangeListener
{
public:
    ImportReviewView(MusicLibrary& library, PreviewPlayer* preview, ImportReview::Scope scope);
    ~ImportReviewView() override;

    void showScope(ImportReview::Scope scope);
    // La búsqueda en línea (puede no haber): sus resultados aparecen como sugerencias.
    void setOnlineIdentifier(OnlineIdentifier* onlineIdentifier);

    std::function<void()> onLibraryChanged;  // se confirmó o se quitó algo
    std::function<void()> onCloseRequested;
    std::function<void()> onShowArtists;     // la tercera pestaña de la ventana
    void setArtistCount(int count);
    int getPendingCount() const { return pendingCount; }
    int getUnreviewedCount() const { return unreviewedCount; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent& e) override;
    void returnKeyPressed(int lastRowSelected) override;
    void deleteKeyPressed(int lastRowSelected) override;

private:
    enum class Filter { All, Check, Duplicates, Ready };

    MusicLibrary& library;
    PreviewPlayer* preview;
    OnlineIdentifier* identifier = nullptr;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    ImportReview review;

    Filter filter = Filter::All;
    juce::String groupFilter;       // clave de la carpeta; vacía = todas
    std::vector<int> visibleRows;   // índices de review.getItems() que se ven
    int detailRow = -1;             // fila de review en el detalle

    // Confirmación en curso, por tandas (la interfaz sigue respondiendo).
    std::vector<int> pendingConfirm;
    size_t confirmIndex = 0;
    ImportReview::Outcome confirmOutcome;
    bool busy() const { return !pendingConfirm.empty(); }

    juce::TextButton newScopeButton, libraryScopeButton, artistsScopeButton;
    // Deshacer una importación: lo que sigue por revisar de un lote sale de la biblioteca.
    juce::TextButton undoImportButton;
    int artistCount = 0;
    void showUndoMenu();
    juce::ComboBox groupCombo;
    juce::TextButton artistTitleButton, titleArtistButton;
    juce::TextButton filterAll, filterCheck, filterDuplicates, filterReady;
    juce::TextButton swapButton, categoryButton, caseButton, listenButton, discardButton;
    juce::TextButton useOnlineButton;  // "Usar datos en línea (N)": las sugerencias seguras de lo que se ve
    std::vector<int> strongOnlineRows() const;
    juce::String onlineStatusText() const;  // "Buscando en línea: 12 en cola"
    juce::String onlineStatusShown;
    juce::TableListBox table;

    juce::Viewport detailViewport;
    struct DetailPanel : public juce::Component {
        void paint(juce::Graphics& g) override;
        std::function<void(juce::Graphics&)> painter;
    } detail;
    CreditsEditor editor;
    juce::TextButton saveEditButton;
    juce::TextButton issueYesButton, issueNoButton;

    juce::TextButton closeButton, confirmSelectionButton, confirmReadyButton;
    juce::String footerMessage;
    juce::Colour footerColour;

    juce::Rectangle<int> countsArea, formatStatusArea, footerStatusArea, tableArea;
    juce::Rectangle<int> issueArea, sourceArea, destinationArea, detailTitleArea;

    void reload();  // después de recargar la revisión
    int pendingCount = 0, unreviewedCount = 0;
    void rebuildVisibleRows();
    void refreshControls();
    void refreshGroupCombo();
    const ImportReview::Group* currentGroup() const;
    std::vector<int> selectedRows() const;  // índices de review
    int reviewRowAt(int tableRow) const;
    void selectReviewRow(int reviewRow);

    void showDetail(int reviewRow);
    void layoutDetail();
    void paintDetail(juce::Graphics& g);
    void commitEdit(bool moveToNext);
    void selectNextToReview(int after);

    void startConfirm(std::vector<int> rows);
    void finishConfirm();
    void discardRows(const std::vector<int>& rows);
    void showCategoryMenu();
    void showCaseMenu();
    void showRowMenu(int reviewRow);
    void togglePreview();
    void answerIssue(bool yes);

    // La duda que se muestra con botones en el detalle: la más importante de la fila.
    ImportReview::Issue mainIssue(const ImportReview::Item& item) const;
    juce::String issueText(const ImportReview::Item& item, ImportReview::Issue issue) const;
    juce::String shortNote(const ImportReview::Item& item) const;
    juce::Colour stateColour(ImportReview::State state) const;
    juce::String categoryNames(const std::vector<int>& ids) const;
    std::vector<Category> categories;

    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ImportReviewView)
};

// La ventana de revisión y artistas (no es modal: la emisora sigue al aire mientras se
// revisa). Sus tres pestañas: lo nuevo, la biblioteca sin revisar y los artistas.
class ImportReviewWindow : public juce::DocumentWindow {
public:
    ImportReviewWindow(MusicLibrary& library, PreviewPlayer* preview, ImportReview::Scope scope);
    void closeButtonPressed() override;

    void showReview(ImportReview::Scope scope);
    void showArtists();
    void setOnlineIdentifier(OnlineIdentifier* identifier) { content->review.setOnlineIdentifier(identifier); }

    std::function<void()> onClosed;
    std::function<void()> onLibraryChanged;  // se confirmó, se quitó o se cambió un artista

private:
    class Content : public juce::Component {
    public:
        Content(MusicLibrary& library, PreviewPlayer* preview, ImportReview::Scope scope);
        void resized() override;
        ImportReviewView review;
        ArtistsView artists;
    };

    MusicLibrary& library;
    Content* content = nullptr;
    void updateArtistCount();
};
