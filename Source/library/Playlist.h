#pragma once
#include <deque>
#include <mutex>
#include <set>
#include <JuceHeader.h>
#include "../database/Database.h"

class Playlist {
public:
    Playlist();
    
    void addTrack(const TrackInfo& track);
    TrackInfo popNext();
    bool isEmpty() const;
    void insertTracks(const std::vector<TrackInfo>& tracks, int index);
    void removeTrack(int index);
    void clear();

    // Referencia a una fila tal como se ve en la tabla (al arrastrar, o con los botones de
    // subir y bajar): posición y ruta del archivo. Las operaciones siguientes solo actúan
    // si la fila todavía contiene esa pista, porque AutoDJ puede sacar pistas de la cola
    // mientras el operador la mira.
    struct RowRef { int index; std::string path; };

    // Mueve las filas a la posición insertIndex (índice sobre la cola original).
    bool moveTracks(const std::vector<RowRef>& rows, int insertIndex);
    // Quita las filas que sigan coincidiendo; devuelve cuántas quitó.
    int removeTracksIfMatching(const std::vector<RowRef>& rows);
    std::vector<int> findIndicesForIds(const std::vector<int>& ids) const;
    int findIndexForId(int id) const;
    // El archivo de una pista cambió de lugar (se confirmó en la revisión): las filas de la
    // cola que la tienen apuntan al nuevo. Devuelve cuántas cambió.
    int updatePath(int id, const std::string& newPath);
    
    std::vector<TrackInfo> getQueue() const;
    uint64_t getLastModified() const;

    // Tandas comerciales (TrackInfo::breakKey) que salieron de la cola sin llegar a sonar
    // al vaciarla o reemplazarla (clear), para que la pauta las vuelva a insertar. Una tanda
    // cuenta como emitida cuando alguna de sus piezas sale por popNext(). Quitar filas una
    // a una es decisión del operador y no se reporta.
    std::vector<std::string> takeDroppedBreaks();

private:
    std::deque<TrackInfo> queue;
    mutable std::mutex mutex;
    uint64_t lastModified = 0;
    std::set<std::string> startedBreaks;
    std::vector<std::string> droppedBreaks;
};
