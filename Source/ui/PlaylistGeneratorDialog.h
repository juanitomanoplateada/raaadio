#pragma once
#include <JuceHeader.h>
#include <map>
#include "../generator/GeneratorSettings.h"
#include "../generator/TemplateManager.h"
#include "../generator/PlaylistGenerator.h"
#include "../library/MusicLibrary.h"
#include "../library/Playlist.h"
#include "../helpers/ThemeManager.h"

// El generador de listas, en dos modos sobre los mismos ajustes:
// - Rápido: tres preguntas (cuánta música, qué música, dónde ponerla) con los valores por
//   defecto ya puestos; basta oprimir Generar.
// - Avanzado: incluir y excluir categorías, las reglas de separación, los separadores y las
//   plantillas.
// En los dos, un resumen dice antes de generar qué va a pasar (cuántas canciones hay, cuántas
// saldrán, qué reglas se usan) y avisa de lo que saldría mal; después de generar dice qué se
// hizo, y "Quitar lo generado" lo deshace.
class PlaylistGeneratorDialog : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::Timer {
public:
    PlaylistGeneratorDialog(MusicLibrary& lib, Playlist& playlist);
    ~PlaylistGeneratorDialog() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void showAdvanced(bool shouldBeAdvanced);

    std::function<void()> onCancel;
    std::function<void()> onGenerateSuccess;

    static constexpr int defaultWidth = 900, defaultHeight = 690;

private:
    friend class UiSnapshotRunner;  // las capturas muestran también el resultado y el aviso de reemplazo

    MusicLibrary& library;
    Playlist& playlist;
    TemplateManager templateManager;
    PlaylistGenerator generator;
    GeneratorSettings settings;
    bool advanced = false;

    // La biblioteca, leída al abrir: categorías de música y de la emisora, y cuántas
    // canciones tiene cada una.
    std::vector<Category> musicCategories, allCategories;
    std::map<int, int> songsInCategory;
    int allMusicCount = 0;
    int sweepersCategoryId = -1;

    // Lo que hay para generar con los ajustes de ahora, y lo último que se generó.
    PlaylistGenerator::Availability available;
    PlaylistGenerator::GenerationResult lastResult;
    bool canUndo = false, canGenerate = false;
    juce::String resultText;
    juce::Colour resultColour;
    int queueCount = 0;
    double queueSeconds = 0.0;

    struct SummaryLine { juce::String text; juce::Colour colour; bool strong = false; };
    std::vector<SummaryLine> summaryLines;
    juce::Colour summaryBorder;

    //==========================================================================
    // Arriba: el modo y las plantillas
    juce::TextButton simpleModeButton, advancedModeButton;
    juce::ComboBox templateCombo;
    juce::TextButton saveTemplateButton, deleteTemplateButton;

    // Rápido
    std::vector<int> durationChipMinutes;
    juce::OwnedArray<juce::TextButton> durationChips;
    struct ChipFlow : public juce::Component {
        juce::OwnedArray<juce::TextButton> chips;
        int layout(int width);  // acomoda los botones en filas; devuelve el alto que ocupan
    };
    juce::Viewport categoryViewport;
    ChipFlow categoryFlow;
    juce::ToggleButton separatorToggle;
    juce::ComboBox separatorEveryCombo;

    // Avanzado
    juce::ListBox categoryList;
    juce::TextEditor durEditor;
    juce::ComboBox durUnitCombo;
    juce::TextEditor artistSepEditor, titleSepEditor, catSepEditor, lastPlayEditor;
    juce::TextButton resetRulesButton;
    juce::ComboBox sepCatCombo;
    juce::TextEditor sepIntEditor;

    // Los dos modos
    juce::TextButton whereEndButton, whereStartButton, whereReplaceButton;
    juce::TextButton changeRulesButton;  // en el resumen del modo rápido
    juce::TextButton generateButton, closeButton, undoButton;

    // Textos que se pintan (títulos de sección, rótulos de las reglas): los ubica resized().
    struct PaintedText { juce::Rectangle<int> area; juce::String text; int style = 0; };  // 0 título, 1 texto, 2 apagado
    std::vector<PaintedText> paintedTexts;
    juce::Rectangle<int> summaryArea, footerTextArea, whereHintArea, durationNoteArea;

    //==========================================================================
    void loadLibrary();
    void buildCategoryChips();
    void refreshTemplates(const juce::String& select = {});
    void syncControlsFromSettings();
    void refresh();                 // después de cualquier cambio: resumen, botones y dibujo
    // La cola cambia sola mientras la ventana está abierta (AutoDJ saca pistas): el resumen
    // se pone al día cada pocos segundos.
    void timerCallback() override { refresh(); }
    void buildSummary();
    juce::String rulesSentence() const;
    juce::String categoryName(int id) const;

    void toggleIncluded(int categoryId);
    void toggleExcluded(int categoryId);
    void generate();
    void undoLast();
    void saveTemplate();
    void deleteTemplate();

    // Lista de categorías (avanzado)
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int rowNumber, const juce::MouseEvent& e) override;
    static void rowChipAreas(int width, int height, juce::Rectangle<int>& include, juce::Rectangle<int>& exclude);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistGeneratorDialog)
};
