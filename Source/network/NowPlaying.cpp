#include "NowPlaying.h"
#include "StreamEncoder.h"

namespace NowPlaying {

namespace {
    juce::Time wholeSeconds(juce::Time t) {
        return juce::Time((t.toMilliseconds() + 500) / 1000 * 1000);
    }

    Item itemFor(const TrackInfo& track, juce::Time time) {
        Item item;
        item.artist = juce::String(track.artist).trim();
        item.title = juce::String(track.title).trim();
        item.duration = track.getAirDuration();
        item.time = time.toMilliseconds() == 0 ? juce::Time() : wholeSeconds(time);
        return item;
    }

    juce::var timeVar(juce::Time t) {
        return t.toMilliseconds() == 0 ? juce::var() : juce::var(t.toISO8601(true));
    }

    const char* stateId(State state) {
        switch (state) {
            case State::Music: return "music";
            case State::Break: return "break";
            case State::Off:   break;
        }
        return "off";
    }
}

Snapshot build(const Inputs& in) {
    Snapshot s;
    s.station = in.station;
    auto isMusic = [&in](const TrackInfo& track) { return !in.isMusic || in.isMusic(track); };

    std::string currentPath;
    if (in.onAir) {
        const auto& airing = *in.onAir;
        currentPath = airing.track.path;
        if (isMusic(airing.track)) {
            s.state = State::Music;
            s.current = itemFor(airing.track, airing.started);
            if (airing.secondsToNext >= 0.0)
                s.currentEnds = wholeSeconds(in.now + juce::RelativeTime(airing.secondsToNext));
        } else {
            s.state = State::Break;
        }
    }

    // Lo que suena ya está en el historial (se anota al salir al aire): arriba de todo, o
    // segundo si después se disparó un cartucho. Esa entrada no es "anterior".
    bool skippedCurrent = currentPath.empty();
    for (size_t i = 0; i < in.history.size() && (int) s.previous.size() < in.maxPrevious; ++i) {
        const auto& entry = in.history[i];
        if (!skippedCurrent && i < 2 && entry.track.path == currentPath) {
            skippedCurrent = true;
            continue;
        }
        if (isMusic(entry.track)) s.previous.push_back(itemFor(entry.track, entry.timestamp));
    }

    for (const auto& upcoming : in.upcoming) {
        if ((int) s.next.size() >= in.maxNext) break;
        if (!isMusic(upcoming.track)) continue;
        const auto eta = upcoming.secondsFromNow >= 0.0 ? in.now + juce::RelativeTime(upcoming.secondsFromNow) : juce::Time();
        s.next.push_back(itemFor(upcoming.track, eta));
    }
    return s;
}

juce::var toVar(const Snapshot& s, juce::Time generatedAt) {
    auto item = [](const Item& i, const char* timeKey) {
        auto* o = new juce::DynamicObject();
        o->setProperty("artist", i.artist);
        o->setProperty("title", i.title);
        o->setProperty("text", StreamEncoder::songText(i.artist, i.title));
        o->setProperty("duration", juce::roundToInt(i.duration));
        o->setProperty(timeKey, timeVar(i.time));
        return o;
    };
    auto list = [&item](const std::vector<Item>& items, const char* timeKey) {
        juce::Array<juce::var> array;
        for (const auto& i : items) array.add(juce::var(item(i, timeKey)));
        return juce::var(array);
    };

    auto* root = new juce::DynamicObject();
    root->setProperty("station", s.station);
    root->setProperty("state", stateId(s.state));
    root->setProperty("updated", timeVar(wholeSeconds(generatedAt)));
    if (s.state == State::Music) {
        auto* now = item(s.current, "started");
        now->setProperty("ends", timeVar(s.currentEnds));
        root->setProperty("now", juce::var(now));
    } else {
        root->setProperty("now", juce::var());
    }
    root->setProperty("previous", list(s.previous, "played"));
    root->setProperty("next", list(s.next, "eta"));
    return juce::var(root);
}

juce::String toJson(const Snapshot& s, juce::Time generatedAt) {
    return juce::JSON::toString(toVar(s, generatedAt));
}

}
