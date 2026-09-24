#include "PlaylistStore.h"
#include "MusicLibrary.h"

PlaylistStore::PlaylistStore(Playlist& p, MusicLibrary& lib, const juce::File& f)
    : playlist(p), library(lib), file(f)
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
    if (!file.existsAsFile()) return 0;

    const auto json = juce::JSON::parse(file);
    const auto* items = json["tracks"].getArray();
    if (items == nullptr) return 0;

    std::vector<TrackInfo> tracks;
    for (const auto& item : *items)
    {
        // El id es lo fiable; la ruta solo sirve si la pista ya no tiene ese id.
        TrackInfo track = library.getTrackById((int) item["id"]);
        if (track.id == -1 && item["path"].toString().isNotEmpty())
            track = library.getTrackByPath(item["path"].toString());

        if (track.id != -1) {
            track.breakKey = item["break"].toString().toStdString();  // pieza de una tanda comercial
            tracks.push_back(track);
        }
    }

    if (!tracks.empty())
        playlist.insertTracks(tracks, std::numeric_limits<int>::max());

    // Lo recién restaurado ya está en disco.
    savedVersion = playlist.getLastModified();

    juce::Logger::writeToLog("PlaylistStore: restored " + juce::String((int) tracks.size()) + " of "
                             + juce::String(items->size()) + " queued tracks");
    return (int) tracks.size();
}

void PlaylistStore::saveIfChanged()
{
    const auto version = playlist.getLastModified();
    if (version == savedVersion) return;

    juce::Array<juce::var> items;
    for (const auto& t : playlist.getQueue())
    {
        auto* item = new juce::DynamicObject();
        item->setProperty("id", t.id);
        item->setProperty("path", juce::String(t.path));
        if (!t.breakKey.empty()) item->setProperty("break", juce::String(t.breakKey));
        items.add(juce::var(item));
    }

    auto* root = new juce::DynamicObject();
    root->setProperty("version", 1);
    root->setProperty("savedAt", juce::Time::getCurrentTime().toISO8601(true));
    root->setProperty("tracks", items);

    file.getParentDirectory().createDirectory();
    if (file.replaceWithText(juce::JSON::toString(juce::var(root))))
        savedVersion = version;
    else
        juce::Logger::writeToLog("PlaylistStore: could not write " + file.getFullPathName());
}

void PlaylistStore::timerCallback()
{
    saveIfChanged();
}
