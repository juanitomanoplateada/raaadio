#pragma once
#include <JuceHeader.h>
#include "MusicLibrary.h"
#include "TrackNaming.h"

// La revisión de canciones: lo importado por revisar o la música de la biblioteca que nadie
// ha confirmado (de versiones anteriores). Propone artista, título y créditos viendo cada
// carpeta completa, marca lo dudoso y los duplicados, guarda lo que el operador corrige
// (como borrador en la base: sobrevive aunque cierre la ventana) y confirma o descarta.
// No tiene interfaz: la usa ImportReviewView, y se prueba sola.
class ImportReview {
public:
    enum class Scope { NewImports, Library };
    enum class State { Ready, Check, Duplicate };
    enum class Issue {
        FormatUnverified,  // la carpeta no dice su formato: hay que mirar unos ejemplos
        Opposite,          // este archivo parece al revés que el resto de su carpeta
        FeaturedGuess,     // "A y B": ¿B es un invitado o parte del nombre?
        DidYouMean,        // se parece a un artista que ya está en la biblioteca
        NoArtist,          // el nombre no trae artista
        Duplicate,         // la misma canción ya está
        OnlineDiffers,     // MusicBrainz dice otra cosa (artista o título)
    };

    struct Item {
        TrackInfo track;
        Database::ImportSource source;
        juce::String groupKey, groupName;
        TrackNaming::Proposal proposal;   // lo que se confirmaría
        juce::String suggestedArtist;     // lo que propuso el motor (para aprender alias)
        juce::String suggestedTitle;
        std::vector<int> categories;
        std::vector<Issue> issues;
        juce::String didYouMean;          // con Issue::DidYouMean
        int duplicateOf = -1;             // con Issue::Duplicate
        juce::String duplicateText;       // "Artista - Título · 3:58"
        bool duplicateBySound = false;    // la huella acústica dice que es la misma grabación
        std::vector<uint32_t> fingerprint;
        Database::OnlineMatch online;       // lo encontrado en línea (status None si no se buscó)
        TrackNaming::Proposal onlineProposal;  // cómo quedaría con esos datos
        bool onlineAgrees = false;          // coincide con lo propuesto
        State state = State::Check;
        bool manual = false;              // el operador lo editó
        bool keepDuplicate = false;
        bool formatChecked = false;       // el operador eligió el formato de la carpeta
        int orientation = 0;              // elegida para esta fila: 1 Título–Artista, -1 Artista–Título, 0 la de la carpeta
        TrackNaming::Orientation effective = TrackNaming::Orientation::ArtistTitle;

        bool has(Issue issue) const { return std::find(issues.begin(), issues.end(), issue) != issues.end(); }
    };

    struct Group {
        juce::String key, name;
        TrackNaming::GroupFormat format;
        bool checkedByUser = false;
        int items = 0;
        bool needsLook() const { return !checkedByUser && !format.verified() && format.files > 0; }
    };

    explicit ImportReview(MusicLibrary& library);

    void load(Scope scope);
    void reload() { load(scope); }
    Scope getScope() const { return scope; }
    const std::vector<Item>& getItems() const { return items; }
    const std::vector<Group>& getGroups() const { return groups; }
    const Group* findGroup(const juce::String& key) const;
    int count(State state) const;
    std::vector<int> readyRows() const;
    const TrackNaming::Context& getContext() const { return context; }

    // Formato de una carpeta elegido por el operador: vale para sus filas sin editar.
    void setGroupOrientation(const juce::String& groupKey, TrackNaming::Orientation orientation);
    void swap(const std::vector<int>& rows);  // artista ↔ título
    enum class CaseStyle { Title, Upper };
    void applyCase(const std::vector<int>& rows, CaseStyle style);
    void edit(int row, const TrackNaming::Proposal& values);
    void acceptDidYouMean(int row);
    void resolveFeatured(int row, bool isFeatured);  // false: el nombre iba junto ("Sandy y Papo")
    void keepBoth(int row);                          // no es un duplicado
    void setCategory(const std::vector<int>& rows, int categoryId, bool add);

    // Sugerencias en línea (AcoustID y MusicBrainz).
    static constexpr double onlineMinimumScore = 0.7;  // menos, no se muestra
    static constexpr double onlineStrongScore = 0.9;   // "usar las seguras" en lote
    bool updateOnline();  // relee lo encontrado; true si cambió algo
    void acceptOnline(const std::vector<int>& rows);  // usar los datos en línea
    void rejectOnline(int row);                        // mantener lo propuesto
    std::vector<int> onlineRows(bool onlyStrong) const; // filas con una sugerencia distinta

    struct Outcome { int confirmed = 0, failed = 0, deferred = 0; };
    // Confirma las filas (las que son duplicados sin resolver se saltan). progress(0..1)
    // devuelve false para detenerse.
    Outcome confirm(std::vector<int> rows, const std::function<bool(double)>& progress = {});
    // Lo mismo por partes, para que la interfaz siga respondiendo: beginConfirm con todas las
    // filas, confirmRow por cada una y endConfirm, que recarga la revisión.
    void beginConfirm(const std::vector<int>& rows);
    void confirmRow(int row, Outcome& outcome);
    void endConfirm() { reload(); }
    // Saca las filas de la biblioteca (a _Descartadas); una copia suma sus categorías a la otra.
    int discard(std::vector<int> rows);

    juce::File destinationFor(int row) const;

private:
    MusicLibrary& library;
    Scope scope = Scope::NewImports;
    std::vector<Item> items;
    std::vector<Group> groups;
    TrackNaming::Context context;
    std::vector<Database::KeyRow> libraryKeys;  // toda la música, para los duplicados
    // Lo que suena igual que otra canción (por huella acústica): id de pista → (id, texto).
    // No depende de los nombres: se calcula una vez al cargar.
    std::map<int, std::pair<int, juce::String>> soundDuplicates;
    void findSoundDuplicates();

    void rebuild();
    void propose(Item& item);
    void evaluate(Item& item);
    void findDuplicates();
    void saveDraft(const Item& item);
    bool validRow(int row) const { return row >= 0 && row < (int) items.size(); }
};
