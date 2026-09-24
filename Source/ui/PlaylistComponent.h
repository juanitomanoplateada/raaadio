#pragma once
#include <JuceHeader.h>
#include "../library/Playlist.h"
#include "../library/MusicLibrary.h"

class Deck;

class PlaylistComponent : public juce::Component,
                          public juce::TableListBoxModel,
                          public juce::DragAndDropTarget,
                          public juce::Timer
{
public:
    PlaylistComponent(Playlist& playlist, MusicLibrary& library);
    ~PlaylistComponent() override;

    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    void updateCache();
    void setDecks(Deck* d1, Deck* d2);

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground(juce::Graphics&, int rowNumber, int width, int height, bool rowIsSelected) override;
    void paintCell(juce::Graphics&, int rowNumber, int columnId, int width, int height, bool rowIsSelected) override;
    juce::Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected, juce::Component* existingComponentToUpdate) override;
    void cellDoubleClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void cellClicked(int rowNumber, int columnId, const juce::MouseEvent&) override;
    void deleteKeyPressed(int lastRowSelected) override;
    juce::var getDragSourceDescription(const juce::SparseSet<int>& selectedRows) override;
    void selectedRowsChanged(int lastRowSelected) override;

    // DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& dragSourceDetails) override;
    void itemDropped(const SourceDetails& dragSourceDetails) override;
    void itemDragEnter(const SourceDetails& dragSourceDetails) override;
    void itemDragMove(const SourceDetails& dragSourceDetails) override;
    void itemDragExit(const SourceDetails& dragSourceDetails) override;

    void timerCallback() override;

private:
    // Flechas para mover la selección; solo aparecen en las filas seleccionadas.
    class RowControlComponent : public juce::Component {
    public:
        RowControlComponent(PlaylistComponent& parent);
        void resized() override;
        void setRow(int row);
        void configure(bool showUp, bool showDown);
    private:
        PlaylistComponent& owner;
        int currentRow = -1;
        juce::TextButton upButton;
        juce::TextButton downButton;
    };

    struct NextUp {
        TrackInfo track;
        juce::String source;     // "DECK A", "DECK B" o vacío si viene de la cola
        double startsIn = -1.0;  // segundos; negativo si no se sabe
        bool valid = false;
    };
    NextUp computeNextUp() const;

    std::vector<int> getSelectedQueueIndices() const;
    void removeSelectedRows();
    void moveSelectionToTop();
    void playNow(int queueIndex);
    // Mueve las filas top..bottom de la cola una posición (delta -1 sube, +1 baja), solo si
    // siguen siendo las que se ven: AutoDJ pudo sacar pistas de la cola desde el último
    // refresco. Devuelve false si no se movió nada.
    bool shiftRows(int top, int bottom, int delta);
    void openGenerator();
    void openTrackDetails(const TrackInfo& track);

    Playlist& playlist;
    MusicLibrary& library;
    Deck* deckA = nullptr;
    Deck* deckB = nullptr;

    juce::Time baseStartTime;
    bool isSchedulingActive = false;
    std::vector<double> cumulativeDurations;
    std::vector<TrackInfo> trackCache;
    uint64_t localLastModified = 0;

    juce::TableListBox table;
    juce::TextButton generatorButton;
    juce::Rectangle<int> headerArea, nextArea;

    int insertionIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistComponent)
};
