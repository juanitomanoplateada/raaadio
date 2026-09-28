#pragma once
#include <JuceHeader.h>
#include <map>
#include <set>
#include <vector>

// Nombres de canciones: de lo que trae un archivo (etiquetas, nombre del archivo, la carpeta
// en la que venía) a artista, título y créditos limpios. Es lógica pura, sin base de datos
// ni disco: la usan la importación, la revisión y las pruebas.
namespace TrackNaming {

//==============================================================================
// Texto

// Clave para comparar nombres: minúsculas, sin tildes, "&" como "y", sin puntuación ni
// espacios repetidos. "Rubby Pérez" y "RUBBY PEREZ" tienen la misma clave.
juce::String key(const juce::String& text);

// Mayúsculas y minúsculas que entienden las vocales con tilde y la eñe (String::toUpperCase
// no las convierte en Windows).
juce::String upper(const juce::String& text);
juce::String lower(const juce::String& text);

// Un nombre escrito TODO EN MAYÚSCULAS o todo en minúsculas (así vienen muchos archivos).
bool isShouting(const juce::String& text);
// "Tipo Título": cada palabra con mayúscula inicial salvo artículos, preposiciones y
// conjunciones ("Pochy y su Cocoband", "Los Ocho de Colombia"); siglas y números romanos
// quedan en mayúsculas.
juce::String titleCase(const juce::String& text);
// titleCase solo si el texto viene todo en mayúsculas o todo en minúsculas.
juce::String smartCase(const juce::String& text);
// ours con las tildes de theirs, si son el mismo texto salvo tildes y mayúsculas ("Volvere" y
// "volveré" dan "Volveré"); si no, ours tal cual.
juce::String withAccentsFrom(const juce::String& ours, const juce::String& theirs);

// Lo que se muestra en pantalla y en la transmisión: tal cual, o en mayúsculas si la
// emisora lo prefiere (Ajustes > Biblioteca). Lo guardado nunca cambia.
void setDisplayUppercase(bool uppercase);
bool isDisplayUppercase();
juce::String display(const std::string& text);
juce::String display(const juce::String& text);

//==============================================================================
// Créditos

// Artista para mostrar: "Eddy Herrera feat. Liz".
juce::String creditLine(const juce::String& artist, const juce::StringArray& featured);
// Título para mostrar: "Volveré (En vivo)".
juce::String displayTitle(const juce::String& title, const juce::String& version);
// Clave para reconocer la misma canción: artista principal y título, sin versión.
juce::String matchKey(const juce::String& artist, const juce::String& title);
// Separa "A, B y C" en nombres.
juce::StringArray splitNames(const juce::String& text);
// Dos claves de artista que seguramente son el mismo escrito distinto: sin el artículo
// ("Hermanos Rosario"), sin espacios ("Wilfridovargas") o con una o dos letras de diferencia.
bool similarKeys(const juce::String& a, const juce::String& b);

//==============================================================================
// Lo que viene en el archivo

enum class Orientation { ArtistTitle, TitleArtist };

struct Source {
    juce::String fileName;    // nombre del archivo, sin extensión
    juce::String group;       // carpeta de origen: los archivos de una carpeta siguen el mismo formato
    juce::String tagArtist;   // etiquetas del archivo, si las tiene
    juce::String tagTitle;

    bool hasTags() const { return tagArtist.isNotEmpty() && tagTitle.isNotEmpty(); }
};

// Artista, título y créditos propuestos para una canción.
struct Proposal {
    juce::String artist;          // intérprete principal
    juce::StringArray featured;   // invitados
    juce::StringArray composers;
    juce::String title;
    juce::String version;         // "En vivo", "Remix"...
    int year = 0;

    juce::String genreRemoved;    // texto de género que se quitó del nombre ("MERENGUE")
    bool featuredGuessed = false; // "A y B" se separó por ser A un artista conocido
    juce::String joinedArtist;    // el nombre sin separar, por si el invitado no era tal

    juce::String credit() const { return creditLine(artist, featured); }
    juce::String fullTitle() const { return displayTitle(title, version); }
    juce::String key() const { return matchKey(artist, title); }
};

// Artistas que la emisora ya confirmó (clave → nombre y cuántas canciones tiene).
struct KnownArtist {
    juce::String name;
    int tracks = 0;
};

// Todo lo que ayuda a entender un lote: los artistas conocidos y sus alias, los géneros
// (para quitarlos del nombre) y, de lo que se está revisando, con cuántos títulos distintos
// aparece cada nombre. Un artista se repite con títulos distintos; un título, no.
class Context {
public:
    Context() = default;
    Context(std::map<juce::String, KnownArtist> known, std::map<juce::String, juce::String> aliases,
            const juce::StringArray& categoryNames, const std::vector<Source>& batch);

    const KnownArtist* findKnown(const juce::String& nameKey) const;  // también por alias
    // Qué tanto parece un artista: conocido (+3) y cuántos títulos distintos lo acompañan.
    int artistness(const juce::String& nameKey) const;
    bool isGenre(const juce::String& text) const;
    // El artista conocido más parecido (errores de tipeo, "Hermanos Rosario" por "Los
    // Hermanos Rosario"), o vacío.
    juce::String closestKnown(const juce::String& name) const;
    std::vector<KnownArtist> suggestions(const juce::String& typed, int maxResults) const;

private:
    std::map<juce::String, KnownArtist> known;
    std::map<juce::String, juce::String> aliases;  // clave del alias → clave del artista
    std::set<juce::String> genres;
    std::map<juce::String, std::set<juce::String>> partners;
    std::map<juce::String, juce::String> batchNames;  // clave → cómo se escribe, de lo que se revisa
};

// Formato de una carpeta: cuántos archivos lo confirman y cuántos lo contradicen.
struct GroupFormat {
    Orientation orientation = Orientation::ArtistTitle;
    int confirming = 0;
    int contradicting = 0;
    int files = 0;      // sin contar los que traen etiquetas
    bool verified() const { return confirming > 0 && contradicting * 10 <= confirming; }
};

// Formato de cada carpeta (clave: Source::group) por mayoría.
std::map<juce::String, GroupFormat> detectFormats(const std::vector<Source>& batch, const Context& context);
// Cómo va este archivo según sus propias pistas: positivo a favor de Título – Artista,
// negativo a favor de Artista – Título, 0 si no dice nada. Cuanto más lejos de 0, más seguro.
int orientationEvidence(const Source& source, const Context& context);

// Nombre limpio: sin el sufijo que agregaban las copias repetidas ("_521167018"), el número
// de pista, "(Video Oficial)", direcciones web ni guiones bajos.
juce::String cleanFileName(const juce::String& fileName);

// Propuesta para un archivo: de sus etiquetas si las tiene; si no, de su nombre leído con
// la orientación dada.
Proposal propose(const Source& source, Orientation orientation, const Context& context);

}
