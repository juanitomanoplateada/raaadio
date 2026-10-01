#include "PlaylistGeneratorDialog.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"
#include "../library/TrackNaming.h"

namespace {
    constexpr int padding = 16, rowH = UiStyle::controlHeight, titleH = 18;
    constexpr int defaultTemplateId = 1;   // "Valores por defecto" en la lista de plantillas
    constexpr int noSeparatorId = 9999;
    constexpr int includeChipW = 64, excludeChipW = 66;

    juce::String dot() { return juce::String::fromUTF8("  \xc2\xb7  "); }

    // Un texto con su cantidad en %1; con una sola, la versión en singular (clave + "_one").
    juce::String counted(const juce::String& key, int n) {
        return TRANS(n == 1 ? key + "_one" : key).replace("%1", juce::String(n));
    }

    // "1 hora", "4 horas", "1 h 30 min", "45 min".
    juce::String niceDuration(double seconds) {
        const int minutes = juce::roundToInt(seconds / 60.0);
        if (minutes >= 60 && minutes % 60 == 0)
            return minutes == 60 ? TRANS("gen_dur_1h") : TRANS("gen_dur_hours").replace("%1", juce::String(minutes / 60));
        return UiStyle::formatLongDuration(seconds);
    }

    // Botón que se queda marcado (un modo, una opción, una categoría).
    void makeToggle(juce::TextButton& b, int connectedEdges = 0) {
        b.getProperties().set("toggleStyle", "soft");
        b.setConnectedEdges(connectedEdges);
    }

    void numeric(juce::TextEditor& e, int digits) {
        e.setInputRestrictions(digits, "0123456789");
        e.setJustification(juce::Justification::centred);
        e.setFont(UiStyle::mono(14.0f, true));
        e.setSelectAllWhenFocused(true);
    }
}

PlaylistGeneratorDialog::PlaylistGeneratorDialog(MusicLibrary& lib, Playlist& pl)
    : library(lib), playlist(pl), templateManager(lib), generator(lib)
{
    // Arriba: modo y plantillas.
    for (auto* b : { &simpleModeButton, &advancedModeButton }) addAndMakeVisible(*b);
    makeToggle(simpleModeButton, juce::Button::ConnectedOnRight);
    makeToggle(advancedModeButton, juce::Button::ConnectedOnLeft);
    simpleModeButton.setButtonText(TRANS("gen_mode_simple"));
    advancedModeButton.setButtonText(TRANS("gen_mode_advanced"));
    simpleModeButton.setTooltip(TRANS("tip_gen_mode_simple"));
    advancedModeButton.setTooltip(TRANS("tip_gen_mode_advanced"));
    simpleModeButton.onClick = [this] { showAdvanced(false); };
    advancedModeButton.onClick = [this] { showAdvanced(true); };

    addAndMakeVisible(templateCombo);
    templateCombo.setTooltip(TRANS("tip_gen_templates"));
    templateCombo.onChange = [this] {
        const int id = templateCombo.getSelectedId();
        if (id <= 0) return;
        settings = id == defaultTemplateId ? GeneratorSettings() : templateManager.loadTemplate(templateCombo.getText());
        syncControlsFromSettings();
        refresh();
    };
    addChildComponent(saveTemplateButton);
    addChildComponent(deleteTemplateButton);
    saveTemplateButton.setButtonText(TRANS("gen_tpl_save"));
    deleteTemplateButton.setButtonText(TRANS("gen_tpl_delete"));
    UiStyle::setVariant(deleteTemplateButton, "ghost");
    saveTemplateButton.onClick = [this] { saveTemplate(); };
    deleteTemplateButton.onClick = [this] { deleteTemplate(); };

    // Rápido: duración en botones.
    struct Chip { int minutes; juce::String label; };
    const Chip chips[] = { { 30, TRANS("gen_dur_30m") }, { 60, TRANS("gen_dur_1h") },
                           { 120, TRANS("gen_dur_hours").replace("%1", "2") }, { 180, TRANS("gen_dur_hours").replace("%1", "3") },
                           { 240, TRANS("gen_dur_hours").replace("%1", "4") }, { 360, TRANS("gen_dur_hours").replace("%1", "6") },
                           { 480, TRANS("gen_dur_hours").replace("%1", "8") }, { 720, TRANS("gen_dur_hours").replace("%1", "12") } };
    for (const auto& chip : chips) {
        auto* b = durationChips.add(new juce::TextButton(chip.label));
        makeToggle(*b);
        durationChipMinutes.push_back(chip.minutes);
        const int minutes = chip.minutes;
        b->onClick = [this, minutes] {
            // Horas enteras se guardan en horas: así se leen igual en el modo avanzado.
            settings.targetDurationIsHours = minutes % 60 == 0;
            settings.targetDurationVal = settings.targetDurationIsHours ? minutes / 60 : minutes;
            syncControlsFromSettings();
            refresh();
        };
        addChildComponent(b);
    }

    addChildComponent(categoryViewport);
    categoryViewport.setViewedComponent(&categoryFlow, false);
    categoryViewport.setScrollBarsShown(true, false);

    addChildComponent(separatorToggle);
    separatorToggle.setButtonText(TRANS("gen_sep_toggle"));
    separatorToggle.onClick = [this] {
        if (separatorToggle.getToggleState()) {
            if (settings.separatorCategoryId == -1) settings.separatorCategoryId = sweepersCategoryId;
            if (settings.separatorInterval <= 0) settings.separatorInterval = 4;
        } else {
            settings.separatorInterval = 0;
        }
        syncControlsFromSettings();
        refresh();
    };
    addChildComponent(separatorEveryCombo);
    for (int n = 1; n <= 20; ++n) separatorEveryCombo.addItem(juce::String(n), n);
    separatorEveryCombo.onChange = [this] {
        if (separatorEveryCombo.getSelectedId() > 0) settings.separatorInterval = separatorEveryCombo.getSelectedId();
        syncControlsFromSettings();
        refresh();
    };

    // Avanzado.
    addChildComponent(categoryList);
    categoryList.setModel(this);
    categoryList.setRowHeight(30);

    for (auto* e : { &durEditor, &artistSepEditor, &titleSepEditor, &catSepEditor, &lastPlayEditor, &sepIntEditor }) addChildComponent(*e);
    numeric(durEditor, 4);
    numeric(artistSepEditor, 3);
    numeric(titleSepEditor, 3);
    numeric(catSepEditor, 3);
    numeric(lastPlayEditor, 5);
    numeric(sepIntEditor, 3);
    artistSepEditor.setTooltip(TRANS("tip_gen_rule_artist"));
    titleSepEditor.setTooltip(TRANS("tip_gen_rule_title"));
    catSepEditor.setTooltip(TRANS("tip_gen_rule_category"));
    lastPlayEditor.setTooltip(TRANS("tip_gen_rule_last"));
    durEditor.onTextChange = [this] { settings.targetDurationVal = durEditor.getText().getIntValue(); refresh(); };
    artistSepEditor.onTextChange = [this] { settings.artistSeparation = artistSepEditor.getText().getIntValue(); refresh(); };
    titleSepEditor.onTextChange = [this] { settings.titleSeparation = titleSepEditor.getText().getIntValue(); refresh(); };
    catSepEditor.onTextChange = [this] { settings.categorySeparation = catSepEditor.getText().getIntValue(); refresh(); };
    lastPlayEditor.onTextChange = [this] { settings.lastPlayedSeparation = lastPlayEditor.getText().getIntValue(); refresh(); };
    sepIntEditor.onTextChange = [this] { settings.separatorInterval = sepIntEditor.getText().getIntValue(); refresh(); };

    addChildComponent(durUnitCombo);
    durUnitCombo.addItem(TRANS("gen_unit_min"), 1);
    durUnitCombo.addItem(TRANS("gen_unit_hours"), 2);
    durUnitCombo.onChange = [this] { settings.targetDurationIsHours = durUnitCombo.getSelectedId() == 2; refresh(); };

    addChildComponent(resetRulesButton);
    resetRulesButton.setButtonText(TRANS("gen_adv_reset"));
    resetRulesButton.setTooltip(TRANS("tip_gen_reset"));
    UiStyle::setVariant(resetRulesButton, "ghost");
    resetRulesButton.onClick = [this] {
        const GeneratorSettings defaults;
        settings.artistSeparation = defaults.artistSeparation;
        settings.titleSeparation = defaults.titleSeparation;
        settings.categorySeparation = defaults.categorySeparation;
        settings.lastPlayedSeparation = defaults.lastPlayedSeparation;
        syncControlsFromSettings();
        refresh();
    };

    addChildComponent(sepCatCombo);
    sepCatCombo.onChange = [this] {
        const int id = sepCatCombo.getSelectedId();
        settings.separatorCategoryId = id == noSeparatorId || id <= 0 ? -1 : id;
        if (settings.separatorCategoryId != -1 && settings.separatorInterval <= 0) settings.separatorInterval = 4;
        syncControlsFromSettings();
        refresh();
    };

    // Dónde ponerla.
    juce::TextButton* where[] = { &whereEndButton, &whereStartButton, &whereReplaceButton };
    const InsertionMode modes[] = { InsertionMode::AddToEnd, InsertionMode::AddToStart, InsertionMode::Replace };
    const char* const whereKeys[] = { "gen_where_end", "gen_where_start", "gen_where_replace" };
    for (int i = 0; i < 3; ++i) {
        addAndMakeVisible(*where[i]);
        makeToggle(*where[i], i == 0 ? juce::Button::ConnectedOnRight
                                     : i == 2 ? juce::Button::ConnectedOnLeft : juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
        where[i]->setButtonText(TRANS(whereKeys[i]));
        const auto mode = modes[i];
        where[i]->onClick = [this, mode] { settings.insertionMode = mode; refresh(); };
    }

    addChildComponent(changeRulesButton);
    changeRulesButton.setButtonText(TRANS("gen_change_rules"));
    UiStyle::setVariant(changeRulesButton, "ghost");
    changeRulesButton.onClick = [this] { showAdvanced(true); };

    // Pie.
    addAndMakeVisible(generateButton);
    UiStyle::setIcon(generateButton, "sparkle");
    generateButton.onClick = [this] { generate(); };
    addAndMakeVisible(closeButton);
    closeButton.setButtonText(TRANS("gen_btn_close"));
    UiStyle::setVariant(closeButton, "ghost");
    closeButton.onClick = [this] { if (onCancel) onCancel(); };
    addChildComponent(undoButton);
    undoButton.setButtonText(TRANS("gen_btn_undo"));
    undoButton.setTooltip(TRANS("tip_gen_undo"));
    undoButton.onClick = [this] { undoLast(); };

    auto& t = ThemeManager::getInstance();
    for (auto* b : { &simpleModeButton, &advancedModeButton, &whereEndButton, &whereStartButton, &whereReplaceButton })
        b->setColour(juce::TextButton::buttonOnColourId, t.getHighlightColour());
    for (auto* chip : durationChips) chip->setColour(juce::TextButton::buttonOnColourId, t.getHighlightColour());
    categoryList.setColour(juce::ListBox::backgroundColourId, t.getPanelBackgroundColour());

    loadLibrary();
    refreshTemplates();
    syncControlsFromSettings();
    setSize(defaultWidth, defaultHeight);
    showAdvanced(false);
    startTimer(5000);
}

PlaylistGeneratorDialog::~PlaylistGeneratorDialog() {
    stopTimer();
    categoryList.setModel(nullptr);
}

//==============================================================================
// Datos

void PlaylistGeneratorDialog::loadLibrary() {
    musicCategories.clear();
    allCategories.clear();
    std::vector<Category> system;
    for (const auto& c : library.getCategories()) {
        if (c.isSystem()) system.push_back(c);
        else musicCategories.push_back(c);
        if (c.role == CategoryRole::Sweepers) sweepersCategoryId = c.id;
    }
    allCategories = musicCategories;
    allCategories.insert(allCategories.end(), system.begin(), system.end());

    // Cuántas canciones que se pueden programar tiene cada categoría.
    songsInCategory.clear();
    const auto categoriesOf = library.getAllTrackCategories();
    const auto systemRoles = library.getSystemRolesByTrack();
    allMusicCount = 0;
    for (const auto& track : library.search("", "")) {
        if (track.isPending() || track.getAirDuration() <= 0.0) continue;
        if (systemRoles.count(track.id) == 0) ++allMusicCount;
        const auto it = categoriesOf.find(track.id);
        if (it != categoriesOf.end())
            for (int c : it->second) ++songsInCategory[c];
    }

    sepCatCombo.clear(juce::dontSendNotification);
    sepCatCombo.addItem(TRANS("gen_sep_none"), noSeparatorId);
    // Primero las de la emisora (separadores, identificaciones), que es lo habitual.
    for (const auto& c : system)
        if (c.role != CategoryRole::Spots) sepCatCombo.addItem(MusicLibrary::getDisplayName(c), c.id);
    for (const auto& c : musicCategories) sepCatCombo.addItem(MusicLibrary::getDisplayName(c), c.id);

    buildCategoryChips();
    categoryList.updateContent();
}

void PlaylistGeneratorDialog::buildCategoryChips() {
    categoryFlow.chips.clear();
    auto& t = ThemeManager::getInstance();
    auto add = [&](const juce::String& text, std::function<void()> onClick) {
        auto* b = categoryFlow.chips.add(new juce::TextButton(text));
        makeToggle(*b);
        b->setColour(juce::TextButton::buttonOnColourId, t.getHighlightColour());
        b->onClick = std::move(onClick);
        categoryFlow.addAndMakeVisible(b);
        return b;
    };
    add(TRANS("gen_all_music").replace("%1", juce::String(allMusicCount)), [this] {
        settings.includedCategories.clear();
        settings.excludedCategories.clear();
        refresh();
    });
    for (const auto& c : musicCategories) {
        const int count = songsInCategory.count(c.id) > 0 ? songsInCategory[c.id] : 0;
        const int id = c.id;
        auto* b = add(TRANS("gen_cat_chip").replace("%1", MusicLibrary::getDisplayName(c)).replace("%2", juce::String(count)),
                      [this, id] { toggleIncluded(id); });
        b->setEnabled(count > 0);  // una categoría vacía no tiene nada que aportar
    }
}

int PlaylistGeneratorDialog::ChipFlow::layout(int width) {
    int x = 0, y = 0;
    const int h = 32, gap = 8;
    for (auto* chip : chips) {
        const int w = juce::jmin(width, chip->getBestWidthForHeight(h) + 22);
        if (x > 0 && x + w > width) {
            x = 0;
            y += h + gap;
        }
        chip->setBounds(x, y, w, h);
        x += w + gap;
    }
    return chips.isEmpty() ? 0 : y + h;
}

juce::String PlaylistGeneratorDialog::categoryName(int id) const {
    for (const auto& c : allCategories)
        if (c.id == id) return MusicLibrary::getDisplayName(c);
    return {};
}

void PlaylistGeneratorDialog::refreshTemplates(const juce::String& select) {
    templateCombo.clear(juce::dontSendNotification);
    templateCombo.addItem(TRANS("gen_template_default"), defaultTemplateId);
    const auto names = templateManager.getTemplateNames();
    if (!names.isEmpty()) templateCombo.addSeparator();
    int id = defaultTemplateId + 1, selected = defaultTemplateId;
    for (const auto& name : names) {
        templateCombo.addItem(name, id);
        if (name == select) selected = id;
        ++id;
    }
    templateCombo.setSelectedId(selected, juce::dontSendNotification);
}

// Los controles, desde los ajustes (sin avisar: no es un cambio del usuario).
void PlaylistGeneratorDialog::syncControlsFromSettings() {
    auto setNumber = [](juce::TextEditor& e, int value) {
        if (e.getText().getIntValue() != value || e.getText().isEmpty()) e.setText(juce::String(value), false);
    };
    setNumber(durEditor, settings.targetDurationVal);
    setNumber(artistSepEditor, settings.artistSeparation);
    setNumber(titleSepEditor, settings.titleSeparation);
    setNumber(catSepEditor, settings.categorySeparation);
    setNumber(lastPlayEditor, settings.lastPlayedSeparation);
    setNumber(sepIntEditor, settings.separatorInterval);
    durUnitCombo.setSelectedId(settings.targetDurationIsHours ? 2 : 1, juce::dontSendNotification);
    sepCatCombo.setSelectedId(settings.separatorCategoryId == -1 ? noSeparatorId : settings.separatorCategoryId, juce::dontSendNotification);

    separatorToggle.setToggleState(settings.usesSeparators(), juce::dontSendNotification);
    const int every = settings.separatorInterval > 0 ? settings.separatorInterval : 4;
    if (every > 20 && separatorEveryCombo.indexOfItemId(every) < 0) separatorEveryCombo.addItem(juce::String(every), every);
    separatorEveryCombo.setSelectedId(every, juce::dontSendNotification);
}

void PlaylistGeneratorDialog::toggleIncluded(int categoryId) {
    auto& inc = settings.includedCategories;
    auto& exc = settings.excludedCategories;
    exc.erase(std::remove(exc.begin(), exc.end(), categoryId), exc.end());
    const auto it = std::find(inc.begin(), inc.end(), categoryId);
    if (it != inc.end()) inc.erase(it);
    else inc.push_back(categoryId);
    refresh();
}

void PlaylistGeneratorDialog::toggleExcluded(int categoryId) {
    auto& inc = settings.includedCategories;
    auto& exc = settings.excludedCategories;
    inc.erase(std::remove(inc.begin(), inc.end(), categoryId), inc.end());
    const auto it = std::find(exc.begin(), exc.end(), categoryId);
    if (it != exc.end()) exc.erase(it);
    else exc.push_back(categoryId);
    refresh();
}

//==============================================================================
// Resumen: qué va a pasar con estos ajustes

juce::String PlaylistGeneratorDialog::rulesSentence() const {
    juce::StringArray rules;
    if (settings.artistSeparation > 0) rules.add(TRANS("gen_sum_rule_artist").replace("%1", juce::String(settings.artistSeparation)));
    if (settings.titleSeparation > 0) rules.add(TRANS("gen_sum_rule_title").replace("%1", juce::String(settings.titleSeparation)));
    if (settings.categorySeparation > 0) rules.add(TRANS("gen_sum_rule_category").replace("%1", juce::String(settings.categorySeparation)));
    if (settings.lastPlayedSeparation > 0) rules.add(TRANS("gen_sum_rule_last").replace("%1", juce::String(settings.lastPlayedSeparation)));
    const auto label = TRANS(settings.hasDefaultRules() ? "gen_sum_rules_default" : "gen_sum_rules_custom");
    return label + ": " + (rules.isEmpty() ? TRANS("gen_sum_rules_none") : rules.joinIntoString(dot()));
}

void PlaylistGeneratorDialog::buildSummary() {
    auto& t = ThemeManager::getInstance();
    summaryLines.clear();
    summaryBorder = {};
    canGenerate = true;

    const double target = settings.targetSeconds();
    const auto targetText = niceDuration(target);
    auto error = [&](const juce::String& text) {
        summaryLines.push_back({ text, t.getErrorColour(), true });
        summaryBorder = t.getErrorColour();
        canGenerate = false;
    };
    auto warning = [&](const juce::String& text) {
        summaryLines.push_back({ text, t.getWarningColour(), false });
        if (summaryBorder.isTransparent()) summaryBorder = t.getWarningColour();
    };

    if (settings.targetDurationVal <= 0) {
        error(TRANS("gen_err_duration"));
    } else if (available.songs == 0) {
        error(available.missingFiles > 0 ? TRANS("gen_err_missing_files")
              : available.recentlyPlayed > 0 ? TRANS("gen_err_all_recent").replace("%1", juce::String(settings.lastPlayedSeparation))
                                             : TRANS("gen_err_none"));
    } else {
        const double average = available.seconds / available.songs;
        const int estimate = juce::jmax(1, juce::roundToInt(target / juce::jmax(1.0, average)));
        const auto key = settings.insertionMode == InsertionMode::Replace ? "gen_sum_replace"
                       : settings.insertionMode == InsertionMode::AddToStart ? "gen_sum_add_start" : "gen_sum_add_end";
        summaryLines.push_back({ TRANS(key).replace("%1", juce::String(estimate)).replace("%2", targetText), t.getPrimaryTextColour(), true });
    }

    if (settings.targetDurationVal > 0) {
        auto line = counted("gen_sum_available", available.songs).replace("%2", UiStyle::formatLongDuration(available.seconds));
        if (available.recentlyPlayed > 0)
            line << " " << counted("gen_sum_recent", available.recentlyPlayed).replace("%2", juce::String(settings.lastPlayedSeparation));
        if (available.alreadyQueued > 0)
            line << " " << counted("gen_sum_queued", available.alreadyQueued);
        summaryLines.push_back({ line, t.getSecondaryTextColour(), false });
    }

    summaryLines.push_back({ rulesSentence(), t.getMutedTextColour(), false });

    // Lo que el modo rápido no muestra en sus botones.
    juce::StringArray extras;
    if (!settings.excludedCategories.empty()) {
        juce::StringArray names;
        for (int id : settings.excludedCategories) names.add(categoryName(id));
        extras.add(TRANS("gen_sum_excluded").replace("%1", names.joinIntoString(", ")));
    }
    if (settings.usesSeparators())
        extras.add(TRANS("gen_sum_separators").replace("%1", categoryName(settings.separatorCategoryId))
                                              .replace("%2", juce::String(settings.separatorInterval)));
    if (!extras.isEmpty()) summaryLines.push_back({ extras.joinIntoString(dot()), t.getMutedTextColour(), false });

    if (canGenerate) {
        const int fresh = available.songs - available.alreadyQueued;
        const double freshSeconds = available.songs > 0 ? available.seconds * fresh / available.songs : 0.0;
        if (target / juce::jmax(1.0, available.seconds / available.songs) > PlaylistGenerator::maxRows)
            warning(TRANS("gen_warn_too_long").replace("%1", juce::String(PlaylistGenerator::maxRows)));
        else if (fresh <= 0)
            warning(TRANS("gen_warn_all_queued").replace("%3", targetText));
        else if (freshSeconds < target)
            warning(TRANS("gen_warn_few").replace("%1", juce::String(fresh))
                                         .replace("%2", UiStyle::formatLongDuration(freshSeconds)).replace("%3", targetText));
        if (settings.usesSeparators() && available.separators == 0)
            warning(TRANS("gen_warn_no_separators").replace("%1", categoryName(settings.separatorCategoryId)));
        if (available.missingFiles > 0) warning(counted("gen_warn_missing", available.missingFiles));
    }
}

void PlaylistGeneratorDialog::refresh() {
    available = generator.check(settings, playlist);
    const auto queue = playlist.getQueue();
    queueCount = (int) queue.size();
    queueSeconds = 0.0;
    for (const auto& track : queue) queueSeconds += track.getAirDuration();

    buildSummary();

    // Lo que está marcado.
    simpleModeButton.setToggleState(!advanced, juce::dontSendNotification);
    advancedModeButton.setToggleState(advanced, juce::dontSendNotification);
    const double target = settings.targetSeconds();
    for (int i = 0; i < durationChips.size(); ++i)
        durationChips[i]->setToggleState(std::abs(target - durationChipMinutes[(size_t) i] * 60.0) < 0.5, juce::dontSendNotification);
    const auto& inc = settings.includedCategories;
    if (!categoryFlow.chips.isEmpty()) categoryFlow.chips[0]->setToggleState(inc.empty(), juce::dontSendNotification);
    for (int i = 0; i < (int) musicCategories.size() && i + 1 < categoryFlow.chips.size(); ++i)
        categoryFlow.chips[i + 1]->setToggleState(std::find(inc.begin(), inc.end(), musicCategories[(size_t) i].id) != inc.end(),
                                                  juce::dontSendNotification);
    whereEndButton.setToggleState(settings.insertionMode == InsertionMode::AddToEnd, juce::dontSendNotification);
    whereStartButton.setToggleState(settings.insertionMode == InsertionMode::AddToStart, juce::dontSendNotification);
    whereReplaceButton.setToggleState(settings.insertionMode == InsertionMode::Replace, juce::dontSendNotification);

    // Separadores: sin piezas en la categoría de la emisora no hay qué ofrecer en el modo rápido.
    const bool hasSweepers = sweepersCategoryId != -1 && songsInCategory.count(sweepersCategoryId) > 0;
    separatorToggle.setEnabled(hasSweepers || settings.usesSeparators());
    separatorEveryCombo.setEnabled(settings.usesSeparators());
    sepIntEditor.setEnabled(settings.separatorCategoryId != -1);
    resetRulesButton.setEnabled(!settings.hasDefaultRules());
    deleteTemplateButton.setEnabled(templateCombo.getSelectedId() > defaultTemplateId);

    // Generar: al reemplazar, el botón lo dice.
    const bool replacing = settings.insertionMode == InsertionMode::Replace && queueCount > 0;
    generateButton.setButtonText(TRANS(replacing ? "gen_btn_replace" : "gen_btn_generate2"));
    UiStyle::setVariant(generateButton, replacing ? "warning" : "primary");
    generateButton.setEnabled(canGenerate);
    undoButton.setVisible(canUndo);

    categoryList.repaint();
    resized();
    repaint();
}

void PlaylistGeneratorDialog::showAdvanced(bool shouldBeAdvanced) {
    advanced = shouldBeAdvanced;
    for (auto* chip : durationChips) chip->setVisible(!advanced);
    categoryViewport.setVisible(!advanced);
    separatorToggle.setVisible(!advanced);
    separatorEveryCombo.setVisible(!advanced);
    changeRulesButton.setVisible(!advanced);

    for (juce::Component* c : std::initializer_list<juce::Component*> { &categoryList, &durEditor, &durUnitCombo, &artistSepEditor,
                                                                        &titleSepEditor, &catSepEditor, &lastPlayEditor, &resetRulesButton,
                                                                        &sepCatCombo, &sepIntEditor, &saveTemplateButton, &deleteTemplateButton })
        c->setVisible(advanced);
    refresh();
}

//==============================================================================
// Acciones

void PlaylistGeneratorDialog::generate() {
    if (!canGenerate) return;
    auto& t = ThemeManager::getInstance();

    lastResult = generator.generate(settings, playlist);
    canUndo = lastResult.success;
    if (!lastResult.success) {
        resultText = lastResult.message;
        resultColour = t.getErrorColour();
    } else {
        const auto duration = UiStyle::formatLongDuration(lastResult.seconds);
        resultText = lastResult.separators > 0
                   ? TRANS("gen_done_with_separators").replace("%1", juce::String(lastResult.songs)).replace("%2", duration)
                                                      .replace("%3", juce::String(lastResult.separators))
                   : TRANS("gen_done").replace("%1", juce::String(lastResult.songs)).replace("%2", duration);
        resultColour = t.getSuccessColour();

        // Lo que no salió como se pidió, dicho.
        juce::StringArray notes;
        if (!lastResult.reachedTarget) notes.add(lastResult.message);
        if (lastResult.repeatedSongs > 0) notes.add(counted("gen_note_repeated", lastResult.repeatedSongs));
        if (lastResult.relaxedRules > 0) notes.add(counted("gen_note_relaxed", lastResult.relaxedRules));
        if (lastResult.missingFiles > 0) notes.add(counted("gen_note_missing", lastResult.missingFiles));
        if (!notes.isEmpty()) {
            resultText << " " << notes.joinIntoString(" ");
            resultColour = t.getWarningColour();
        }
        if (onGenerateSuccess) onGenerateSuccess();
    }
    refresh();
}

// Quita de la cola lo último que se generó (lo que todavía esté: algo pudo haber salido al aire).
void PlaylistGeneratorDialog::undoLast() {
    if (!canUndo) return;
    const auto queue = playlist.getQueue();
    std::vector<Playlist::RowRef> rows;
    const auto& tracks = lastResult.tracks;

    if (settings.insertionMode == InsertionMode::AddToEnd) {
        // Desde el final: si una canción está dos veces en la cola, la generada es la última.
        int cursor = (int) queue.size() - 1;
        for (int i = (int) tracks.size() - 1; i >= 0 && cursor >= 0; --i) {
            int found = cursor;
            while (found >= 0 && queue[(size_t) found].path != tracks[(size_t) i].path) --found;
            if (found < 0) continue;
            rows.push_back({ found, queue[(size_t) found].path });
            cursor = found - 1;
        }
    } else {
        int cursor = 0;
        for (const auto& track : tracks) {
            int found = cursor;
            while (found < (int) queue.size() && queue[(size_t) found].path != track.path) ++found;
            if (found >= (int) queue.size()) continue;
            rows.push_back({ found, queue[(size_t) found].path });
            cursor = found + 1;
        }
    }

    const int removed = playlist.removeTracksIfMatching(rows);
    canUndo = false;
    resultText = TRANS("gen_undone").replace("%1", juce::String(removed));
    resultColour = ThemeManager::getInstance().getSecondaryTextColour();
    if (onGenerateSuccess) onGenerateSuccess();
    refresh();
}

void PlaylistGeneratorDialog::saveTemplate() {
    auto* window = new juce::AlertWindow(TRANS("gen_tpl_save_title"), TRANS("gen_tpl_save_text"), juce::AlertWindow::NoIcon, this);
    const auto current = templateCombo.getSelectedId() > defaultTemplateId ? templateCombo.getText() : juce::String();
    window->addTextEditor("name", current, TRANS("gen_tpl_name"));
    window->addButton(TRANS("gen_tpl_save_ok"), 1, juce::KeyPress(juce::KeyPress::returnKey));
    window->addButton(TRANS("cancel"), 0, juce::KeyPress(juce::KeyPress::escapeKey));
    juce::Component::SafePointer<PlaylistGeneratorDialog> safe(this);
    window->enterModalState(true, juce::ModalCallbackFunction::create([safe, window](int result) {
        if (safe == nullptr || result != 1) return;
        const auto name = window->getTextEditorContents("name").trim();
        if (name.isEmpty()) return;
        safe->templateManager.saveTemplate(name, safe->settings);
        safe->refreshTemplates(name);
        safe->resultText = TRANS("gen_template_saved") + " " + name;
        safe->resultColour = ThemeManager::getInstance().getSecondaryTextColour();
        safe->refresh();
    }), true);
}

void PlaylistGeneratorDialog::deleteTemplate() {
    if (templateCombo.getSelectedId() <= defaultTemplateId) return;
    const auto name = templateCombo.getText();
    juce::Component::SafePointer<PlaylistGeneratorDialog> safe(this);
    juce::AlertWindow::showOkCancelBox(juce::AlertWindow::QuestionIcon, TRANS("gen_tpl_delete"),
        TRANS("delete_template_confirm") + " \"" + name + "\"?", TRANS("gen_tpl_delete"), TRANS("cancel"), this,
        juce::ModalCallbackFunction::create([safe, name](int result) {
            if (safe == nullptr || result != 1) return;
            safe->templateManager.deleteTemplate(name);
            safe->refreshTemplates();  // los ajustes quedan como están
            safe->resultText = TRANS("gen_template_deleted") + " " + name;
            safe->resultColour = ThemeManager::getInstance().getSecondaryTextColour();
            safe->refresh();
        }));
}

//==============================================================================
// Lista de categorías del modo avanzado: nombre, cuántas canciones, e Incluir / Excluir.

int PlaylistGeneratorDialog::getNumRows() { return (int) allCategories.size(); }

void PlaylistGeneratorDialog::rowChipAreas(int width, int height, juce::Rectangle<int>& include, juce::Rectangle<int>& exclude) {
    auto row = juce::Rectangle<int>(width, height).reduced(8, 5);
    exclude = row.removeFromRight(excludeChipW);
    row.removeFromRight(6);
    include = row.removeFromRight(includeChipW);
}

void PlaylistGeneratorDialog::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool) {
    if (row < 0 || row >= (int) allCategories.size()) return;
    auto& t = ThemeManager::getInstance();
    const auto& category = allCategories[(size_t) row];
    const auto& inc = settings.includedCategories;
    const auto& exc = settings.excludedCategories;
    const bool included = std::find(inc.begin(), inc.end(), category.id) != inc.end();
    const bool excluded = std::find(exc.begin(), exc.end(), category.id) != exc.end();

    const auto tone = included ? t.getSuccessColour() : excluded ? t.getErrorColour() : juce::Colour();
    if (!tone.isTransparent()) {
        g.setColour(tone.withAlpha(0.10f));
        g.fillRect(0, 0, w, h);
        g.setColour(tone);
        g.fillRect(0, 0, 3, h);
    } else if (row % 2) {
        g.fillAll(t.getPrimaryTextColour().withAlpha(0.025f));
    }

    juce::Rectangle<int> includeArea, excludeArea;
    rowChipAreas(w, h, includeArea, excludeArea);
    auto chip = [&](juce::Rectangle<int> area, const juce::String& text, bool on, juce::Colour colour) {
        const auto r = area.toFloat();
        if (on) {
            g.setColour(colour.withAlpha(0.22f));
            g.fillRoundedRectangle(r, r.getHeight() / 2.0f);
        }
        g.setColour(on ? colour : t.getStrongOutlineColour());
        g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() / 2.0f, 1.0f);
        g.setColour(on ? colour.interpolatedWith(t.getPrimaryTextColour(), 0.2f) : t.getMutedTextColour());
        g.setFont(UiStyle::ui(12.0f, on));
        g.drawText(text, area, juce::Justification::centred, false);
    };
    chip(includeArea, TRANS("gen_include"), included, t.getSuccessColour());
    chip(excludeArea, TRANS("gen_exclude"), excluded, t.getErrorColour());

    auto text = juce::Rectangle<int>(w, h).withTrimmedLeft(12).withRight(includeArea.getX() - 8);
    const int count = songsInCategory.count(category.id) > 0 ? songsInCategory.at(category.id) : 0;
    g.setColour(t.getMutedTextColour());
    g.setFont(UiStyle::mono(12.0f));
    g.drawText(juce::String(count), text.removeFromRight(46), juce::Justification::centredRight, false);
    g.setColour(excluded ? t.getMutedTextColour() : category.isSystem() ? t.getSecondaryTextColour() : t.getPrimaryTextColour());
    g.setFont(included ? UiStyle::semibold(14.0f) : UiStyle::ui(14.0f));
    g.drawText(MusicLibrary::getDisplayName(category), text, juce::Justification::centredLeft, true);
}

void PlaylistGeneratorDialog::listBoxItemClicked(int row, const juce::MouseEvent& e) {
    if (row < 0 || row >= (int) allCategories.size()) return;
    const int id = allCategories[(size_t) row].id;
    juce::Rectangle<int> includeArea, excludeArea;
    rowChipAreas(categoryList.getVisibleRowWidth(), categoryList.getRowHeight(), includeArea, excludeArea);
    // "Excluir" (o el clic derecho, como antes) la excluye; el resto de la fila la incluye.
    if (excludeArea.expanded(3).contains(e.getPosition()) || e.mods.isPopupMenu()) toggleExcluded(id);
    else toggleIncluded(id);
}

//==============================================================================
// Dibujo y medidas

void PlaylistGeneratorDialog::paint(juce::Graphics& g) {
    auto& t = ThemeManager::getInstance();
    g.fillAll(t.getBaseBackgroundColour());

    for (const auto& text : paintedTexts) {
        if (text.style == 0) {
            UiStyle::drawSectionTitle(g, text.area, text.text);
            continue;
        }
        g.setColour(text.style == 1 ? t.getSecondaryTextColour() : t.getMutedTextColour());
        g.setFont(UiStyle::ui(text.style == 1 ? 14.0f : 13.0f));
        g.drawFittedText(text.text, text.area, juce::Justification::centredLeft, 1, 0.85f);
    }

    // Cuánto hay en la cola y, si se va a reemplazar, que se pierde.
    const bool replacing = settings.insertionMode == InsertionMode::Replace && queueCount > 0;
    g.setColour(replacing ? t.getWarningColour() : t.getMutedTextColour());
    g.setFont(UiStyle::ui(13.0f));
    g.drawFittedText(queueCount == 0 ? TRANS("gen_queue_empty")
                                     : TRANS(replacing ? "gen_queue_replace" : "gen_queue_now")
                                           .replace("%1", juce::String(queueCount)).replace("%2", UiStyle::formatLongDuration(queueSeconds)),
                     whereHintArea, juce::Justification::centredLeft, 1, 0.85f);

    // El resumen.
    UiStyle::drawPanel(g, summaryArea.toFloat(), summaryBorder.isTransparent() ? juce::Colour() : summaryBorder.withAlpha(0.7f));
    auto lines = summaryArea.reduced(14, 10);
    UiStyle::drawSectionTitle(g, lines.removeFromTop(titleH), TRANS("gen_summary_title"));
    lines.removeFromTop(4);
    for (size_t i = 0; i < summaryLines.size(); ++i) {
        auto row = lines.removeFromTop(20);
        // La línea de las reglas deja lugar al botón "Cambiar reglas".
        if (changeRulesButton.isVisible() && row.getY() == changeRulesButton.getY() + (changeRulesButton.getHeight() - 20) / 2)
            row.removeFromRight(changeRulesButton.getWidth() + 8);
        g.setColour(summaryLines[i].colour);
        g.setFont(summaryLines[i].strong ? UiStyle::semibold(14.5f) : UiStyle::ui(13.5f));
        g.drawFittedText(summaryLines[i].text, row, juce::Justification::centredLeft, 1, 0.8f);
    }

    // Lo último que pasó.
    g.setColour(resultColour);
    g.setFont(UiStyle::ui(13.5f));
    g.drawFittedText(resultText, footerTextArea, juce::Justification::centredLeft, 2, 0.85f);
}

void PlaylistGeneratorDialog::resized() {
    paintedTexts.clear();
    auto title = [this](juce::Rectangle<int> area, const juce::String& text) { paintedTexts.push_back({ area, text, 0 }); };
    auto label = [this](juce::Rectangle<int> area, const juce::String& text, int style = 1) { paintedTexts.push_back({ area, text, style }); };

    auto area = getLocalBounds().reduced(padding);

    // Arriba: modo a la izquierda, plantillas a la derecha.
    auto top = area.removeFromTop(34);
    simpleModeButton.setBounds(top.removeFromLeft(130));
    advancedModeButton.setBounds(top.removeFromLeft(130));
    templateCombo.setBounds(top.removeFromRight(juce::jmin(230, top.getWidth() / 2)));
    if (advanced) {
        top.removeFromRight(8);
        deleteTemplateButton.setBounds(top.removeFromRight(deleteTemplateButton.getBestWidthForHeight(34) + 18));
        top.removeFromRight(4);
        saveTemplateButton.setBounds(top.removeFromRight(saveTemplateButton.getBestWidthForHeight(34) + 24));
    }
    area.removeFromTop(16);

    // Pie: lo último que pasó y las acciones.
    auto footer = area.removeFromBottom(38);
    area.removeFromBottom(12);
    generateButton.setBounds(footer.removeFromRight(juce::jmax(190, generateButton.getBestWidthForHeight(38) + 40)));
    footer.removeFromRight(8);
    closeButton.setBounds(footer.removeFromRight(closeButton.getBestWidthForHeight(38) + 26));
    if (undoButton.isVisible()) {
        footer.removeFromRight(8);
        undoButton.setBounds(footer.removeFromRight(undoButton.getBestWidthForHeight(34) + 28).withSizeKeepingCentre(
            undoButton.getBestWidthForHeight(34) + 28, 34));
    }
    footer.removeFromRight(12);
    footerTextArea = footer;

    // Resumen: tan alto como sus líneas.
    summaryArea = area.removeFromBottom(titleH + 4 + juce::jmax(3, (int) summaryLines.size()) * 20 + 22);
    area.removeFromBottom(12);
    if (changeRulesButton.isVisible()) {
        // A la derecha de la línea de las reglas (la que empieza con "Reglas").
        int rulesLine = 0;
        for (size_t i = 0; i < summaryLines.size(); ++i)
            if (summaryLines[i].text == rulesSentence()) rulesLine = (int) i;
        const int w = changeRulesButton.getBestWidthForHeight(24) + 20;
        changeRulesButton.setBounds(summaryArea.getRight() - 14 - w, summaryArea.getY() + 10 + titleH + 4 + rulesLine * 20 - 2, w, 24);
    }

    // Dónde ponerla: tres opciones y, al lado, cuánto hay en la cola.
    const int whereHeight = titleH + 6 + rowH;
    auto layoutWhere = [&](juce::Rectangle<int> where) {
        title(where.removeFromTop(titleH), TRANS("gen_q_where"));
        where.removeFromTop(6);
        auto whereRow = where.removeFromTop(rowH);
        const int whereW = juce::jmin(210, (whereRow.getWidth() - 20) / 3);
        whereEndButton.setBounds(whereRow.removeFromLeft(whereW));
        whereStartButton.setBounds(whereRow.removeFromLeft(whereW));
        whereReplaceButton.setBounds(whereRow.removeFromLeft(whereW));
        whereHintArea = whereRow.withTrimmedLeft(16);
    };

    if (!advanced) {
        //----------------------------------------------------------------------
        // Rápido: las preguntas, una debajo de otra (lo que sobra queda sobre el resumen).
        title(area.removeFromTop(titleH), TRANS("gen_q_duration"));
        area.removeFromTop(6);
        auto chips = area.removeFromTop(rowH);
        const int chipW = (chips.getWidth() - 8 * (durationChips.size() - 1)) / juce::jmax(1, durationChips.size());
        bool anyOn = false;
        for (auto* chip : durationChips) {
            chip->setBounds(chips.removeFromLeft(chipW));
            chips.removeFromLeft(8);
            anyOn = anyOn || chip->getToggleState();
        }
        durationNoteArea = area.removeFromTop(anyOn ? 0 : 20);
        if (!anyOn && settings.targetDurationVal > 0)
            label(durationNoteArea, TRANS("gen_dur_other").replace("%1", niceDuration(settings.targetSeconds())), 2);
        area.removeFromTop(18);

        // Las categorías ocupan lo que necesitan; si son muchas, se desplazan.
        title(area.removeFromTop(titleH), TRANS("gen_q_music"));
        area.removeFromTop(6);
        const int below = 14 + rowH + 18 + whereHeight + 8;
        const int flowWidth = area.getWidth() - categoryViewport.getScrollBarThickness() - 2;
        const int flowHeight = juce::jmax(1, categoryFlow.layout(flowWidth));
        categoryFlow.setSize(flowWidth, flowHeight);
        categoryViewport.setBounds(area.removeFromTop(juce::jlimit(rowH, juce::jmax(rowH, area.getHeight() - below), flowHeight)));
        area.removeFromTop(14);

        auto separators = area.removeFromTop(rowH);
        const int toggleW = juce::jmin(separators.getWidth() - 200, 250);
        separatorToggle.setBounds(separators.removeFromLeft(toggleW));
        separatorEveryCombo.setBounds(separators.removeFromLeft(74));
        separators.removeFromLeft(10);
        const int sweepers = sweepersCategoryId != -1 && songsInCategory.count(sweepersCategoryId) > 0 ? songsInCategory[sweepersCategoryId] : 0;
        label(separators, TRANS("gen_sep_songs") + "   " + (sweepers > 0 || settings.usesSeparators()
                              ? juce::String() : "(" + TRANS("gen_sep_empty") + ")"), 1);
        area.removeFromTop(18);

        layoutWhere(area.removeFromTop(whereHeight));
        return;
    }

    //--------------------------------------------------------------------------
    // Avanzado: categorías a la izquierda; duración, reglas y separadores a la derecha;
    // dónde ponerla, abajo.
    layoutWhere(area.removeFromBottom(whereHeight));
    area.removeFromBottom(16);
    auto left = area.removeFromLeft(area.getWidth() * 46 / 100);
    area.removeFromLeft(24);
    title(left.removeFromTop(titleH), TRANS("gen_adv_categories"));
    left.removeFromTop(6);
    label(left.removeFromBottom(20), TRANS("gen_adv_cat_hint"), 2);
    left.removeFromBottom(2);
    categoryList.setBounds(left);

    auto right = area;
    title(right.removeFromTop(titleH), TRANS("gen_adv_duration"));
    right.removeFromTop(6);
    auto duration = right.removeFromTop(rowH);
    durEditor.setBounds(duration.removeFromLeft(84));
    duration.removeFromLeft(8);
    durUnitCombo.setBounds(duration.removeFromLeft(130));
    right.removeFromTop(12);

    auto rulesTitle = right.removeFromTop(titleH + 6);
    resetRulesButton.setBounds(rulesTitle.removeFromRight(resetRulesButton.getBestWidthForHeight(24) + 16).withHeight(24).translated(0, -3));
    title(rulesTitle.withHeight(titleH), TRANS("gen_adv_rules"));
    auto rule = [&](const juce::String& text, juce::TextEditor& editor, const juce::String& unit) {
        auto row = right.removeFromTop(30);
        label(row.removeFromRight(84).withTrimmedLeft(8), unit, 2);
        editor.setBounds(row.removeFromRight(64));
        label(row, text);
        right.removeFromTop(5);
    };
    rule(TRANS("gen_rule_artist"), artistSepEditor, TRANS("gen_sep_songs"));
    rule(TRANS("gen_rule_title"), titleSepEditor, TRANS("gen_sep_songs"));
    rule(TRANS("gen_rule_category"), catSepEditor, TRANS("gen_sep_songs"));
    rule(TRANS("gen_rule_last"), lastPlayEditor, TRANS("gen_unit_min").toLowerCase());
    right.removeFromTop(7);

    title(right.removeFromTop(titleH), TRANS("gen_adv_separators"));
    right.removeFromTop(6);
    auto separators = right.removeFromTop(rowH);
    label(separators.removeFromRight(84).withTrimmedLeft(8), TRANS("gen_sep_songs"), 2);
    sepIntEditor.setBounds(separators.removeFromRight(64));
    label(separators.removeFromRight(50).withTrimmedLeft(10), TRANS("gen_sep_every"), 2);
    sepCatCombo.setBounds(separators);
}
