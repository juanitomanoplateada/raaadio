 

#include "AdvertisingManager.h"

AdvertisingManager::AdvertisingManager()
{
}

AdvertisingManager::~AdvertisingManager()
{
    saveData();
}

void AdvertisingManager::initialise(const juce::File& dataFile)
{
    persistenceFile = dataFile;
    loadData();
}

void AdvertisingManager::loadData()
{
    juce::ScopedLock sl(dataLock);
    
    if (!persistenceFile.exists())
        return;

    auto json = juce::JSON::parse(persistenceFile);
    if (!json.isObject())
        return;

     
    auto assignments = json["assignments"];
    if (assignments.isArray())
    {
        for (auto& item : *assignments.getArray())
        {
            if (item.hasProperty("tid") && item.hasProperty("num"))
            {
                int tid = item["tid"];
                int num = item["num"];
                if (num >= 0 && num <= 99)
                    trackToNumber[tid] = num;
            }
        }
    }

     
    autoSchedule = (bool) json.getProperty("autoSchedule", false);

    auto schedules = json["schedules"];
    if (schedules.isArray())
    {
        for (auto& item : *schedules.getArray())
        {
            if (item.hasProperty("num") && item.hasProperty("times"))
            {
                int num = item["num"];
                auto times = item["times"];
                if (times.isArray())
                {
                    juce::StringArray sa;
                    for (auto& t : *times.getArray())
                        sa.add(t.toString());
                    
                    numberToSchedule[num] = sa;
                }
            }
        }
    }
}

void AdvertisingManager::saveData()
{
    juce::ScopedLock sl(dataLock);

    juce::DynamicObject* root = new juce::DynamicObject();
    
     
    juce::Array<juce::var> assignArray;
    for (auto const& [tid, num] : trackToNumber)
    {
        juce::DynamicObject* obj = new juce::DynamicObject();
        obj->setProperty("tid", tid);
        obj->setProperty("num", num);
        assignArray.add(juce::var(obj));
    }
    root->setProperty("assignments", assignArray);

     
    juce::Array<juce::var> schedArray;
    for (auto const& [num, times] : numberToSchedule)
    {
        if (times.isEmpty()) continue;
        
        juce::DynamicObject* obj = new juce::DynamicObject();
        obj->setProperty("num", num);
        
        juce::Array<juce::var> timeArray;
        for (auto& t : times)
            timeArray.add(t);
            
        obj->setProperty("times", timeArray);
        schedArray.add(juce::var(obj));
    }
    root->setProperty("schedules", schedArray);
    root->setProperty("autoSchedule", autoSchedule);

     
    juce::String jsonString = juce::JSON::toString(juce::var(root));
    persistenceFile.replaceWithText(jsonString);
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
