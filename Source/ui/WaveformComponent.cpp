#include "WaveformComponent.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

WaveformComponent::WaveformComponent(juce::AudioThumbnail& t) : thumbnail(t) {
    thumbnail.addChangeListener(this);
}

WaveformComponent::~WaveformComponent() {
    thumbnail.removeChangeListener(this);
}

void WaveformComponent::paint(juce::Graphics& g) {
    auto& theme = ThemeManager::getInstance();
    const auto bounds = getLocalBounds().toFloat();
    const auto wellColour = theme.getBaseBackgroundColour().interpolatedWith(theme.getPanelBackgroundColour(), 0.35f);

    juce::Path frame;
    frame.addRoundedRectangle(bounds, UiStyle::controlRadius);
    g.saveState();
    g.reduceClipRegion(frame);

    g.setColour(wellColour);
    g.fillRect(bounds);

    const double length = thumbnail.getTotalLength();
    if (length > 0) {
        // La forma de onda se dibuja una vez en una imagen; al avanzar solo se repintan
        // las capas de progreso encima.
        if (!isImageValid || waveformImage.isNull() || waveformImage.getWidth() != getWidth() || waveformImage.getHeight() != getHeight()) {
            waveformImage = juce::Image(juce::Image::RGB, std::max(1, getWidth()), std::max(1, getHeight()), true);
            juce::Graphics imgG(waveformImage);
            imgG.fillAll(wellColour);
            imgG.setColour(theme.getWaveformColour());
            thumbnail.drawChannels(imgG, getLocalBounds().reduced(0, 6), 0.0, length, scalingFactor);
            isImageValid = true;
        }

        g.drawImageAt(waveformImage, 0, 0);

        auto xFor = [&](double seconds) { return (float) (seconds / length) * bounds.getWidth(); };
        const double end = (cueOutSeconds > cueInSeconds && cueOutSeconds < length) ? cueOutSeconds : length;

        // Zona de salida: los últimos segundos antes del punto de salida, en rojo tenue.
        if (endWarningSeconds > 0.0 && end > endWarningSeconds * 2.0) {
            g.setColour(theme.getErrorColour().withAlpha(0.16f));
            g.fillRect(bounds.withLeft(xFor(end - endWarningSeconds)).withRight(xFor(end)));
        }

        // Silencio de cabeza y cola (fuera de los puntos de entrada y salida): atenuado,
        // con una línea en cada punto.
        const auto markerColour = theme.getSecondaryTextColour().withAlpha(0.7f);
        if (cueInSeconds > 0.0) {
            const float x = xFor(cueInSeconds);
            g.setColour(wellColour.withAlpha(0.78f));
            g.fillRect(bounds.withRight(x));
            if (x >= 2.0f) {
                g.setColour(markerColour);
                g.fillRect(x - 0.5f, 0.0f, 1.0f, bounds.getHeight());
            }
        }
        if (end < length) {
            const float x = xFor(end);
            g.setColour(wellColour.withAlpha(0.78f));
            g.fillRect(bounds.withLeft(x));
            if (x <= bounds.getWidth() - 2.0f) {
                g.setColour(markerColour);
                g.fillRect(x - 0.5f, 0.0f, 1.0f, bounds.getHeight());
            }
        }

        // Lo ya reproducido se atenúa para que el ojo vaya a lo que falta.
        const float playX = (float)(currentPosition * bounds.getWidth());
        g.setColour(wellColour.withAlpha(0.62f));
        g.fillRect(bounds.withRight(playX));

        g.setColour(theme.getPrimaryTextColour());
        g.fillRect(playX - 1.0f, 0.0f, 2.0f, bounds.getHeight());
        juce::Path head;
        head.addTriangle(playX - 5.0f, 0.0f, playX + 5.0f, 0.0f, playX, 6.0f);
        g.fillPath(head);
    } else {
        g.setColour(theme.getMutedTextColour());
        g.setFont(UiStyle::ui(14.0f));
        g.drawText(TRANS("no_audio"), bounds, juce::Justification::centred, true);
    }

    g.restoreState();
}

void WaveformComponent::lookAndFeelChanged() {
    isImageValid = false;
    repaint();
}

void WaveformComponent::changeListenerCallback(juce::ChangeBroadcaster*) {
    isImageValid = false;
    repaint();
}

void WaveformComponent::resized() {
    isImageValid = false;
}

void WaveformComponent::setPosition(double fraction) {
    if (fraction != currentPosition) {
        currentPosition = fraction;
        repaint();
    }
}

bool WaveformComponent::canSeek(const juce::MouseEvent& e) const {
    if (thumbnail.getTotalLength() <= 0) return false;
    if (isSeekLocked && isSeekLocked()) return e.mods.isShiftDown();
    return true;
}

void WaveformComponent::mouseMove(const juce::MouseEvent& e) {
    setMouseCursor(canSeek(e) ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void WaveformComponent::mouseDown(const juce::MouseEvent& e) {
    if (!canSeek(e)) return;

    double fraction = (double)e.x / getWidth();
    fraction = std::max(0.0, std::min(1.0, fraction));

    if (onPositionChanged) onPositionChanged(fraction);
}

void WaveformComponent::mouseDrag(const juce::MouseEvent& e) {
    mouseDown(e);
}
