#include "HistoryManager.h"
#include "MusicLibrary.h"

static constexpr const char* timestampFormat = "%Y-%m-%d %H:%M:%S";

HistoryManager::HistoryManager() {}

HistoryManager::~HistoryManager() {}

void HistoryManager::initialise(MusicLibrary& lib) {
    const juce::ScopedLock sl(historyLock);
    library = &lib;
    history.clear();

    for (const auto& row : library->getRecentPlayLog(maxEntries)) {
        HistoryEntry entry {};
        entry.timestamp = parseLocalTimestamp(row.playedAt);
        entry.track.id = row.trackId;
        entry.track.artist = row.artist;
        entry.track.title = row.title;
        entry.track.duration = row.duration;
        entry.deckName = juce::String::fromUTF8(row.deck.c_str());
        history.push_back(entry);
    }

    sendChangeMessage();
}

void HistoryManager::addEntry(const TrackInfo& track, const juce::String& deckName) {
    const juce::ScopedLock sl(historyLock);

    HistoryEntry entry {};
    entry.timestamp = juce::Time::getCurrentTime();
    entry.track = track;
    entry.deckName = deckName;

    history.push_front(entry);
    if ((int) history.size() > maxEntries)
        history.pop_back();

    if (library != nullptr) {
        PlayLogEntry row;
        row.playedAt = entry.timestamp.formatted(timestampFormat).toStdString();
        row.trackId = track.id;
        row.artist = track.artist;
        row.title = track.title;
        row.duration = track.duration;
        row.deck = deckName.toStdString();
        if (!library->addPlayLogEntry(row))
            juce::Logger::writeToLog("HistoryManager: could not write play_log entry for " + juce::String(track.title));
    }

    sendChangeMessage();
}

std::vector<HistoryEntry> HistoryManager::getHistory() const {
    const juce::ScopedLock sl(historyLock);
    return { history.begin(), history.end() };
}

void HistoryManager::clearHistory() {
    const juce::ScopedLock sl(historyLock);
    history.clear();
    sendChangeMessage();
}

void HistoryManager::setMaxEntries(int max) {
    const juce::ScopedLock sl(historyLock);
    maxEntries = max;
}
