#include "StationSettings.h"
#include "SecretStore.h"
#include "PersistenceManager.h"
#include "../library/MusicLibrary.h"

namespace {
    const char* formatId(StationSettings::StreamFormat format) {
        return format == StationSettings::StreamFormat::Mp3 ? "mp3" : "ogg";
    }
}

juce::var StationSettings::toVar() const {
    auto* o = new juce::DynamicObject();
    o->setProperty("deadAirEnabled", deadAirEnabled);
    o->setProperty("deadAirSeconds", deadAirSeconds);
    o->setProperty("emergencyCategory", emergencyCategory);
    o->setProperty("airLogEnabled", airLogEnabled);
    o->setProperty("airLogRetentionDays", airLogRetentionDays);
    o->setProperty("streamEnabled", streamEnabled);
    o->setProperty("streamHost", streamHost);
    o->setProperty("streamPort", streamPort);
    o->setProperty("streamMount", streamMount);
    o->setProperty("streamUser", streamUser);
    o->setProperty("streamPasswordProtected", isStreamPasswordUnreadable() ? unreadablePassword
                                                                           : SecretStore::protect(streamPassword));
    o->setProperty("streamName", streamName);
    o->setProperty("streamFormat", formatId(streamFormat));
    o->setProperty("streamQuality", streamQuality);
    o->setProperty("streamBitrate", streamBitrate);
    o->setProperty("streamGenre", streamGenre);
    o->setProperty("streamDescription", streamDescription);
    o->setProperty("streamWebsite", streamWebsite);
    o->setProperty("streamPublic", streamPublic);
    o->setProperty("streamTls", streamTls);
    o->setProperty("streamMetadataDelay", streamMetadataDelay);
    o->setProperty("nowPlayingPushUrl", nowPlayingPushUrl);
    o->setProperty("backupEnabled", backupEnabled);
    o->setProperty("backupHour", backupHour);
    o->setProperty("backupExternalFolder", backupExternalFolder);
    o->setProperty("backupMirrorLibrary", backupMirrorLibrary);
    return juce::var(o);
}

StationSettings StationSettings::fromVar(const juce::var& v) {
    StationSettings s;
    if (!v.isObject()) return s;

    auto read = [&v](const char* name, const auto& fallback) {
        return v.hasProperty(name) ? v[name] : juce::var(fallback);
    };
    s.deadAirEnabled = read("deadAirEnabled", s.deadAirEnabled);
    s.deadAirSeconds = juce::jlimit(5, 600, (int) read("deadAirSeconds", s.deadAirSeconds));
    s.emergencyCategory = read("emergencyCategory", s.emergencyCategory).toString();
    s.airLogEnabled = read("airLogEnabled", s.airLogEnabled);
    s.airLogRetentionDays = juce::jlimit(1, 3650, (int) read("airLogRetentionDays", s.airLogRetentionDays));
    s.streamEnabled = read("streamEnabled", s.streamEnabled);
    s.streamHost = read("streamHost", s.streamHost).toString();
    s.streamPort = juce::jlimit(1, 65535, (int) read("streamPort", s.streamPort));
    s.streamMount = read("streamMount", s.streamMount).toString();
    s.streamUser = read("streamUser", s.streamUser).toString();
    if (v.hasProperty("streamPasswordProtected")) {
        const auto stored = v["streamPasswordProtected"].toString();
        if (!SecretStore::unprotect(stored, s.streamPassword)) s.unreadablePassword = stored;
    } else {
        s.streamPassword = read("streamPassword", s.streamPassword).toString();  // versiones anteriores: en claro
    }
    s.streamName = read("streamName", s.streamName).toString();

    // Las versiones anteriores solo transmitían en Ogg: una emisora que ya tenía servidor
    // sigue igual hasta que se cambie el formato.
    if (v.hasProperty("streamFormat"))
        s.streamFormat = v["streamFormat"].toString() == "ogg" ? StreamFormat::OggVorbis : StreamFormat::Mp3;
    else if (s.streamHost.isNotEmpty())
        s.streamFormat = StreamFormat::OggVorbis;

    s.streamQuality = juce::jlimit(0, 10, (int) read("streamQuality", s.streamQuality));
    s.streamBitrate = juce::jlimit(32, 320, (int) read("streamBitrate", s.streamBitrate));
    s.streamGenre = read("streamGenre", s.streamGenre).toString();
    s.streamDescription = read("streamDescription", s.streamDescription).toString();
    s.streamWebsite = read("streamWebsite", s.streamWebsite).toString();
    s.streamPublic = read("streamPublic", s.streamPublic);
    s.streamTls = read("streamTls", s.streamTls);
    s.streamMetadataDelay = juce::jlimit(0, 60, (int) read("streamMetadataDelay", s.streamMetadataDelay));
    s.nowPlayingPushUrl = read("nowPlayingPushUrl", s.nowPlayingPushUrl).toString();
    // Antes el respaldo automático se activaba en la configuración de Windows, no en la emisora.
    s.backupEnabled = read("backupEnabled", PersistenceManager::legacyAutoBackupSetting());
    s.backupHour = juce::jlimit(0, 23, (int) read("backupHour", s.backupHour));
    s.backupExternalFolder = read("backupExternalFolder", s.backupExternalFolder).toString();
    s.backupMirrorLibrary = read("backupMirrorLibrary", s.backupMirrorLibrary);
    return s;
}

StationConfig::StationConfig(MusicLibrary* lib) : library(lib) {
    if (library == nullptr) return;
    const auto text = library->withDatabase([](Database& db) { return db.getSetting("station"); });
    const auto json = juce::JSON::parse(juce::String(text));
    settings = StationSettings::fromVar(json);
    // Una clave que una versión anterior guardó en claro se cifra enseguida.
    if (json.hasProperty("streamPassword")) save();
}

void StationConfig::set(const StationSettings& newSettings) {
    settings = newSettings;
    save();
    sendChangeMessage();
}

void StationConfig::save() {
    if (library == nullptr) return;
    const auto text = juce::JSON::toString(settings.toVar(), true).toStdString();
    if (!library->withDatabase([&text](Database& db) { return db.setSetting("station", text); }))
        juce::Logger::writeToLog("StationConfig: could not save the station settings");
}
