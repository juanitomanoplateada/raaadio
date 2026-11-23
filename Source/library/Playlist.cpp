#include "Playlist.h"

Playlist::Playlist() {
    juce::Logger::writeToLog("Playlist::Playlist ctor");
}

void Playlist::addTrack(const TrackInfo& track) {
    std::lock_guard<std::mutex> lock(mutex);
    queue.push_back(track);
    lastModified++;
}

TrackInfo Playlist::popNext() {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty()) return {};
    TrackInfo t = queue.front();
    queue.pop_front();
    if (!t.breakKey.empty()) {
        startedBreaks.insert(t.breakKey);
        // Las claves son fechas ("AAAA-MM-DD HH:MM") y se ordenan solas: basta con las recientes.
        while (startedBreaks.size() > 200) startedBreaks.erase(startedBreaks.begin());
    }
    lastModified++;
    return t;
}

bool Playlist::isEmpty() const {
    std::lock_guard<std::mutex> lock(mutex);
    return queue.empty();
}

int Playlist::findIndexForId(int id) const {
    std::lock_guard<std::mutex> lock(mutex);
    for (int i=0; i<queue.size(); ++i) {
        if (queue[i].id == id) return i;
    }
    return -1;
}

void Playlist::insertTracks(const std::vector<TrackInfo>& tracks, int index) {
    if (tracks.empty()) return;
    std::lock_guard<std::mutex> lock(mutex);
    
    if (index < 0) index = 0;
    if (index > queue.size()) index = (int)queue.size();
    
    auto it = queue.begin();
    std::advance(it, index);
    queue.insert(it, tracks.begin(), tracks.end());
    lastModified++;
}

std::vector<int> Playlist::findIndicesForIds(const std::vector<int>& ids) const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<int> result;
    for (int id : ids) {
        for (int i=0; i<queue.size(); ++i) {
            if (queue[i].id == id) {
                result.push_back(i);
                break;
            }
        }
    }
    return result;
}

void Playlist::clear() {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty()) return;
    for (const auto& t : queue) {
        if (!t.breakKey.empty() && startedBreaks.count(t.breakKey) == 0
            && std::find(droppedBreaks.begin(), droppedBreaks.end(), t.breakKey) == droppedBreaks.end())
            droppedBreaks.push_back(t.breakKey);
    }
    queue.clear();
    lastModified++;
}

std::vector<std::string> Playlist::takeDroppedBreaks() {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<std::string> dropped;
    dropped.swap(droppedBreaks);
    return dropped;
}

void Playlist::removeTrack(int index) {
    std::lock_guard<std::mutex> lock(mutex);
    if (index < 0 || index >= queue.size()) return;
    auto it = queue.begin();
    std::advance(it, index);
    queue.erase(it);
    lastModified++;
}

bool Playlist::moveTracks(const std::vector<RowRef>& rows, int insertIndex) {
    std::lock_guard<std::mutex> lock(mutex);
    if (rows.empty()) return false;

    for (const auto& r : rows) {
        if (r.index < 0 || r.index >= (int)queue.size() || queue[(size_t)r.index].path != r.path)
            return false;
    }

    std::vector<int> indices;
    indices.reserve(rows.size());
    for (const auto& r : rows) indices.push_back(r.index);
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    std::vector<TrackInfo> moving;
    moving.reserve(indices.size());
    for (int i : indices) moving.push_back(queue[(size_t)i]);

    // Al quitar las filas, las que estaban antes del destino corren la posición.
    int target = juce::jlimit(0, (int)queue.size(), insertIndex);
    for (int i : indices) if (i < insertIndex) --target;

    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        queue.erase(queue.begin() + *it);

    target = juce::jlimit(0, (int)queue.size(), target);
    queue.insert(queue.begin() + target, moving.begin(), moving.end());
    lastModified++;
    return true;
}

int Playlist::removeTracksIfMatching(const std::vector<RowRef>& rows) {
    std::lock_guard<std::mutex> lock(mutex);

    std::vector<int> indices;
    for (const auto& r : rows) {
        if (r.index >= 0 && r.index < (int)queue.size() && queue[(size_t)r.index].path == r.path)
            indices.push_back(r.index);
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

    for (auto it = indices.rbegin(); it != indices.rend(); ++it)
        queue.erase(queue.begin() + *it);

    if (!indices.empty()) lastModified++;
    return (int)indices.size();
}

std::vector<TrackInfo> Playlist::getQueue() const {
    std::lock_guard<std::mutex> lock(mutex);
    return {queue.begin(), queue.end()};
}

uint64_t Playlist::getLastModified() const {
    std::lock_guard<std::mutex> lock(mutex);
    return lastModified;
}

int Playlist::updatePath(int id, const std::string& newPath) {
    std::lock_guard<std::mutex> lock(mutex);
    int changed = 0;
    for (auto& track : queue) {
        if (track.id == id && track.path != newPath) {
            track.path = newPath;
            ++changed;
        }
    }
    if (changed > 0) ++lastModified;
    return changed;
}
