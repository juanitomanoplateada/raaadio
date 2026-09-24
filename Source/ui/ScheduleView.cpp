#include "ScheduleView.h"
#include "../autodj/AdScheduler.h"
#include "../autodj/QueueTiming.h"
#include "../helpers/ThemeManager.h"
#include "../helpers/UiStyle.h"

enum Columns { colTime = 1, colAssigned, colAssign, colInsert };

ScheduleView::ScheduleView(MusicLibrary& lib, AdvertisingManager& ads)
    : library(lib), adsManager(ads)
{
    addAndMakeVisible(autoToggle);
    autoToggle.setToggleState(adsManager.isAutoScheduleEnabled(), juce::dontSendNotification);
    autoToggle.onClick = [this] { adsManager.setAutoScheduleEnabled(autoToggle.getToggleState()); };

    addAndMakeVisible(inputLabel);
    inputLabel.setJustificationType(juce::Justification::centredLeft);
    inputLabel.setBorderSize({ 0, 0, 0, 0 });
    inputLabel.setFont(UiStyle::ui(13.5f));

    addAndMakeVisible(numberInput);
    numberInput.setInputRestrictions(2, "0123456789");
    numberInput.setJustification(juce::Justification::centred);
    numberInput.setFont(UiStyle::mono(15.0f, true));
    numberInput.onTextChange = [this] { table.repaint(); };

    addAndMakeVisible(table);
    table.setModel(this);
    UiStyle::styleTable(table);

    table.getHeader().addColumn("", colTime, 64, 56, 90, juce::TableHeaderComponent::notResizableOrSortable);
    table.getHeader().addColumn("", colAssigned, 200, 60, -1, juce::TableHeaderComponent::notSortable);
    table.getHeader().addColumn("", colAssign, 76, 60, 90, juce::TableHeaderComponent::notResizableOrSortable);
    table.getHeader().addColumn("", colInsert, 132, 40, 150, juce::TableHeaderComponent::notResizableOrSortable);

    buildTimeSlots();
    adsManager.addChangeListener(this);
    lookAndFeelChanged();
}

ScheduleView::~ScheduleView()
{
    adsManager.removeChangeListener(this);
}

void ScheduleView::buildTimeSlots()
{
    for (int m = 0; m < 24 * 60; m += 30)
        timeSlots.add(juce::String::formatted("%02d:%02d", m / 60, m % 60));
    table.updateContent();
}

void ScheduleView::changeListenerCallback(juce::ChangeBroadcaster*)
{
    autoToggle.setToggleState(adsManager.isAutoScheduleEnabled(), juce::dontSendNotification);
    table.repaint();
}

void ScheduleView::setSelectedCueNumber(int number)
{
    if (number != -1)
        numberInput.setText(juce::String(number), juce::dontSendNotification);
    else
        numberInput.clear();

    table.repaint();
}

int ScheduleView::getInputNumber() const
{
    juce::String t = numberInput.getText().trim();
    if (t.isEmpty()) return -1;
    return t.getIntValue();
}

void ScheduleView::paint(juce::Graphics& g)
{
    UiStyle::drawSectionTitle(g, titleArea, TRANS("schedule_title"));
}

void ScheduleView::lookAndFeelChanged() {
    autoToggle.setButtonText(TRANS("schedule_auto"));
    autoToggle.setTooltip(TRANS("tip_schedule_auto"));
    inputLabel.setText(TRANS("label_cue_input"), juce::dontSendNotification);
    inputLabel.setColour(juce::Label::textColourId, ThemeManager::getInstance().getSecondaryTextColour());
    numberInput.setTooltip(TRANS("tip_cue_input"));

    auto& header = table.getHeader();
    header.setColumnName(colTime, TRANS("header_time"));
    header.setColumnName(colAssigned, TRANS("header_assigned_anuncios"));
    header.setColumnName(colAssign, TRANS("header_assign"));
    header.setColumnName(colInsert, "");

    table.setColour(juce::ListBox::backgroundColourId, ThemeManager::getInstance().getPanelBackgroundColour());
    table.updateContent();
    table.repaint();
    resized();  // los anchos dependen de los textos (cambian con el idioma)
}

void ScheduleView::resized()
{
    auto area = getLocalBounds().withTrimmedTop(UiStyle::gap);

    // Fila 1: título y pauta automática. Fila 2: el número de cuña que se programa. Así
    // nada se corta en un panel angosto (pantallas de 1366 px).
    auto titleRow = area.removeFromTop(UiStyle::controlHeight);
    const int toggleWidth = (int) juce::GlyphArrangement::getStringWidth(UiStyle::ui(14.0f), autoToggle.getButtonText()) + 40;
    autoToggle.setBounds(titleRow.removeFromRight(juce::jmin(toggleWidth, titleRow.getWidth() / 2 + 40)));
    titleArea = titleRow;

    area.removeFromTop(6);
    auto inputRow = area.removeFromTop(UiStyle::controlHeight);
    const int labelWidth = (int) juce::GlyphArrangement::getStringWidth(UiStyle::ui(13.5f), inputLabel.getText()) + 12;
    inputLabel.setBounds(inputRow.removeFromLeft(juce::jmin(labelWidth, inputRow.getWidth() - 64)));
    numberInput.setBounds(inputRow.removeFromLeft(56));

    area.removeFromTop(UiStyle::gap);
    table.setBounds(area);

    // Columnas: la de "Insertar tanda" queda solo con su icono si no cabe con el texto.
    const int available = table.getWidth() - table.getViewport()->getScrollBarThickness();
    const bool compact = available < 420;
    if (compact != compactInsert) {
        compactInsert = compact;
        table.updateContent();
    }
    const int timeWidth = 64, assignWidth = compact ? 64 : 76, insertWidth = compact ? 44 : 132;
    auto& header = table.getHeader();
    header.setColumnWidth(colTime, timeWidth);
    header.setColumnWidth(colAssign, assignWidth);
    header.setColumnWidth(colInsert, insertWidth);
    header.setColumnWidth(colAssigned, juce::jmax(60, available - timeWidth - assignWidth - insertWidth));
}

int ScheduleView::getNumRows()
{
    return timeSlots.size();
}

void ScheduleView::paintRowBackground(juce::Graphics& g, int rowNumber, int, int, bool)
{
    if (rowNumber % 2)
        g.fillAll(ThemeManager::getInstance().getPrimaryTextColour().withAlpha(0.025f));
}

void ScheduleView::paintCell(juce::Graphics& g, int rowNumber, int columnId, int width, int height, bool)
{
    if (rowNumber >= timeSlots.size()) return;

    auto& t = ThemeManager::getInstance();
    const juce::String time = timeSlots[rowNumber];
    const auto area = juce::Rectangle<int>(width, height).reduced(8, 0);

    if (columnId == colTime)
    {
        g.setColour(t.getPrimaryTextColour());
        g.setFont(UiStyle::mono(13.5f, true));
        g.drawText(time, area, juce::Justification::centredLeft, false);
    }
    else if (columnId == colAssigned)
    {
        // Cada número de cuña asignado a la franja, como etiqueta.
        auto chips = area.toFloat().withSizeKeepingCentre((float) area.getWidth(), 20.0f);
        for (int num : adsManager.getNumbersForTime(time)) {
            const auto text = juce::String(num).paddedLeft('0', 2);
            if (UiStyle::getChipWidth(text, 20.0f) > chips.getWidth()) break;
            const float w = UiStyle::drawChip(g, chips, text, t.getCueOnColour().withAlpha(0.2f),
                                              t.getCueOnColour().interpolatedWith(t.getPrimaryTextColour(), 0.2f));
            chips.removeFromLeft(w + 4.0f);
        }
    }
    else if (columnId == colAssign)
    {
        const int currentInput = getInputNumber();
        const bool enabled = currentInput != -1;
        const bool assigned = enabled && adsManager.getSchedules(currentInput).contains(time);
        auto box = juce::Rectangle<float>((float) width, (float) height).withSizeKeepingCentre(18.0f, 18.0f);
        juce::LookAndFeel::getDefaultLookAndFeel().drawTickBox(g, table, box.getX(), box.getY(), box.getWidth(), box.getHeight(),
                                                              assigned, enabled, false, false);
    }
}

juce::Component* ScheduleView::refreshComponentForCell(int rowNumber, int columnId, bool, juce::Component* existingComponentToUpdate)
{
    if (columnId != colInsert)
    {
        delete existingComponentToUpdate;
        return nullptr;
    }

    auto* btn = dynamic_cast<juce::TextButton*>(existingComponentToUpdate);
    if (btn == nullptr) {
        btn = new juce::TextButton();
        UiStyle::setVariant(*btn, "ghost");
        UiStyle::setIcon(*btn, "plus");
        btn->getProperties().set("radius", 5.0f);
    }

    btn->setButtonText(compactInsert ? juce::String() : TRANS("schedule_generate"));
    btn->setTooltip(TRANS("schedule_generate") + ": " + TRANS("tip_schedule_generate"));
    btn->onClick = [this, rowNumber] { generateBlock(rowNumber); };
    return btn;
}

void ScheduleView::cellClicked(int rowNumber, int columnId, const juce::MouseEvent&)
{
    if (rowNumber >= timeSlots.size() || columnId != colAssign) return;

    const int currentInput = getInputNumber();
    if (currentInput == -1) return;

    const juce::String time = timeSlots[rowNumber];
    if (adsManager.getSchedules(currentInput).contains(time))
        adsManager.removeSchedule(currentInput, time);
    else
        adsManager.addSchedule(currentInput, time);
}

void ScheduleView::setPlaylist(Playlist* p) { playlist = p; }
void ScheduleView::setDecks(Deck* dA, Deck* dB) { deckA = dA; deckB = dB; }

void ScheduleView::generateBlock(int row)
{
    if (!playlist || row >= timeSlots.size()) return;

    const juce::String timeSlot = timeSlots[row];

    juce::Random random;
    const auto block = AdBreaks::buildBreak(library, adsManager, timeSlot, random);
    if (block.empty()) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, TRANS("schedule_title"),
                                               TRANS("msg_no_ads_for_slot") + " " + timeSlot);
        return;
    }

    // La tanda va en el punto de la cola donde empieza más cerca de la hora contratada,
    // sin cortar canciones.
    const auto now = juce::Time::getCurrentTime();
    const double untilQueue = (deckA != nullptr && deckB != nullptr) ? QueueTiming::secondsUntilQueueStarts(*deckA, *deckB) : -1.0;
    const auto queueStart = now + juce::RelativeTime(juce::jmax(0.0, untilQueue));
    const int index = QueueTiming::findInsertionIndex(playlist->getQueue(), queueStart, AdBreaks::timeOfSlot(timeSlot, now));

    playlist->insertTracks(block, index);
}
