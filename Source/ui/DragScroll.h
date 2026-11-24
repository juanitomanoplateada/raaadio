#pragma once
#include <JuceHeader.h>

// La rueda del mouse mientras se arrastra una pista. JUCE se la manda al componente donde
// empezó el arrastre (la lista de origen, por ejemplo la biblioteca) aunque el puntero esté
// sobre otra lista (la cola), así que se movía la lista equivocada. WheelRouter la reparte
// como espera el operador: se desplaza la lista que está bajo el puntero y la de origen se
// queda donde estaba. Escucha los eventos del escritorio solo durante el arrastre:
// MainComponent lo arranca y lo detiene (dragOperationStarted / dragOperationEnded).
class WheelRouter : public juce::MouseListener
{
public:
    WheelRouter() = default;
    ~WheelRouter() override;

    void begin(juce::Component* dragSource);
    void end();
    bool isActive() const { return listening; }

    // Llega después de que el componente de origen ya usó la rueda.
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

    // Lo que desplaza a un componente: la lista que lo contiene o el Viewport más cercano.
    static juce::Viewport* viewportOf(juce::Component* component);

private:
    // Lo que está bajo el puntero se busca en la ventana del arrastre, sin preguntarle a
    // Windows qué ventana está encima (otra ventana que la tape no confunde la búsqueda).
    juce::Component::SafePointer<juce::Component> window;
    juce::Component::SafePointer<juce::Viewport> sourceViewport;
    juce::Point<int> pinned;  // dónde tiene que quedar la lista de origen
    bool listening = false;

    JUCE_DECLARE_NON_COPYABLE(WheelRouter)
};
