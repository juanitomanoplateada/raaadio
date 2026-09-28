 

#include "AdvertisingManager.h"
#include "MusicLibrary.h"

AdvertisingManager::AdvertisingManager()
{
}

AdvertisingManager::~AdvertisingManager()
{
}

void AdvertisingManager::initialise(MusicLibrary& lib)
{
    library = &lib;
    loadData();
}

void AdvertisingManager::loadData()
{
    juce::ScopedLock sl(dataLock);
    if (library == nullptr) return;

    const auto plan = library->withDatabase([](Database& db) { return db.loadAdPlan(); });
    trackToNumber = plan.numberByTrack;
    numberToSchedule.clear();
    for (const auto& [number, slots] : plan.slotsByNumber)
        for (const auto& slot : slots)
            numberToSchedule[number].add(juce::String(slot));
    autoSchedule = plan.autoSchedule;
}

void AdvertisingManager::saveData()
{
    juce::ScopedLock sl(dataLock);
    if (library == nullptr) return;

    Database::AdPlan plan;
    plan.numberByTrack = trackToNumber;
    for (const auto& [number, times] : numberToSchedule)
        for (const auto& t : times)
            plan.slotsByNumber[number].push_back(t.toStdString());
    plan.autoSchedule = autoSchedule;

    if (!library->withDatabase([&plan](Database& db) { return db.saveAdPlan(plan); }))
        juce::Logger::writeToLog("AdvertisingManager: could not save the ad plan");
}

void AdvertisingManager::setCueNumber(int trackId, int number)
{
    const juce::ScopedLock sl(dataLock);
    
    if (number < 0) {
        trackToNumber.erase(trackId);
    } else {
        trackToNumber[trackId] = std::min(99, number);  
    }
    
    saveData();  
    sendChangeMessage();
}

int AdvertisingManager::getCueNumber(int trackId) const
{
     
     
     
     
     
     
     
     
     
    
    auto it = trackToNumber.find(trackId);
    if (it != trackToNumber.end())
        return it->second;
    return -1;
}

std::vector<int> AdvertisingManager::getTracksWithNumber(int number) const
{
    std::vector<int> tracks;
    for (auto const& [tid, num] : trackToNumber)
    {
        if (num == number)
            tracks.push_back(tid);
    }
    return tracks;
}

void AdvertisingManager::addSchedule(int number, const juce::String& timeSlot)
{
    const juce::ScopedLock sl(dataLock);
    if (!isValidTimeSlot(timeSlot)) return;
    
     
     
    if (!numberToSchedule[number].contains(timeSlot)) {
        numberToSchedule[number].add(timeSlot);
        numberToSchedule[number].sort(true);  
        saveData();
        sendChangeMessage();
    }
}

void AdvertisingManager::removeSchedule(int number, const juce::String& timeSlot)
{
    const juce::ScopedLock sl(dataLock);
    if (numberToSchedule[number].contains(timeSlot)) {
        numberToSchedule[number].removeString(timeSlot);
        saveData();
        sendChangeMessage();
    }
}

juce::StringArray AdvertisingManager::getSchedules(int number) const
{
    auto it = numberToSchedule.find(number);
    if (it != numberToSchedule.end())
        return it->second;
    return {};
}

bool AdvertisingManager::isValidTimeSlot(const juce::String& timeSlot)
{
     
     
    if (timeSlot.length() != 5) return false;
    if (timeSlot[2] != ':') return false;
    return true;
}

std::vector<int> AdvertisingManager::getNumbersForTime(const juce::String& timeSlot) const
{
    std::vector<int> numbers;
    for (auto const& [num, times] : numberToSchedule)
    {
        if (times.contains(timeSlot))
            numbers.push_back(num);
    }
    std::sort(numbers.begin(), numbers.end());
    return numbers;
}

void AdvertisingManager::resetData() {
    const juce::ScopedLock sl(dataLock);
    trackToNumber.clear();
    numberToSchedule.clear();
    saveData();  
    sendChangeMessage(); 
}

juce::StringArray AdvertisingManager::getScheduledSlots() const
{
    const juce::ScopedLock sl(dataLock);
    juce::StringArray slots;
    for (auto const& [num, times] : numberToSchedule)
        for (const auto& t : times)
            slots.addIfNotAlreadyThere(t);
    slots.sort(false);
    return slots;
}

void AdvertisingManager::setAutoScheduleEnabled(bool enabled)
{
    const juce::ScopedLock sl(dataLock);
    if (autoSchedule == enabled) return;
    autoSchedule = enabled;
    saveData();
    sendChangeMessage();
}
