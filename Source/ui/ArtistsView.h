#pragma once
#include <JuceHeader.h>
#include "../library/MusicLibrary.h"
#include "../library/ImportReview.h"

// Artistas de la biblioteca: cada uno con sus canciones como intérprete, invitado o
// compositor y las formas en que también se reconoce (alias). Aquí se corrige cómo se escribe
// uno (todas sus canciones y archivos cambian) y se fusionan los que son el mismo escrito de
// dos formas; la vista sugiere los parecidos ("Wilfridovargas" y "Wilfrido Vargas").
class ArtistsView : public juce::Component,
                    public juce::TableListBoxModel
{
public:
    explicit ArtistsView(MusicLibrary& library);
    ~ArtistsView() override;

    void reload();

    std::function<void(ImportReview::Scope)> onShowReview;  // las otras dos pestañas de la ventana
    std::function<void()> onLibraryChanged;
    std::function<void()> onCloseRequested;
    // Cuántas hay por revisar en cada pestaña (lo pone la ventana, que ya las cuenta).
    void setScopeCounts(int pending, int unreviewed);

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    int getNumRows() override;
    void paintRowBackground(juce::Graphics& g, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    void selectedRowsChanged(int lastRowSelected) override;

private:
    enum class Filter { All, Performers, Composers, Similar };

    MusicLibrary& library;
    std::vector<Database::ArtistOverview> artists;
    std::map<int, int> similarTo;          // id → el artista parecido con más canciones
    std::set<juce::String> distinctPairs;  // "clave|clave" que el operador dijo que no son el mismo
    std::vector<int> visibleRows;          // índices de artists
    Filter filter = Filter::All;
    int detailIndex = -1;                  // índice en artists del artista en el detalle
    int pendingCount = 0, unreviewedCount = 0;

    juce::TextButton newScopeButton, libraryScopeButton, artistsScopeButton;
    juce::TextEditor searchBox;
    juce::TextButton filterAll, filterPerformers, filterComposers, filterSimilar;
    juce::TableListBox table;

    juce::TextEditor nameEditor;
    juce::TextButton renameButton, mergeSuggestedButton, notSameButton, mergeSelectedButton;
    juce::OwnedArray<juce::TextButton> aliasButtons;
    juce::ListBox songList;
    struct SongModel : public juce::ListBoxModel {
        std::vector<TrackInfo> tracks;
        int getNumRows() override { return (int) tracks.size(); }
        void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;
    } songs;

    juce::TextButton closeButton;
    juce::String footerMessage;
    juce::Colour footerColour;
    juce::Rectangle<int> detailArea, countsArea, summaryArea, suggestionArea, aliasCaptionArea, songsCaptionArea,
                         footerArea, tableArea;

    void rebuildVisibleRows();
    void findSimilar();
    void showDetail(int artistIndex);
    void refreshControls();
    int indexOfId(int artistId) const;
    std::vector<int> selectedIndices() const;

    void rename();
    void merge(std::vector<int> fromIds, int intoId);
    void markNotSame();
    void report(const MusicLibrary::ArtistResult& result, const juce::String& done);
    static juce::String pairKey(const std::string& a, const std::string& b);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ArtistsView)
};
