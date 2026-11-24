#pragma once

#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "../library/TemporaryAudio.h"
#include "../network/SongDownloader.h"

class Deck;
class Playlist;
class CartPlayer;
class PreviewPlayer;

// Pestaña Temporales: los audios de una sola ocasión (un saludo, un mensaje, una nota de voz, un
// audio de redes), aparte de la música y de las cuñas. Desde aquí se preescuchan por CUE, se
// ponen al aire, se cargan en un deck, se mandan a la cola o a la botonera (también
// arrastrándolos), se renombran y se borran: uno por uno o todos con "Vaciar", siempre con
// confirmación, y nunca lo que está en un deck, sonando en la botonera o en la cola.
// Arriba, un campo para pegar el enlace de una publicación (Facebook, Instagram, X...) y bajar
// su audio; también se pueden soltar archivos sobre la lista.
class TemporariesView : public juce::Component,
                        public juce::TableListBoxModel,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    explicit TemporariesView(MusicLibrary& library);
    ~TemporariesView() override;

    void setDecks(Deck* a, Deck* b) { deckA = a; deckB = b; }
    void setPlaylist(Playlist* p) { playlist = p; }
    void setCartPlayer(CartPlayer* c) { cartPlayer = c; }
    void setPreviewPlayer(PreviewPlayer* p) { previewPlayer = p; }
    void setSongDownloader(SongDownloader* downloader);
    // "Importar temporal" del aviso de lista vacía (el mismo que el de la fila de pestañas).
    std::function<void()> onImportRequested;

    void refresh();

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;
    void visibilityChanged() override;

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void deleteKeyPressed(int lastRowSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;
    juce::String getCellTooltip(int rowNumber, int columnId) override;

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files, int x, int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    MusicLibrary& library;
    Deck* deckA = nullptr;
    Deck* deckB = nullptr;
    Playlist* playlist = nullptr;
    CartPlayer* cartPlayer = nullptr;
    PreviewPlayer* previewPlayer = nullptr;
    SongDownloader* downloader = nullptr;

    juce::TableListBox table;
    std::vector<TrackInfo> tracks;
    int catalogVersionSeen = -1;
    // Lo que está en uso (para las marcas de cada fila y para no borrarlo), y en qué cartucho.
    TemporaryAudio::Usage usage;
    std::map<int, int> cartSlotOf;  // pista → cartucho (0-11)
    bool fileDragOver = false;

    // Enlace de una red social
    juce::TextEditor linkEditor;
    juce::TextButton downloadButton;
    juce::String linkText;
    juce::Colour linkColour;
    double linkProgress = -1.0;  // < 0: sin barra
    bool linkBar = false;
    juce::TextButton linkActionButton;  // Cancelar / Reintentar / Instalar
    std::function<void()> linkAction;
    juce::Rectangle<int> linkStatusArea, linkBarArea;
    int dismissedLinkJob = 0;     // el último resultado que ya no se muestra
    int selectAfterDownload = 0;  // la pista que se seleccionará cuando aparezca
    int selectedLinkJob = 0;      // el enlace cuyo audio ya se seleccionó en la lista

    // Mensaje breve (lo que se hizo, o por qué no se pudo)
    juce::String flashText;
    juce::Colour flashColour;
    juce::uint32 flashUntil = 0;

    juce::TextButton emptyButton;   // Vaciar
    juce::TextButton cueButton, airButton, deckAButton, deckBButton, queueButton, cartButton, renameButton, removeButton;
    juce::TextButton importEmptyButton;
    juce::Rectangle<int> countArea, actionsArea;

    std::vector<TrackInfo> selectedTracks() const;
    TemporaryAudio::Usage currentUsage(std::map<int, int>* cartSlots = nullptr) const;
    juce::String busyText(const TrackInfo& track, TemporaryAudio::Busy busy) const;
    juce::String displayTitle(const TrackInfo& track) const;
    void flash(const juce::String& text, juce::Colour colour);

    void togglePreview();
    void playNow();
    void loadInto(Deck* deck, const juce::String& deckName);
    void showQueueMenu();
    void addToQueue(bool next);
    void showCartMenu();
    void renameSelected();
    void removeSelected();
    void emptyAll();
    // Antes de borrar: se detiene la preescucha y se sueltan los cartuchos que no suenan (los
    // dos tienen el archivo abierto).
    void releaseFiles(const std::vector<int>& trackIds);

    void startLinkDownload();
    void confirmLinkDownload(int step, const juce::String& url);
    void refreshLinkStatus();
    juce::String linkProblemText(const SongDownloader::Job& job) const;
    void updateButtons();
    void layoutActions();

    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TemporariesView)
};
