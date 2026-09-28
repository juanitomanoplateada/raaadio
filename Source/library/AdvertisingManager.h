 

#pragma once

#include <JuceHeader.h>

class MusicLibrary;

// La pauta: el N.º de cuña de cada pista, las franjas de cada número y si la pauta
// automática está activa. Vive en la base de datos de la emisora; cada cambio se guarda al
// momento.
class AdvertisingManager : public juce::ChangeBroadcaster
{
public:
    AdvertisingManager();
    ~AdvertisingManager();

    void initialise(MusicLibrary& library);
    void saveData();
    void resetData();

     
     
     
    void setCueNumber(int trackId, int number);
    int getCueNumber(int trackId) const;
    std::vector<int> getTracksWithNumber(int number) const;

     
     
     
    void addSchedule(int number, const juce::String& timeSlot);
    void removeSchedule(int number, const juce::String& timeSlot);
    juce::StringArray getSchedules(int number) const;
    
     
    std::vector<int> getNumbersForTime(const juce::String& timeSlot) const;
    
     
    static bool isValidTimeSlot(const juce::String& timeSlot);

    // Todas las franjas ("HH:MM") que tienen alguna cuña programada, ordenadas.
    juce::StringArray getScheduledSlots() const;

    // Pauta automática: AdScheduler inserta sola la tanda de cada franja en la cola.
    bool isAutoScheduleEnabled() const { return autoSchedule; }
    void setAutoScheduleEnabled(bool enabled);

private:
    MusicLibrary* library = nullptr;
    
     
     
    std::map<int, int> trackToNumber;
    
     
    std::map<int, juce::StringArray> numberToSchedule;

    bool autoSchedule = false;
    
    mutable juce::CriticalSection dataLock;

    void loadData();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvertisingManager)
};
