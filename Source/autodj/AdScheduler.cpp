#include "AdScheduler.h"
#include "QueueTiming.h"
#include "../library/MusicLibrary.h"
#include "../library/AdvertisingManager.h"
#include "../library/Playlist.h"

namespace AdBreaks {

juce::Time timeOfSlot(const juce::String& slot, juce::Time day) {
    return juce::Time(day.getYear(), day.getMonth(), day.getDayOfMonth(),
                      slot.upToFirstOccurrenceOf(":", false, false).getIntValue(),
                      slot.fromFirstOccurrenceOf(":", false, false).getIntValue(), 0, 0, true);
}

std::vector<TrackInfo> buildBreak(MusicLibrary& library, const AdvertisingManager& ads,
                                  const juce::String& slot, juce::Random& random) {
    std::vector<TrackInfo> block;
    const auto numbers = ads.getNumbersForTime(slot);
    if (numbers.empty()) return block;

    const auto ids = library.getTracksWithRole(CategoryRole::StationIds);
    if (!ids.empty())
        block.push_back(ids[(size_t) random.nextInt((int) ids.size())]);

    for (int number : numbers) {
        for (int trackId : ads.getTracksWithNumber(number)) {
            const auto track = library.getTrackById(trackId);
            if (track.id != -1) block.push_back(track);
        }
    }
    return block;
}

}

//==============================================================================
AdScheduler::AdScheduler(MusicLibrary& lib, AdvertisingManager& adsManager, Playlist& pl,
                         Deck& a, Deck& b, juce::File file)
    : library(lib), ads(adsManager), playlist(pl), deckA(a), deckB(b), stateFile(std::move(file))
{
    loadState();
    startTimer(15000);
}

AdScheduler::~AdScheduler() {
    stopTimer();
}

juce::String AdScheduler::keyFor(juce::Time breakTime) {
    return breakTime.formatted("%Y-%m-%d %H:%M");
}

std::vector<juce::Time> AdScheduler::dueBreaks(const juce::StringArray& slots, juce::Time now,
                                               const std::set<juce::String>& done) {
    std::vector<juce::Time> due;
    const auto from = now - juce::RelativeTime::minutes(lateMinutes);
    const auto to = now + juce::RelativeTime::minutes(lookaheadMinutes);

    // Ayer, hoy y mañana: las ventanas cruzan la medianoche.
    for (int dayOffset = -1; dayOffset <= 1; ++dayOffset) {
        const auto day = now + juce::RelativeTime::days(dayOffset);
        for (const auto& slot : slots) {
            const auto t = AdBreaks::timeOfSlot(slot, day);
            if (t >= from && t <= to && done.count(keyFor(t)) == 0)
                due.push_back(t);
        }
    }
    std::sort(due.begin(), due.end());
    return due;
}

static juce::Time timeOfKey(const juce::String& key) {
    return juce::Time(key.substring(0, 4).getIntValue(), key.substring(5, 7).getIntValue() - 1,
                      key.substring(8, 10).getIntValue(), key.substring(11, 13).getIntValue(),
                      key.substring(14, 16).getIntValue(), 0, 0, true);
}

void AdScheduler::checkNow(juce::Time now) {
    // Tandas que estaban en la cola y se perdieron sin sonar al vaciarla o reemplazarla: se
    // olvidan para que vuelvan a insertarse, en su punto, en la cola nueva.
    bool changed = false;
    for (const auto& dropped : playlist.takeDroppedBreaks()) {
        const juce::String key(dropped);
        if (timeOfKey(key) < now - juce::RelativeTime::minutes(lateMinutes)) {
            juce::Logger::writeToLog("AdScheduler: break for " + key + " was removed from the queue before airing"
                                     " and it is too late to reinsert it");
        } else if (done.erase(key) > 0) {
            changed = true;
            juce::Logger::writeToLog("AdScheduler: break for " + key + " was removed from the queue before airing,"
                                     " it will be inserted again");
        }
    }

    if (!ads.isAutoScheduleEnabled()) {
        if (changed) saveState(now);
        return;
    }

    for (const auto& breakTime : dueBreaks(ads.getScheduledSlots(), now, done)) {
        const double untilQueue = QueueTiming::secondsUntilQueueStarts(deckA, deckB);
        const auto queueStart = now + juce::RelativeTime(juce::jmax(0.0, untilQueue));
        const auto queue = playlist.getQueue();

        // Si la cola todavía no llega a esa hora, se espera a que crezca; faltando menos
        // de dos minutos, la tanda va igual (al final de la cola).
        double queueSeconds = 0.0;
        for (const auto& t : queue) queueSeconds += QueueTiming::secondsUntilNext(t);
        const bool imminent = breakTime - now < juce::RelativeTime::minutes(2);
        if (queueStart + juce::RelativeTime(queueSeconds) < breakTime && !imminent) continue;

        done.insert(keyFor(breakTime));
        changed = true;

        auto block = AdBreaks::buildBreak(library, ads, breakTime.formatted("%H:%M"), random);
        if (block.empty()) continue;
        for (auto& t : block) t.breakKey = keyFor(breakTime).toStdString();  // para saber si se pierde

        const int index = QueueTiming::findInsertionIndex(queue, queueStart, breakTime);
        playlist.insertTracks(block, index);
        juce::Logger::writeToLog("AdScheduler: break for " + keyFor(breakTime) + " (" + juce::String((int) block.size())
                                 + " items) inserted at queue position " + juce::String(index));
    }

    if (changed) saveState(now);
}

void AdScheduler::loadState() {
    const auto json = juce::JSON::parse(stateFile);
    if (auto* keys = json.getArray())
        for (const auto& key : *keys)
            done.insert(key.toString());
}

void AdScheduler::saveState(juce::Time now) {
    // Solo hace falta recordar los últimos dos días.
    const auto oldest = keyFor(now - juce::RelativeTime::days(2));
    juce::Array<juce::var> keys;
    for (auto it = done.begin(); it != done.end();) {
        if (*it < oldest) {
            it = done.erase(it);
        } else {
            keys.add(*it);
            ++it;
        }
    }
    if (stateFile != juce::File()) {
        stateFile.getParentDirectory().createDirectory();
        stateFile.replaceWithText(juce::JSON::toString(juce::var(keys)));
    }
}
