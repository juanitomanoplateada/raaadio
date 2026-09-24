#include "StationSettings.h"
#include "SecretStore.h"

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
    o->setProperty("streamQuality", streamQuality);
    return juce::var(o);
}

StationSettings StationSettings::fromVar(const juce::var& v) {
    StationSettings s;
    if (!v.isObject()) return s;

    auto read = [&v](const char* name, auto fallback) {
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
    s.streamQuality = juce::jlimit(0, 10, (int) read("streamQuality", s.streamQuality));
    return s;
}

StationConfig::StationConfig(juce::File f) : file(std::move(f)) {
    if (file.existsAsFile()) {
        const auto json = juce::JSON::parse(file);
        settings = StationSettings::fromVar(json);
        // Una clave que una versión anterior guardó en claro se cifra enseguida.
        if (json.hasProperty("streamPassword")) save();
    }
}

void StationConfig::set(const StationSettings& newSettings) {
    settings = newSettings;
    save();
    sendChangeMessage();
}

void StationConfig::save() {
    if (file == juce::File()) return;
    file.getParentDirectory().createDirectory();
    file.replaceWithText(juce::JSON::toString(settings.toVar()));
}
