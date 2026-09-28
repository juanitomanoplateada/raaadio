#include "PlaylistStore.h"
#include "MusicLibrary.h"

PlaylistStore::PlaylistStore(Playlist& p, MusicLibrary& lib)
    : playlist(p), library(lib)
{
    savedVersion = playlist.getLastModified();
    startTimer(1000);
}

PlaylistStore::~PlaylistStore()
{
    stopTimer();
    saveIfChanged();
}

int PlaylistStore::restore()
{
    const auto items = library.withDatabase([](Database& db) { return db.loadQueue(); });

    // Las filas de pistas borradas ya no están (la base las quita con la pista).
    std::vector<TrackInfo> tracks;
    for (const auto& item : items)
    {
        TrackInfo track = library.getTrackById(item.trackId);
        if (track.id != -1) {
            track.breakKey = item.breakKey;  // pieza de una tanda comercial
            tracks.push_back(track);
        }
    }

    if (!tracks.empty())
        playlist.insertTracks(tracks, std::numeric_limits<int>::max());

    // Lo recién restaurado ya está guardado.
    savedVersion = playlist.getLastModified();

    juce::Logger::writeToLog("PlaylistStore: restored " + juce::String((int) tracks.size()) + " of "
                             + juce::String((int) items.size()) + " queued tracks");
    return (int) tracks.size();
}

void PlaylistStore::saveIfChanged()
{
    const auto version = playlist.getLastModified();
    if (version == savedVersion) return;

    std::vector<Database::QueueItem> items;
    for (const auto& t : playlist.getQueue())
        if (t.id != -1) items.push_back({ t.id, t.breakKey });

    if (library.withDatabase([&items](Database& db) { return db.saveQueue(items); }))
        savedVersion = version;
    else
        juce::Logger::writeToLog("PlaylistStore: could not save the queue");
}

void PlaylistStore::timerCallback()
{
    saveIfChanged();
}
