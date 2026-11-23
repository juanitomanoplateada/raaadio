#pragma once

#include <JuceHeader.h>
#include "../database/Database.h"

class MusicLibrary;

struct HistoryEntry {
    juce::Time timestamp;
    TrackInfo track;
    juce::String deckName;
    juce::String source;
};

// Historial de lo que salió al aire. Cada entrada se guarda en la tabla play_log de
// la base de datos (sobrevive a reinicios y sirve de registro de emisión); en memoria
// solo se mantienen las más recientes para la vista.
class HistoryManager : public juce::ChangeBroadcaster {
public:
    HistoryManager();
    ~HistoryManager() override;

    // Conecta el historial a la base de datos y carga las entradas más recientes.
    void initialise(MusicLibrary& library);

    void addEntry(const TrackInfo& track, const juce::String& deckName);
    std::vector<HistoryEntry> getHistory() const;

    // Solo vacía la vista en memoria: el registro guardado no se borra.
    void clearHistory();
    void setMaxEntries(int max);

private:
    MusicLibrary* library = nullptr;
    std::deque<HistoryEntry> history;
    int maxEntries = 500;

    juce::CriticalSection historyLock;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryManager)
};
