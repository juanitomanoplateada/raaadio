#include "CreditsEditor.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

namespace {
    constexpr int captionHeight = 15, fieldHeight = UiStyle::controlHeight, rowGap = 8, chipHeight = 24;

    juce::StringArray namesIn(const juce::TextEditor& editor) {
        auto names = juce::StringArray::fromTokens(editor.getText(), ",", "");
        names.trim();
        names.removeEmptyStrings();
        return names;
    }
}

CreditsEditor::CreditsEditor() {
    for (auto* label : { &artistLabel, &featuredLabel, &titleLabel, &versionLabel, &yearLabel, &composersLabel }) {
        addAndMakeVisible(*label);
        label->setBorderSize({ 0, 0, 0, 0 });
    }
    for (auto* field : fields) {
        addAndMakeVisible(*field);
        field->setIndents(10, 0);
        field->setJustification(juce::Justification::centredLeft);
        field->onReturnKey = [this] { if (onCommit) onCommit(); };
        field->onTextChange = [this, field] {
            edited = true;
            if (field == &artist) updateSuggestions();
            if (onEdited) onEdited();
        };
    }
    year.setInputRestrictions(4, "0123456789");
    // Tab recorre los campos en el orden en que se ven.
    int order = 1;
    for (auto* field : { &artist, &title, &version, &featured, &year, &composers }) field->setExplicitFocusOrder(order++);
    // Las sugerencias no quitan el foco al artista: se van cuando se sale del campo.
    artist.onFocusLost = [this] { suggestionButtons.clear(); resized(); };
    lookAndFeelChanged();
}

void CreditsEditor::setValues(const TrackNaming::Proposal& v) {
    artist.setText(v.artist, false);
    featured.setText(v.featured.joinIntoString(", "), false);
    title.setText(v.title, false);
    version.setText(v.version, false);
    year.setText(v.year > 0 ? juce::String(v.year) : juce::String(), false);
    composers.setText(v.composers.joinIntoString(", "), false);
    suggestionButtons.clear();
    edited = false;
    resized();
}

TrackNaming::Proposal CreditsEditor::getValues() const {
    TrackNaming::Proposal v;
    v.artist = artist.getText().trim();
    v.featured = namesIn(featured);
    v.title = title.getText().trim();
    v.version = version.getText().trim();
    v.year = year.getText().getIntValue();
    v.composers = namesIn(composers);
    return v;
}

void CreditsEditor::setCreditsVisible(bool visible) {
    creditsVisible = visible;
    for (auto* c : std::initializer_list<juce::Component*> { &featuredLabel, &featured, &versionLabel, &version, &yearLabel, &year,
                                                             &composersLabel, &composers })
        c->setVisible(visible);
    resized();
}

void CreditsEditor::focusArtist() {
    artist.grabKeyboardFocus();
    artist.selectAll();
}

void CreditsEditor::updateSuggestions() {
    suggestionButtons.clear();
    if (namingContext != nullptr && artist.hasKeyboardFocus(true)) {
        const auto typed = artist.getText().trim();
        for (const auto& known : namingContext->suggestions(typed, 3)) {
            if (known.name == typed) continue;
            auto* chip = suggestionButtons.add(new juce::TextButton(known.name));
            UiStyle::setVariant(*chip, "ghost");
            chip->setMouseClickGrabsKeyboardFocus(false);
            chip->setWantsKeyboardFocus(false);
            chip->setTooltip(TRANS("credits_known_artist").replace("%1", juce::String(known.tracks)));
            const auto name = known.name;
            // Después del clic: el botón se borra al elegir.
            juce::Component::SafePointer<CreditsEditor> safeEditor(this);
            chip->onClick = [safeEditor, name] {
                juce::MessageManager::callAsync([safe = safeEditor, name] {
                    if (safe == nullptr) return;
                    safe->artist.setText(name, false);
                    safe->edited = true;
                    safe->suggestionButtons.clear();
                    safe->resized();
                    safe->title.grabKeyboardFocus();
                    if (safe->onEdited) safe->onEdited();
                });
            };
            addAndMakeVisible(chip);
        }
    }
    resized();
}

int CreditsEditor::getIdealHeight() const {
    const int row = captionHeight + 3 + fieldHeight + rowGap;
    return (creditsVisible ? row * 4 : row * 2) + chipHeight + 2;
}

void CreditsEditor::resized() {
    auto area = getLocalBounds();
    auto takeRow = [&area] {
        auto row = area.removeFromTop(captionHeight + 3 + fieldHeight);
        area.removeFromTop(rowGap);
        return row;
    };
    auto place = [](juce::Rectangle<int> column, juce::Label& label, juce::TextEditor& field) {
        label.setBounds(column.removeFromTop(captionHeight));
        column.removeFromTop(3);
        field.setBounds(column.removeFromTop(fieldHeight));
    };

    place(takeRow(), artistLabel, artist);
    // Sugerencias bajo el artista (el espacio queda reservado: los campos no saltan).
    auto chips = area.removeFromTop(chipHeight);
    area.removeFromTop(2);
    for (auto* chip : suggestionButtons) {
        const int w = juce::jmin(chips.getWidth(), chip->getBestWidthForHeight(chipHeight) + 16);
        if (w < 40) break;
        chip->setBounds(chips.removeFromLeft(w));
        chips.removeFromLeft(6);
    }

    if (!creditsVisible) {
        place(takeRow(), titleLabel, title);
        return;
    }

    // Título con su versión al lado, invitados con el año, y los compositores.
    auto row = takeRow();
    auto side = row.removeFromRight(juce::jmin(140, row.getWidth() / 3));
    row.removeFromRight(10);
    place(row, titleLabel, title);
    place(side, versionLabel, version);

    row = takeRow();
    side = row.removeFromRight(juce::jmin(90, row.getWidth() / 4));
    row.removeFromRight(10);
    place(row, featuredLabel, featured);
    place(side, yearLabel, year);

    place(takeRow(), composersLabel, composers);
}

void CreditsEditor::lookAndFeelChanged() {
    auto& t = ThemeManager::getInstance();
    artistLabel.setText(TRANS("credits_artist"), juce::dontSendNotification);
    featuredLabel.setText(TRANS("credits_featured"), juce::dontSendNotification);
    titleLabel.setText(TRANS("credits_title"), juce::dontSendNotification);
    versionLabel.setText(TRANS("credits_version"), juce::dontSendNotification);
    yearLabel.setText(TRANS("credits_year"), juce::dontSendNotification);
    composersLabel.setText(TRANS("credits_composers"), juce::dontSendNotification);
    for (auto* label : { &artistLabel, &featuredLabel, &titleLabel, &versionLabel, &yearLabel, &composersLabel }) {
        label->setFont(UiStyle::caption(12.0f));
        label->setColour(juce::Label::textColourId, t.getSecondaryTextColour());
    }

    for (auto* field : fields) field->applyFontToAllText(UiStyle::ui(14.5f));
    artist.applyFontToAllText(UiStyle::semibold(15.5f));
    title.applyFontToAllText(UiStyle::semibold(15.5f));
    featured.setTextToShowWhenEmpty(TRANS("credits_names_hint"), t.getMutedTextColour());
    composers.setTextToShowWhenEmpty(TRANS("credits_names_hint"), t.getMutedTextColour());
    version.setTextToShowWhenEmpty(TRANS("credits_version_hint"), t.getMutedTextColour());
    repaint();
}
