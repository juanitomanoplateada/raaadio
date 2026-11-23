#include "QueueTiming.h"
#include "../audio/Deck.h"

namespace QueueTiming {

double secondsUntilNext(const TrackInfo& track) {
    const double end = track.cueIn + track.getAirDuration();
    return Deck::mixPointFor(track, track.cueIn, end) - track.cueIn;
}

double secondsUntilNext(const Deck& deck) {
    if (deck.isFadingOut()) return 0.0;  // la siguiente ya entró, o entra ahora
    const double speed = deck.getSpeed() > 0.01 ? deck.getSpeed() : 1.0;
    return juce::jmax(0.0, deck.getMixPoint() - deck.getCurrentPosition()) / speed;
}

double secondsUntilQueueStarts(const Deck& deckA, const Deck& deckB) {
    if (!deckA.isPlaying() && !deckB.isPlaying()) return -1.0;

    // Durante una mezcla suenan los dos: manda el que termina más tarde.
    double seconds = juce::jmax(deckA.isPlaying() ? secondsUntilNext(deckA) : 0.0,
                                deckB.isPlaying() ? secondsUntilNext(deckB) : 0.0);

    for (const Deck* deck : { &deckA, &deckB })
        if (deck->isLoaded() && !deck->isPlaying())
            seconds += secondsUntilNext(*deck);

    return seconds;
}

std::vector<double> startOffsets(const std::vector<TrackInfo>& queue) {
    std::vector<double> offsets;
    offsets.reserve(queue.size());
    double offset = 0.0;
    for (const auto& track : queue) {
        offsets.push_back(offset);
        offset += secondsUntilNext(track);
    }
    return offsets;
}

int findInsertionIndex(const std::vector<TrackInfo>& queue, juce::Time queueStart, juce::Time target) {
    auto distance = [&](double offset) {
        return std::abs((queueStart + juce::RelativeTime(offset) - target).inSeconds());
    };

    const auto offsets = startOffsets(queue);
    int best = 0;
    double bestDistance = distance(0.0);
    for (size_t i = 0; i < queue.size(); ++i) {
        const double afterTrack = offsets[i] + secondsUntilNext(queue[i]);  // insertar en i + 1
        const double d = distance(afterTrack);
        if (d < bestDistance) {
            bestDistance = d;
            best = (int) i + 1;
        }
    }
    return best;
}

}
