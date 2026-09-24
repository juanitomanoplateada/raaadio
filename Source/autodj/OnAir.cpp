#include "OnAir.h"

namespace OnAir {

bool isOnAir(const Deck& deck) {
    return deck.isPlaying() && deck.isRoutedToMain();
}

const Deck* nowPlaying(const Deck& a, const Deck& b) {
    const bool aOnAir = isOnAir(a), bOnAir = isOnAir(b);
    if (aOnAir && bOnAir) return a.getLastPlayTime() > b.getLastPlayTime() ? &a : &b;
    if (aOnAir) return &a;
    if (bOnAir) return &b;
    return nullptr;
}

bool playNow(Deck& a, Deck& b, const juce::File& file) {
    if (!file.existsAsFile()) return false;

    // El deck libre. Si suenan los dos, el que está en preescucha; y en plena mezcla (los
    // dos al aire), el que arrancó antes, que es el que ya se está yendo.
    Deck* target;
    if (!a.isPlaying()) target = &a;
    else if (!b.isPlaying()) target = &b;
    else if (!isOnAir(a)) target = &a;
    else if (!isOnAir(b)) target = &b;
    else target = b.getLastPlayTime() < a.getLastPlayTime() ? &b : &a;
    Deck& other = target == &a ? b : a;

    if (target->isPlaying()) target->stop();  // una preescucha, o el final de la saliente
    if (!target->loadTrack(file)) return false;

    target->setRouting(true, target->isRoutedToCue());
    target->play();

    // Lo que sonaba al aire se despide con un fundido; AutoDJ lo descarga al terminar.
    if (isOnAir(other)) other.startSkip();
    return target->isPlaying();
}

void requestPlayNow(Deck& a, Deck& b, const TrackInfo& track, std::function<bool()> beforeStart) {
    const juce::File file(juce::String(track.path));
    auto start = [&a, &b, file, beforeStart] {
        if (beforeStart && !beforeStart()) return;
        if (!playNow(a, b, file))
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, TRANS("play_now_failed_title"),
                                                   TRANS("play_now_failed_msg") + "\n" + file.getFullPathName());
    };

    if (!isOnAir(a) && !isOnAir(b)) {
        start();
        return;
    }

    const auto name = juce::String(track.artist) + " - " + juce::String(track.title);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("play_now_confirm_title"),
        TRANS("play_now_confirm_msg").replace("%1", name),
        TRANS("play_now_confirm_btn"), TRANS("cancel"), nullptr,
        juce::ModalCallbackFunction::create([start](int answer) {
            if (answer == 1) start();
        }));
}

}
