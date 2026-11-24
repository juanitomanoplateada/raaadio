#include "TemporaryAudio.h"
#include "MusicLibrary.h"

namespace TemporaryAudio {

Busy whyBusy(int trackId, const Usage& usage) {
    if (usage.onDecks.count(trackId) > 0) return Busy::OnDeck;
    if (usage.playingCarts.count(trackId) > 0) return Busy::InCart;
    if (usage.queued.count(trackId) > 0) return Busy::Queued;
    return Busy::No;
}

EmptyResult emptyAll(MusicLibrary& library, const Usage& usage) {
    EmptyResult result;
    for (const auto& track : library.getTemporaries()) {
        if (whyBusy(track.id, usage) != Busy::No) ++result.busy;
        else if (library.deleteTemporary(track.id)) ++result.deleted;
        else ++result.failed;
    }
    return result;
}

}
