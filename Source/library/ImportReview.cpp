#include "ImportReview.h"
#include "../helpers/PersistenceManager.h"
#include "../audio/AcousticFingerprint.h"
#include "../network/OnlineMetadata.h"

namespace {
    juce::String str(const std::string& s) { return juce::String::fromUTF8(s.c_str()); }

    juce::var toVar(const juce::StringArray& list) {
        juce::Array<juce::var> values;
        for (const auto& s : list) values.add(s);
        return values;
    }

    juce::StringArray fromVar(const juce::var& v) {
        juce::StringArray list;
        if (const auto* array = v.getArray())
            for (const auto& s : *array) list.add(s.toString());
        return list;
    }

    juce::String formatDuration(double seconds) {
        const int total = juce::roundToInt(seconds);
        return juce::String(total / 60) + ":" + juce::String(total % 60).paddedLeft('0', 2);
    }

    TrackNaming::Orientation opposite(TrackNaming::Orientation o) {
        return o == TrackNaming::Orientation::ArtistTitle ? TrackNaming::Orientation::TitleArtist
                                                          : TrackNaming::Orientation::ArtistTitle;
    }

    int toOverride(TrackNaming::Orientation o) { return o == TrackNaming::Orientation::TitleArtist ? 1 : -1; }
}

ImportReview::ImportReview(MusicLibrary& lib) : library(lib) {}

const ImportReview::Group* ImportReview::findGroup(const juce::String& key) const {
    for (const auto& g : groups)
        if (g.key == key) return &g;
    return nullptr;
}

int ImportReview::count(State state) const {
    int n = 0;
    for (const auto& item : items)
        if (item.state == state) ++n;
    return n;
}

std::vector<int> ImportReview::readyRows() const {
    std::vector<int> rows;
    for (int i = 0; i < (int) items.size(); ++i)
        if (items[(size_t) i].state == State::Ready) rows.push_back(i);
    return rows;
}

//==============================================================================

void ImportReview::load(Scope newScope) {
    scope = newScope;
    items.clear();

    const auto records = library.getReviewRecords(scope == Scope::NewImports);
    const auto onlineMatches = library.withDatabase([](Database& db) { return db.getOnlineMatches(); });
    const auto categoriesOf = library.getAllTrackCategories();
    const auto libraryFolder = PersistenceManager::getLibraryFolder();

    for (const auto& record : records) {
        Item item;
        item.track = record.track;
        item.source = record.source;

        // Música de versiones anteriores: su nombre se rehace con lo que se guardó (así se
        // guardaron los archivos) y la carpeta es la de la categoría con la que se importó.
        if (item.source.name.empty()) {
            const auto artist = str(record.track.artist), title = str(record.track.title);
            item.source.name = (artist.isEmpty() || artist == "UNKNOWN" ? title : artist + " - " + title).toStdString();
        }
        if (item.source.folder.empty()) {
            const juce::File file(str(record.track.path));
            juce::File folder = file.getParentDirectory();
            if (file.isAChildOf(libraryFolder)) {
                const auto first = file.getRelativePathFrom(libraryFolder).replaceCharacter('\\', '/').upToFirstOccurrenceOf("/", false, false);
                folder = libraryFolder.getChildFile(first);
            }
            item.source.folder = folder.getFullPathName().toStdString();
        }
        item.groupKey = str(item.source.folder);
        item.groupName = juce::File::isAbsolutePath(item.groupKey) ? juce::File(item.groupKey).getFileName() : item.groupKey;

        const auto it = categoriesOf.find(record.track.id);
        if (it != categoriesOf.end()) item.categories = it->second;
        item.fingerprint = record.fingerprint;
        if (const auto found = onlineMatches.find(record.track.id); found != onlineMatches.end()) item.online = found->second;

        // Lo que el operador ya había hecho con esta fila.
        const auto draft = juce::JSON::parse(str(record.draft));
        if (draft.isObject()) {
            item.orientation = (int) draft.getProperty("orientation", 0);
            item.formatChecked = (bool) draft.getProperty("formatChecked", false);
            item.keepDuplicate = (bool) draft.getProperty("keepDuplicate", false);
            item.manual = (bool) draft.getProperty("manual", false);
            if (item.manual) {
                auto& p = item.proposal;
                p.artist = draft["artist"].toString();
                p.featured = fromVar(draft["featured"]);
                p.composers = fromVar(draft["composers"]);
                p.title = draft["title"].toString();
                p.version = draft["version"].toString();
                p.year = (int) draft.getProperty("year", 0);
                item.suggestedArtist = draft["suggestedArtist"].toString();
                item.suggestedTitle = draft["suggestedTitle"].toString();
            }
        }
        items.push_back(std::move(item));
    }

    libraryKeys = library.withDatabase([](Database& db) { return db.getMusicKeys(); });
    findSoundDuplicates();
    rebuild();
}

void ImportReview::findSoundDuplicates() {
    soundDuplicates.clear();
    // En otro formato la misma grabación dura casi lo mismo (unos milisegundos de relleno).
    constexpr double tolerance = 3.0;
    std::set<int> reviewed;
    for (const auto& item : items) reviewed.insert(item.track.id);

    // Entre las filas: ordenadas por duración, solo se comparan las que duran parecido. La
    // copia es la que llegó después.
    std::vector<const Item*> withPrint;
    for (const auto& item : items)
        if (!item.fingerprint.empty()) withPrint.push_back(&item);
    std::sort(withPrint.begin(), withPrint.end(), [](const Item* a, const Item* b) { return a->track.duration < b->track.duration; });
    if (withPrint.size() <= 5000) {
        for (size_t i = 0; i < withPrint.size(); ++i) {
            for (size_t j = i + 1; j < withPrint.size() && withPrint[j]->track.duration - withPrint[i]->track.duration <= tolerance; ++j) {
                const auto* first = withPrint[i]->track.id < withPrint[j]->track.id ? withPrint[i] : withPrint[j];
                const auto* copy = first == withPrint[i] ? withPrint[j] : withPrint[i];
                if (soundDuplicates.count(copy->track.id) > 0) continue;
                if (AcousticFingerprint::similarity(first->fingerprint, copy->fingerprint) < AcousticFingerprint::sameRecording) continue;
                soundDuplicates[copy->track.id] = { first->track.id, str(first->track.artist) + " - " + str(first->track.title)
                                                                         + "  ·  " + formatDuration(first->track.duration) + "  ·  " + first->groupName };
            }
        }
    }

    // Contra la música que no se está revisando (una consulta por fila: solo cuando son pocas).
    if (items.size() > 1000) return;
    for (const auto& item : items) {
        if (item.fingerprint.empty() || soundDuplicates.count(item.track.id) > 0) continue;
        const auto candidates = library.withDatabase([&](Database& db) { return db.getFingerprintsNear(item.track.duration, tolerance); });
        double best = AcousticFingerprint::sameRecording;
        for (const auto& c : candidates) {
            if (reviewed.count(c.id) > 0) continue;
            const double s = AcousticFingerprint::similarity(item.fingerprint, c.fingerprint);
            if (s < best) continue;
            best = s;
            soundDuplicates[item.track.id] = { c.id, str(c.artist) + " - " + str(c.title) + "  ·  " + formatDuration(c.duration) };
        }
    }
}

void ImportReview::rebuild() {
    std::vector<TrackNaming::Source> sources;
    sources.reserve(items.size());
    for (const auto& item : items) {
        TrackNaming::Source s;
        s.fileName = str(item.source.name);
        s.group = item.groupKey;
        s.tagArtist = str(item.source.tagArtist);
        s.tagTitle = str(item.source.tagTitle);
        sources.push_back(s);
    }

    context = library.makeNamingContext(sources);
    const auto formats = TrackNaming::detectFormats(sources, context);

    // Carpetas, con el formato detectado o el que eligió el operador.
    groups.clear();
    std::map<juce::String, std::pair<int, int>> chosen;  // carpeta → votos del operador (T–A, A–T)
    for (const auto& item : items) {
        if (item.formatChecked && item.orientation != 0) {
            auto& c = chosen[item.groupKey];
            (item.orientation > 0 ? c.first : c.second)++;
        }
    }
    for (const auto& item : items) {
        auto found = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.key == item.groupKey; });
        if (found == groups.end()) {
            Group g;
            g.key = item.groupKey;
            g.name = item.groupName;
            const auto f = formats.find(item.groupKey);
            if (f != formats.end()) g.format = f->second;
            const auto c = chosen.find(item.groupKey);
            if (c != chosen.end()) {
                g.checkedByUser = true;
                g.format.orientation = c->second.first >= c->second.second ? TrackNaming::Orientation::TitleArtist
                                                                            : TrackNaming::Orientation::ArtistTitle;
            }
            groups.push_back(g);
            found = groups.end() - 1;
        }
        ++found->items;
    }
    std::sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) { return a.name.compareNatural(b.name) < 0; });

    for (auto& item : items) propose(item);
    for (auto& item : items) evaluate(item);
    findDuplicates();
}

void ImportReview::propose(Item& item) {
    const auto* group = findGroup(item.groupKey);
    auto orientation = group != nullptr ? group->format.orientation : TrackNaming::Orientation::ArtistTitle;

    TrackNaming::Source source;
    source.fileName = str(item.source.name);
    source.group = item.groupKey;
    source.tagArtist = str(item.source.tagArtist);
    source.tagTitle = str(item.source.tagTitle);

    item.issues.clear();
    if (item.orientation != 0) {
        orientation = item.orientation > 0 ? TrackNaming::Orientation::TitleArtist : TrackNaming::Orientation::ArtistTitle;
    } else if (group != nullptr && !source.hasTags()) {
        // Un archivo con pruebas claras de ir al revés que su carpeta va como dicen sus pruebas.
        const int evidence = TrackNaming::orientationEvidence(source, context);
        const bool againstGroup = orientation == TrackNaming::Orientation::TitleArtist ? evidence <= -2 : evidence >= 2;
        if (againstGroup) {
            orientation = opposite(orientation);
            item.issues.push_back(Issue::Opposite);
        }
    }
    item.effective = orientation;

    if (item.manual) return;  // lo que escribió el operador no se toca
    item.proposal = TrackNaming::propose(source, orientation, context);
    item.suggestedArtist = item.proposal.artist;
    item.suggestedTitle = item.proposal.title;
}

void ImportReview::evaluate(Item& item) {
    const bool opposite = item.has(Issue::Opposite) && !item.manual;
    item.issues.clear();
    item.didYouMean = {};

    // Lo encontrado en línea: si coincide con lo propuesto, lo confirma (y completa el año y
    // los compositores que falten); si no, es una duda con su sugerencia.
    item.onlineAgrees = false;
    const bool hasOnline = item.online.status == Database::OnlineStatus::Found && item.online.score >= onlineMinimumScore;
    if (hasOnline) {
        item.onlineProposal = OnlineMetadata::toProposal(item.online, context);
        item.onlineAgrees = TrackNaming::key(item.onlineProposal.artist) == TrackNaming::key(item.proposal.artist)
                         && TrackNaming::key(item.onlineProposal.title) == TrackNaming::key(item.proposal.title);
        if (item.onlineAgrees && !item.manual) {
            if (item.proposal.year == 0) item.proposal.year = item.onlineProposal.year;
            if (item.proposal.composers.isEmpty()) item.proposal.composers = item.onlineProposal.composers;
            // Las tildes que el nombre del archivo perdió ("Volvere" → "Volveré"), con las
            // mayúsculas de la propuesta.
            item.proposal.title = TrackNaming::withAccentsFrom(item.proposal.title, item.onlineProposal.title);
            item.proposal.artist = TrackNaming::withAccentsFrom(item.proposal.artist, item.onlineProposal.artist);
        }
    }
    if (opposite && !item.onlineAgrees) item.issues.push_back(Issue::Opposite);

    const auto* group = findGroup(item.groupKey);
    const bool tagged = !item.source.tagArtist.empty() && !item.source.tagTitle.empty();

    if (item.proposal.artist.trim().isEmpty()) item.issues.push_back(Issue::NoArtist);
    if (!item.manual) {
        if (!tagged && item.orientation == 0 && group != nullptr && group->needsLook() && !item.onlineAgrees)
            item.issues.push_back(Issue::FormatUnverified);
        if (item.proposal.featuredGuessed && !item.onlineAgrees) item.issues.push_back(Issue::FeaturedGuess);
        item.didYouMean = context.closestKnown(item.proposal.artist);
        if (item.didYouMean.isNotEmpty()) item.issues.push_back(Issue::DidYouMean);
        if (hasOnline && !item.onlineAgrees) item.issues.push_back(Issue::OnlineDiffers);
    }
    if (item.duplicateOf != -1 && !item.keepDuplicate) item.issues.push_back(Issue::Duplicate);

    if (item.has(Issue::Duplicate)) item.state = State::Duplicate;
    else item.state = item.issues.empty() ? State::Ready : State::Check;
}

void ImportReview::findDuplicates() {
    const double tolerance = MusicLibrary::duplicateToleranceSeconds;

    // Entre las filas: la de id más alto es la copia (la otra llegó antes).
    std::map<juce::String, std::vector<size_t>> byKey;
    std::set<int> reviewed;
    for (size_t i = 0; i < items.size(); ++i) {
        byKey[items[i].proposal.key()].push_back(i);
        reviewed.insert(items[i].track.id);
    }

    // La música que no se está revisando, por clave.
    std::multimap<std::string, const Database::KeyRow*> outside;
    for (const auto& row : libraryKeys)
        if (reviewed.count(row.id) == 0) outside.insert({ row.key, &row });

    for (auto& item : items) {
        item.duplicateOf = -1;
        item.duplicateText = {};
        item.duplicateBySound = false;

        // La misma grabación, por su sonido, aunque el nombre sea otro.
        const auto sound = soundDuplicates.find(item.track.id);
        if (sound != soundDuplicates.end()) {
            item.duplicateOf = sound->second.first;
            item.duplicateText = sound->second.second;
            item.duplicateBySound = true;
        }

        const auto key = item.proposal.key();
        if (key.isEmpty() || item.proposal.artist.trim().isEmpty()) continue;

        for (size_t j : byKey[key]) {
            const auto& other = items[j];
            if (other.track.id < item.track.id && std::abs(other.track.duration - item.track.duration) <= tolerance) {
                item.duplicateOf = other.track.id;
                item.duplicateBySound = false;
                item.duplicateText = other.proposal.credit() + " - " + other.proposal.fullTitle() + "  ·  "
                                   + formatDuration(other.track.duration) + "  ·  " + other.groupName;
                break;
            }
        }
        if (item.duplicateOf == -1) {
            const auto range = outside.equal_range(key.toStdString());
            for (auto it = range.first; it != range.second; ++it) {
                if (std::abs(it->second->duration - item.track.duration) > tolerance) continue;
                item.duplicateOf = it->second->id;
                item.duplicateBySound = false;
                item.duplicateText = str(it->second->artist) + " - " + str(it->second->title) + "  ·  "
                                   + formatDuration(it->second->duration);
                break;
            }
        }
    }
    for (auto& item : items) evaluate(item);
}

void ImportReview::saveDraft(const Item& item) {
    auto* o = new juce::DynamicObject();
    o->setProperty("orientation", item.orientation);
    o->setProperty("formatChecked", item.formatChecked);
    o->setProperty("keepDuplicate", item.keepDuplicate);
    o->setProperty("manual", item.manual);
    if (item.manual) {
        const auto& p = item.proposal;
        o->setProperty("artist", p.artist);
        o->setProperty("featured", toVar(p.featured));
        o->setProperty("composers", toVar(p.composers));
        o->setProperty("title", p.title);
        o->setProperty("version", p.version);
        o->setProperty("year", p.year);
        o->setProperty("suggestedArtist", item.suggestedArtist);
        o->setProperty("suggestedTitle", item.suggestedTitle);
    }
    library.saveReviewDraft(item.track.id, juce::JSON::toString(juce::var(o), true));
}

//==============================================================================
// Lo que hace el operador

void ImportReview::setGroupOrientation(const juce::String& groupKey, TrackNaming::Orientation orientation) {
    for (auto& item : items) {
        if (item.groupKey != groupKey) continue;
        item.orientation = toOverride(orientation);
        item.formatChecked = true;
        saveDraft(item);
    }
    rebuild();
}

void ImportReview::swap(const std::vector<int>& rows) {
    for (int row : rows) {
        if (!validRow(row)) continue;
        auto& item = items[(size_t) row];
        if (item.manual) {
            auto& p = item.proposal;
            std::swap(p.artist, p.title);
        } else {
            item.orientation = toOverride(opposite(item.effective));
        }
        saveDraft(item);
    }
    rebuild();
}

void ImportReview::applyCase(const std::vector<int>& rows, CaseStyle style) {
    auto convert = [style](const juce::String& s) {
        return style == CaseStyle::Upper ? TrackNaming::upper(s) : TrackNaming::titleCase(TrackNaming::lower(s));
    };
    for (int row : rows) {
        if (!validRow(row)) continue;
        auto& item = items[(size_t) row];
        auto& p = item.proposal;
        p.artist = convert(p.artist);
        p.title = convert(p.title);
        p.version = convert(p.version);
        for (auto& f : p.featured) f = convert(f);
        for (auto& c : p.composers) c = convert(c);
        item.manual = true;
        saveDraft(item);
    }
    for (auto& item : items) evaluate(item);
    findDuplicates();
}

void ImportReview::edit(int row, const TrackNaming::Proposal& values) {
    if (!validRow(row)) return;
    auto& item = items[(size_t) row];
    item.proposal = values;
    item.proposal.featuredGuessed = false;
    item.manual = true;
    saveDraft(item);
    evaluate(item);
    findDuplicates();
}

void ImportReview::acceptDidYouMean(int row) {
    if (!validRow(row) || items[(size_t) row].didYouMean.isEmpty()) return;
    auto values = items[(size_t) row].proposal;
    values.artist = items[(size_t) row].didYouMean;
    edit(row, values);
}

void ImportReview::resolveFeatured(int row, bool isFeatured) {
    if (!validRow(row)) return;
    auto values = items[(size_t) row].proposal;
    if (!isFeatured && values.joinedArtist.isNotEmpty()) {
        values.artist = values.joinedArtist;
        values.featured.clear();
    }
    edit(row, values);
}

void ImportReview::keepBoth(int row) {
    if (!validRow(row)) return;
    auto& item = items[(size_t) row];
    item.keepDuplicate = true;
    saveDraft(item);
    evaluate(item);
}

void ImportReview::setCategory(const std::vector<int>& rows, int categoryId, bool add) {
    for (int row : rows) {
        if (!validRow(row)) continue;
        auto& item = items[(size_t) row];
        auto categories = library.getTrackCategories(item.track.id);
        const auto it = std::find(categories.begin(), categories.end(), categoryId);
        if (add && it == categories.end()) categories.push_back(categoryId);
        else if (!add && it != categories.end()) categories.erase(it);
        else continue;
        library.setTrackCategories(item.track.id, categories);
        item.categories = categories;
    }
}

//==============================================================================

ImportReview::Outcome ImportReview::confirm(std::vector<int> rows, const std::function<bool(double)>& progress) {
    Outcome outcome;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    beginConfirm(rows);
    for (size_t n = 0; n < rows.size(); ++n) {
        if (progress && !progress((double) n / (double) rows.size())) break;
        confirmRow(rows[n], outcome);
    }
    if (progress) progress(1.0);
    endConfirm();
    return outcome;
}

void ImportReview::beginConfirm(const std::vector<int>& rows) {
    // Lo que quede de cada carpeta conserva el formato que se le vio con todas sus filas
    // (sin ellas, dos o tres archivos sueltos no alcanzan para saberlo).
    const std::set<int> confirming(rows.begin(), rows.end());
    for (int i = 0; i < (int) items.size(); ++i) {
        auto& item = items[(size_t) i];
        const auto* group = findGroup(item.groupKey);
        if (item.orientation != 0 || item.manual || group == nullptr || confirming.count(i) > 0) continue;
        if (!group->checkedByUser && !group->format.verified()) continue;
        item.orientation = toOverride(item.effective);
        item.formatChecked = true;
        saveDraft(item);
    }
}

void ImportReview::confirmRow(int row, Outcome& outcome) {
    if (!validRow(row)) return;
    const auto& item = items[(size_t) row];
    if (item.state == State::Duplicate || item.proposal.artist.trim().isEmpty()) {
        ++outcome.failed;
        return;
    }

    // Si el operador corrigió solo el artista, lo que traía el archivo queda como alias.
    juce::String alias;
    if (TrackNaming::key(item.suggestedArtist) != TrackNaming::key(item.proposal.artist)
        && TrackNaming::key(item.suggestedTitle) == TrackNaming::key(item.proposal.title))
        alias = item.suggestedArtist;

    const auto result = library.confirmTrack(item.track.id, item.proposal, alias);
    if (!result.ok) ++outcome.failed;
    else {
        ++outcome.confirmed;
        if (result.moveDeferred) ++outcome.deferred;
        // Confirmada con los datos de MusicBrainz: se guardan su identificador y el ISRC.
        const bool sameAsOnline = item.online.status == Database::OnlineStatus::Found
                               && TrackNaming::key(item.onlineProposal.artist) == TrackNaming::key(item.proposal.artist)
                               && TrackNaming::key(item.onlineProposal.title) == TrackNaming::key(item.proposal.title);
        if (sameAsOnline)
            library.withDatabase([&item](Database& db) { return db.setOnlineIds(item.track.id, item.online.recordingId, item.online.isrc); });
    }
}

//==============================================================================
// En línea

bool ImportReview::updateOnline() {
    const auto online = library.withDatabase([](Database& db) { return db.getOnlineMatches(); });
    bool changed = false;
    for (auto& item : items) {
        const auto it = online.find(item.track.id);
        const auto updated = it != online.end() ? it->second : Database::OnlineMatch {};
        if (updated.status != item.online.status || updated.recordingId != item.online.recordingId || updated.score != item.online.score) {
            item.online = updated;
            changed = true;
        }
    }
    if (changed) findDuplicates();  // y cada fila se vuelve a evaluar
    return changed;
}

void ImportReview::acceptOnline(const std::vector<int>& rows) {
    for (int row : rows) {
        if (!validRow(row)) continue;
        auto& item = items[(size_t) row];
        if (item.online.status != Database::OnlineStatus::Found) continue;
        auto values = item.onlineProposal;
        if (values.version.isEmpty()) values.version = item.proposal.version;  // MusicBrainz no la separa siempre
        item.proposal = values;
        item.manual = true;
        saveDraft(item);
    }
    findDuplicates();
}

void ImportReview::rejectOnline(int row) {
    if (validRow(row)) edit(row, items[(size_t) row].proposal);
}

std::vector<int> ImportReview::onlineRows(bool onlyStrong) const {
    std::vector<int> rows;
    for (int i = 0; i < (int) items.size(); ++i) {
        const auto& item = items[(size_t) i];
        if (item.has(Issue::OnlineDiffers) && (!onlyStrong || item.online.score >= onlineStrongScore)) rows.push_back(i);
    }
    return rows;
}

int ImportReview::discard(std::vector<int> rows) {
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    int removed = 0;
    for (int row : rows) {
        if (!validRow(row)) continue;
        const auto& item = items[(size_t) row];
        const int mergeInto = item.has(Issue::Duplicate) || item.keepDuplicate ? item.duplicateOf : -1;
        if (library.discardTrack(item.track.id, mergeInto)) ++removed;
    }
    reload();
    return removed;
}

juce::File ImportReview::destinationFor(int row) const {
    if (!validRow(row)) return {};
    const auto& item = items[(size_t) row];
    return library.musicDestination(item.proposal, juce::File(str(item.track.path)).getFileExtension());
}
