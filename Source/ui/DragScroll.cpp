#include "DragScroll.h"

WheelRouter::~WheelRouter() {
    end();
}

void WheelRouter::begin(juce::Component* dragSource) {
    end();
    window = dragSource != nullptr ? dragSource->getTopLevelComponent() : nullptr;
    sourceViewport = viewportOf(dragSource);
    if (sourceViewport != nullptr) pinned = sourceViewport->getViewPosition();
    juce::Desktop::getInstance().addGlobalMouseListener(this);
    listening = true;
}

void WheelRouter::end() {
    if (listening) juce::Desktop::getInstance().removeGlobalMouseListener(this);
    listening = false;
    window = nullptr;
    sourceViewport = nullptr;
}

juce::Viewport* WheelRouter::viewportOf(juce::Component* component) {
    for (auto* c = component; c != nullptr; c = c->getParentComponent()) {
        if (auto* list = dynamic_cast<juce::ListBox*>(c)) return list->getViewport();
        if (auto* viewport = dynamic_cast<juce::Viewport*>(c)) return viewport;
    }
    return nullptr;
}

void WheelRouter::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) {
    if (!listening || window == nullptr) return;

    // La imagen que acompaña al puntero no intercepta el mouse: no se confunde con una lista.
    auto* under = window->getComponentAt(window->getLocalPoint(nullptr, e.getScreenPosition()));
    auto* target = viewportOf(under);

    // Sobre la propia lista de origen: se desplaza ella, como siempre.
    if (target != nullptr && target == sourceViewport.getComponent()) {
        pinned = sourceViewport->getViewPosition();
        return;
    }

    // Sobre otra cosa: la de origen vuelve a donde estaba (el arrastre no la mueve) y se
    // desplaza la lista que está bajo el puntero, si hay una.
    if (sourceViewport != nullptr) sourceViewport->setViewPosition(pinned);
    if (target != nullptr) {
        target->useMouseWheelMoveIfNeeded(e.getEventRelativeTo(target), wheel);
        // Quien recibe el arrastre vuelve a calcular dónde caería la pista.
        e.source.triggerFakeMove();
    }
}
