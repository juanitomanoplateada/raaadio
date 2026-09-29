#pragma once
#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "../network/SongSearcher.h"
#include "../network/SongDownloader.h"

// Buscar una canción en internet: los resultados de Deezer (con los que ya están en la
// biblioteca marcados) y, para la elegida, su detalle y sus enlaces de YouTube, del más al
// menos parecido. El enlace se copia o se abre en el navegador; también se puede abrir la
// canción en Deezer o buscarla en Spotify. Con SongDownloader, el video elegido se descarga a
// la biblioteca (por revisar); abajo se ve cómo va la fila de descargas.
class OnlineSearchView : public juce::Component,
                         public juce::TableListBoxModel,
                         private juce::ChangeListener,
                         private juce::Timer
{
public:
    // transport: para las capturas de la interfaz y las pruebas; por omisión, internet.
    // downloader: puede no haber (entonces no se ofrece descargar).
    OnlineSearchView(MusicLibrary& library, SongDownloader* downloader, HttpTransport::Transport transport = {});
    ~OnlineSearchView() override;

    // Escribe la búsqueda y la hace.
    void search(const juce::String& query);
    std::function<void()> onCloseRequested;
    std::function<void()> onOpenReview;  // lo descargado quedó por revisar

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    bool keyPressed(const juce::KeyPress& key) override;

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;

private:
    MusicLibrary& library;
    SongDownloader* downloader = nullptr;
    SongSearcher searcher;

    SongSearcher::Results results;
    std::vector<int> ownedIds;           // por fila: la canción de la biblioteca, -1 si no está
    std::vector<Database::KeyRow> libraryKeys;
    int libraryKeysVersion = -1;

    int detailRow = -1;                  // fila de results en el detalle
    SongSearcher::Links links;
    bool hasLinks = false;
    int chosenVideo = -1;                // índice en links.videos
    juce::int64 chosenForTrack = 0;      // la canción para la que se eligió (a mano) otro video

    juce::TextEditor searchBox;
    juce::TextButton searchButton;
    juce::TableListBox table;

    juce::ListBox videoList;
    struct VideoModel : public juce::ListBoxModel {
        explicit VideoModel(OnlineSearchView& o) : owner(o) {}
        int getNumRows() override;
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
        void selectedRowsChanged(int lastRowSelected) override;
        void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
        OnlineSearchView& owner;
    } videoModel { *this };

    juce::TextEditor linkField;
    juce::TextButton downloadButton, copyButton, youTubeButton, musicButton, deezerButton, spotifyButton, searchYouTubeButton;
    juce::TextButton closeButton;

    // La fila de descargas, en el pie: cómo va, y la acción que corresponde (cancelar,
    // reintentar la instalación, revisar lo descargado).
    double downloadProgress = 0.0;
    juce::ProgressBar downloadBar { downloadProgress };
    juce::TextButton downloadActionButton;
    std::function<void()> downloadAction;
    juce::String downloadText;
    juce::Colour downloadColour;
    struct Ticker : public juce::Timer {  // la cuenta regresiva de la siguiente descarga
        std::function<void()> tick;
        void timerCallback() override { if (tick) tick(); }
    } ticker;

    juce::String footerMessage;
    juce::Colour footerColour;
    juce::uint32 footerUntil = 0;

    juce::Rectangle<int> statusArea, tableArea, detailArea, coverArea, infoArea, ownedArea, videosCaptionArea,
                         videoMessageArea, footerArea;

    void startSearch();
    void showResults();
    void showDetail(int row);
    void refreshLinks();
    void chooseVideo(int index);
    void refreshButtons();
    juce::String chosenLink() const;
    void copyLink();
    void flash(const juce::String& message, juce::Colour colour);

    // Descargar: el aviso de uso (una vez), las herramientas (si faltan), las dudas (ya la
    // tienes, video dudoso) y a la fila.
    void startDownload();
    void confirmDownload(int step);
    void refreshDownloads();
    std::optional<SongDownloader::Job> currentJob() const;  // la de la canción del detalle
    juce::String problemText(SongDownload::Problem problem, const juce::String& detail) const;
    juce::String jobChip(const SongDownloader::Job& job, juce::Colour& colour) const;

    juce::String statusText() const;
    juce::String errorText(const juce::String& code) const;
    juce::String matchLabel(double score) const;
    juce::Colour matchColour(double score) const;
    juce::String kindLabel(const SongSearch::Video& video) const;

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OnlineSearchView)
};

// La búsqueda en su propia ventana (no es modal: la emisora sigue al aire mientras se busca).
class OnlineSearchWindow : public juce::DocumentWindow {
public:
    OnlineSearchWindow(MusicLibrary& library, SongDownloader* downloader, const juce::String& query);
    void closeButtonPressed() override;
    void search(const juce::String& query);

    std::function<void()> onClosed;
    std::function<void()> onOpenReview;

private:
    OnlineSearchView* view = nullptr;
};
