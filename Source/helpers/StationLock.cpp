#include "StationLock.h"

StationLock::StationLock(const juce::File& labRoot) : file(labRoot.getChildFile("raaadio.lock")) {}

StationLock::~StationLock() {
    release();
}

juce::String StationLock::thisComputer() {
    return juce::SystemStats::getComputerName();
}

juce::String StationLock::describe(const Owner& owner) {
    auto* o = new juce::DynamicObject();
    o->setProperty("computer", owner.computer);
    o->setProperty("user", owner.user);
    o->setProperty("heartbeat", owner.heartbeat.toMilliseconds());
    return juce::JSON::toString(juce::var(o));
}

std::optional<StationLock::Owner> StationLock::parse(const juce::String& text) {
    const auto json = juce::JSON::parse(text);
    if (!json.isObject() || !json.hasProperty("computer")) return std::nullopt;
    return Owner { json["computer"].toString(), json["user"].toString(), juce::Time((juce::int64) json["heartbeat"]) };
}

std::optional<StationLock::Owner> StationLock::otherOwner(juce::Time now) const {
    if (!file.existsAsFile()) return std::nullopt;
    auto owner = parse(file.loadFileAsString());
    if (!owner || owner->computer == thisComputer()) return std::nullopt;
    const auto age = now - owner->heartbeat;
    if (age > juce::RelativeTime::seconds(staleSeconds) || age < juce::RelativeTime::seconds(-staleSeconds))
        return std::nullopt;  // latido viejo (se cerró mal) o de un reloj muy corrido
    return owner;
}

void StationLock::acquire() {
    held = true;
    write();
    startTimer(heartbeatSeconds * 1000);
}

void StationLock::release() {
    stopTimer();
    if (!held) return;
    held = false;
    const auto owner = parse(file.loadFileAsString());
    if (owner && owner->computer == thisComputer()) file.deleteFile();
}

void StationLock::write() {
    if (!held) return;
    file.getParentDirectory().createDirectory();
    file.replaceWithText(describe({ thisComputer(), juce::SystemStats::getLogonName(), juce::Time::getCurrentTime() }));
}
