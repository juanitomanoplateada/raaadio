#pragma once
#include <JuceHeader.h>
#include <vector>

enum class InsertionMode {
    AddToEnd,
    AddToStart,
    Replace
};

struct GeneratorSettings {
     
    std::vector<int> includedCategories;
    std::vector<int> excludedCategories;

     
    int artistSeparation = 5;       
    int titleSeparation = 10;       
    int categorySeparation = 3;     
    int lastPlayedSeparation = 60;  

     
    int targetDurationVal = 60;     
    bool targetDurationIsHours = false;  

     
    int separatorCategoryId = -1;
    int separatorInterval = 0;      

     
    InsertionMode insertionMode = InsertionMode::AddToEnd;
    
     
    juce::var toVar() const {
        juce::DynamicObject* obj = new juce::DynamicObject();
        
        juce::Array<juce::var> inc, exc;
        for(auto i : includedCategories) inc.add(i);
        for(auto i : excludedCategories) exc.add(i);
        
        obj->setProperty("includedCategories", inc);
        obj->setProperty("excludedCategories", exc);
        obj->setProperty("artistSeparation", artistSeparation);
        obj->setProperty("titleSeparation", titleSeparation);
        obj->setProperty("categorySeparation", categorySeparation);
        obj->setProperty("lastPlayedSeparation", lastPlayedSeparation);
        obj->setProperty("targetDurationVal", targetDurationVal);
        obj->setProperty("targetDurationIsHours", targetDurationIsHours);
        obj->setProperty("separatorCategoryId", separatorCategoryId);
        obj->setProperty("separatorInterval", separatorInterval);
        obj->setProperty("insertionMode", static_cast<int>(insertionMode));
        
        return obj;
    }
    
    static GeneratorSettings fromVar(const juce::var& v) {
        GeneratorSettings s;
        if (!v.isObject()) return s;
        
        auto* obj = v.getDynamicObject();
        
        auto inc = obj->getProperty("includedCategories").getArray();
        if (inc) for(auto& i : *inc) s.includedCategories.push_back(static_cast<int>(i));
        
        auto exc = obj->getProperty("excludedCategories").getArray();
        if (exc) for(auto& i : *exc) s.excludedCategories.push_back(static_cast<int>(i));
        
        s.artistSeparation = obj->getProperty("artistSeparation");
        s.titleSeparation = obj->getProperty("titleSeparation");
        s.categorySeparation = obj->getProperty("categorySeparation");
        s.lastPlayedSeparation = obj->getProperty("lastPlayedSeparation");
        s.targetDurationVal = obj->getProperty("targetDurationVal");
        s.targetDurationIsHours = obj->getProperty("targetDurationIsHours");
        s.separatorCategoryId = obj->getProperty("separatorCategoryId");
        s.separatorInterval = obj->getProperty("separatorInterval");
        s.insertionMode = static_cast<InsertionMode>(static_cast<int>(obj->getProperty("insertionMode")));
        
        return s;
    }
};
