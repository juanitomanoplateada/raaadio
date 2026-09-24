#pragma once
#include <JuceHeader.h>
#include "../audio/CartPlayer.h"
#include "DragFormats.h"

class MusicLibrary;

// Botonera: 12 cartuchos en una grilla de 4 x 3. Clic o F1-F12 para sonar o detener;
// se asignan arrastrando una pista (de la biblioteca, las cuñas o la cola) o con el clic
// derecho de la biblioteca, y se quitan con clic derecho sobre el cartucho.
class CartwallView : public juce::Component,
                     public juce::DragAndDropTarget,
                     private juce::ChangeListener
{
public:
    CartwallView(CartPlayer& player, MusicLibrary& library);
    ~CartwallView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Submenú "Asignar a la botonera" para el clic derecho de la biblioteca y de la lista de
    // cuñas: F1-F12 con lo que tiene cada uno. Los ids van de firstItemId a firstItemId + 11.
    static juce::PopupMenu createAssignMenu(const CartPlayer& player, int firstItemId);

    // Pista soltada sobre la pestaña de la botonera: va al primer cartucho libre. Devuelve
    // el cartucho, o -1 (y avisa si la botonera está llena).
    int assignToFirstFree(const juce::String& dragDescription);

    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override { repaint(); }
    int slotAt(juce::Point<int> position) const;

    CartPlayer& player;
    MusicLibrary& library;
    std::array<juce::Rectangle<int>, CartPlayer::numCarts> cells;
    juce::Rectangle<int> titleArea;
    int hoverSlot = -1;
    int dropSlot = -1;
};
