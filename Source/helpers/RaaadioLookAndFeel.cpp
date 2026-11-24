#include "RaaadioLookAndFeel.h"
#include "ThemeManager.h"
#include "UiStyle.h"

using namespace juce;

RaaadioLookAndFeel::RaaadioLookAndFeel()
{
   #if JUCE_WINDOWS
    setDefaultSansSerifTypefaceName("Segoe UI");
   #endif
    applyTheme();
}

void RaaadioLookAndFeel::applyTheme()
{
    auto& t = ThemeManager::getInstance();

    const auto bg          = t.getBaseBackgroundColour();
    const auto surface     = t.getPanelBackgroundColour();
    const auto raised      = t.getRaisedColour();
    const auto hover       = t.getHoverColour();
    const auto border      = t.getOutlineColour();
    const auto borderStrong = t.getStrongOutlineColour();
    const auto text        = t.getPrimaryTextColour();
    const auto textSecondary = t.getSecondaryTextColour();
    const auto textMuted   = t.getMutedTextColour();
    const auto accent      = t.getHighlightColour();
    const auto onAccent    = t.getContrastingTextColour(accent);

    setColourScheme({ bg, raised, surface, border, text, accent, onAccent, accent.withAlpha(0.3f), text });

    setColour(ResizableWindow::backgroundColourId, bg);
    setColour(DocumentWindow::textColourId, text);

    setColour(TextButton::buttonColourId, raised);
    setColour(TextButton::buttonOnColourId, accent);
    setColour(TextButton::textColourOffId, text);
    setColour(TextButton::textColourOnId, onAccent);
    setColour(ToggleButton::textColourId, text);
    setColour(ToggleButton::tickColourId, onAccent);
    setColour(ToggleButton::tickDisabledColourId, textMuted);

    setColour(ComboBox::backgroundColourId, raised);
    setColour(ComboBox::textColourId, text);
    setColour(ComboBox::outlineColourId, borderStrong);
    setColour(ComboBox::arrowColourId, textSecondary);
    setColour(ComboBox::focusedOutlineColourId, accent);

    setColour(PopupMenu::backgroundColourId, surface);
    setColour(PopupMenu::textColourId, text);
    setColour(PopupMenu::headerTextColourId, textSecondary);
    setColour(PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.2f));
    setColour(PopupMenu::highlightedTextColourId, text);

    setColour(TextEditor::backgroundColourId, raised);
    setColour(TextEditor::textColourId, text);
    setColour(TextEditor::highlightColourId, accent.withAlpha(0.35f));
    setColour(TextEditor::highlightedTextColourId, text);
    setColour(TextEditor::outlineColourId, borderStrong);
    setColour(TextEditor::focusedOutlineColourId, accent);
    setColour(TextEditor::shadowColourId, Colours::transparentBlack);
    setColour(CaretComponent::caretColourId, accent);

    setColour(Label::textColourId, text);
    setColour(Label::textWhenEditingColourId, text);
    setColour(Label::outlineWhenEditingColourId, accent);

    setColour(ListBox::backgroundColourId, surface);
    setColour(ListBox::outlineColourId, Colours::transparentBlack);
    setColour(ListBox::textColourId, text);

    setColour(TableHeaderComponent::textColourId, textSecondary);
    setColour(TableHeaderComponent::backgroundColourId, raised);
    setColour(TableHeaderComponent::outlineColourId, border);
    setColour(TableHeaderComponent::highlightColourId, hover);

    setColour(ScrollBar::thumbColourId, textMuted);
    setColour(ScrollBar::trackColourId, Colours::transparentBlack);
    setColour(ScrollBar::backgroundColourId, Colours::transparentBlack);

    setColour(Slider::thumbColourId, accent);
    setColour(Slider::trackColourId, borderStrong);
    setColour(Slider::backgroundColourId, raised);
    setColour(Slider::textBoxTextColourId, text);
    setColour(Slider::textBoxBackgroundColourId, raised);
    setColour(Slider::textBoxOutlineColourId, border);

    setColour(TabbedComponent::backgroundColourId, Colours::transparentBlack);
    setColour(TabbedComponent::outlineColourId, Colours::transparentBlack);
    setColour(TabbedButtonBar::tabOutlineColourId, Colours::transparentBlack);
    setColour(TabbedButtonBar::frontOutlineColourId, Colours::transparentBlack);
    setColour(TabbedButtonBar::tabTextColourId, textSecondary);
    setColour(TabbedButtonBar::frontTextColourId, text);

    setColour(AlertWindow::backgroundColourId, surface);
    setColour(AlertWindow::textColourId, text);
    setColour(AlertWindow::outlineColourId, borderStrong);

    setColour(GroupComponent::outlineColourId, border);
    setColour(GroupComponent::textColourId, textSecondary);

    setColour(ProgressBar::backgroundColourId, raised);
    setColour(ProgressBar::foregroundColourId, accent);

    setColour(TooltipWindow::backgroundColourId, raised);
    setColour(TooltipWindow::textColourId, text);
    setColour(TooltipWindow::outlineColourId, borderStrong);

    setColour(HyperlinkButton::textColourId, accent);
}

//==============================================================================
// Botones

Font RaaadioLookAndFeel::getTextButtonFont(TextButton&, int buttonHeight)
{
    return UiStyle::semibold(jlimit(12.5f, 15.0f, (float) buttonHeight * 0.42f));
}

Colour RaaadioLookAndFeel::resolveButtonFill(Button& b, Colour fromButton) const
{
    auto& t = ThemeManager::getInstance();

    if (b.getToggleState())
    {
        const auto onColour = b.findColour(TextButton::buttonOnColourId);
        if (b.getProperties()["toggleStyle"].toString() == "soft")
            return onColour.withAlpha(0.2f);
        return onColour;
    }

    const auto variant = b.getProperties()["variant"].toString();
    if (variant == "primary") return t.getHighlightColour();
    if (variant == "success") return t.getSuccessColour();
    if (variant == "warning") return t.getWarningColour();
    if (variant == "danger")  return t.getErrorColour();
    if (variant == "ghost")   return Colours::transparentBlack;
    return fromButton;
}

void RaaadioLookAndFeel::drawButtonBackground(Graphics& g, Button& b, const Colour& backgroundColour,
                                              bool isHighlighted, bool isDown)
{
    auto& t = ThemeManager::getInstance();
    auto bounds = b.getLocalBounds().toFloat().reduced(0.5f);
    const float radius = b.getProperties().getWithDefault("radius", UiStyle::controlRadius);

    auto fill = resolveButtonFill(b, backgroundColour);
    const bool soft = b.getToggleState() && b.getProperties()["toggleStyle"].toString() == "soft";
    const bool ghost = !b.getToggleState() && b.getProperties()["variant"].toString() == "ghost";

    // Pasar el puntero acerca el relleno al color del texto: aclara en el tema
    // oscuro y oscurece en el claro, sin depender del color de cada botón.
    if (b.isEnabled())
    {
        if (ghost)
            fill = isDown ? t.getHoverColour() : (isHighlighted ? t.getHoverColour().withAlpha(0.7f) : fill);
        else if (isDown)
            fill = fill.interpolatedWith(t.getPrimaryTextColour(), 0.16f);
        else if (isHighlighted)
            fill = fill.interpolatedWith(t.getPrimaryTextColour(), 0.08f);
    }
    else
    {
        fill = fill.withMultipliedAlpha(0.45f);
    }

    Path shape;
    shape.addRoundedRectangle(bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                              radius, radius,
                              !(b.isConnectedOnLeft() || b.isConnectedOnTop()),
                              !(b.isConnectedOnRight() || b.isConnectedOnTop()),
                              !(b.isConnectedOnLeft() || b.isConnectedOnBottom()),
                              !(b.isConnectedOnRight() || b.isConnectedOnBottom()));
    g.setColour(fill);
    g.fillPath(shape);

    // Solo los botones neutros llevan borde; los de color ya se distinguen por el relleno.
    Colour border = Colours::transparentBlack;
    if (soft)
        border = b.findColour(TextButton::buttonOnColourId).withAlpha(0.65f);
    else if (!b.getToggleState() && b.getProperties()["variant"].toString().isEmpty())
        border = t.getStrongOutlineColour();

    if (!border.isTransparent())
    {
        g.setColour(b.isEnabled() ? border : border.withMultipliedAlpha(0.5f));
        g.strokePath(shape, PathStrokeType(1.0f));
    }
}

void RaaadioLookAndFeel::drawIconAndText(Graphics& g, Rectangle<float> area, const String& iconName,
                                         const String& text, const Font& font, Colour colour,
                                         Justification justification)
{
    const float iconSize = jmin(area.getHeight() * 0.48f, 18.0f);
    const bool hasIcon = iconName.isNotEmpty();
    const bool hasText = text.isNotEmpty();

    if (hasIcon && !hasText)
    {
        UiStyle::drawIcon(g, iconName, area.withSizeKeepingCentre(iconSize, iconSize), colour);
        return;
    }

    const float spacing = hasIcon ? 7.0f : 0.0f;
    const float textWidth = jmin(GlyphArrangement::getStringWidth(font, text),
                                 area.getWidth() - (hasIcon ? iconSize + spacing : 0.0f));
    const float total = (hasIcon ? iconSize + spacing : 0.0f) + textWidth;

    float x = area.getX();
    if (justification.testFlags(Justification::horizontallyCentred))
        x = area.getCentreX() - total * 0.5f;
    else if (justification.testFlags(Justification::right))
        x = area.getRight() - total;

    if (hasIcon)
    {
        UiStyle::drawIcon(g, iconName, { x, area.getCentreY() - iconSize * 0.5f, iconSize, iconSize }, colour);
        x += iconSize + spacing;
    }

    g.setColour(colour);
    g.setFont(font);
    g.drawFittedText(text, Rectangle<float>(x, area.getY(), textWidth + 2.0f, area.getHeight()).toNearestInt(),
                     Justification::centredLeft, 1, 0.85f);
}

void RaaadioLookAndFeel::drawButtonText(Graphics& g, TextButton& b, bool isHighlighted, bool)
{
    auto& t = ThemeManager::getInstance();
    const auto fromButton = b.findColour(b.getToggleState() ? TextButton::buttonOnColourId : TextButton::buttonColourId);
    const auto fill = resolveButtonFill(b, fromButton);
    const bool soft = b.getToggleState() && b.getProperties()["toggleStyle"].toString() == "soft";
    const bool ghost = !b.getToggleState() && b.getProperties()["variant"].toString() == "ghost";

    Colour colour;
    if (soft)
        colour = b.findColour(TextButton::buttonOnColourId).interpolatedWith(t.getPrimaryTextColour(), 0.25f);
    else if (ghost)
        colour = isHighlighted ? t.getPrimaryTextColour() : t.getSecondaryTextColour();
    else
        colour = t.getContrastingTextColour(fill);

    if (b.getProperties().contains("textColour") && !b.getToggleState())
        colour = Colour::fromString(b.getProperties()["textColour"].toString());

    if (!b.isEnabled())
        colour = colour.withMultipliedAlpha(0.45f);

    const int pad = jmin(12, b.getHeight() / 3);
    auto area = b.getLocalBounds().toFloat().reduced((float) pad, 0.0f);
    drawIconAndText(g, area, b.getProperties()["icon"].toString(), b.getButtonText(),
                    getTextButtonFont(b, b.getHeight()), colour);
}

void RaaadioLookAndFeel::drawToggleButton(Graphics& g, ToggleButton& b, bool isHighlighted, bool isDown)
{
    const float fontSize = jmin(14.5f, (float) b.getHeight() * 0.75f);
    const float box = jmin(18.0f, (float) b.getHeight() - 6.0f);

    drawTickBox(g, b, 2.0f, ((float) b.getHeight() - box) * 0.5f, box, box,
                b.getToggleState(), b.isEnabled(), isHighlighted, isDown);

    auto colour = b.findColour(ToggleButton::textColourId);
    if (!b.isEnabled()) colour = colour.withMultipliedAlpha(0.5f);

    g.setColour(colour);
    g.setFont(UiStyle::ui(fontSize));
    g.drawFittedText(b.getButtonText(),
                     b.getLocalBounds().withTrimmedLeft(roundToInt(box) + 10).withTrimmedRight(2),
                     Justification::centredLeft, 2);
}

void RaaadioLookAndFeel::drawTickBox(Graphics& g, Component& c, float x, float y, float w, float h,
                                     bool ticked, bool isEnabled, bool isHighlighted, bool)
{
    auto& t = ThemeManager::getInstance();
    Rectangle<float> r(x, y, w, h);
    const float alpha = isEnabled ? 1.0f : 0.45f;

    if (ticked)
    {
        g.setColour(t.getHighlightColour().withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(r, 4.0f);

        Path tick;
        tick.startNewSubPath(r.getX() + w * 0.24f, r.getY() + h * 0.52f);
        tick.lineTo(r.getX() + w * 0.43f, r.getY() + h * 0.7f);
        tick.lineTo(r.getX() + w * 0.77f, r.getY() + h * 0.31f);
        g.setColour(t.getContrastingTextColour(t.getHighlightColour()).withMultipliedAlpha(alpha));
        g.strokePath(tick, PathStrokeType(jmax(1.6f, w * 0.12f), PathStrokeType::curved, PathStrokeType::rounded));
    }
    else
    {
        g.setColour(t.getRaisedColour().withMultipliedAlpha(alpha));
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour((isHighlighted ? t.getSecondaryTextColour() : t.getStrongOutlineColour()).withMultipliedAlpha(alpha));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
    }

    ignoreUnused(c);
}

//==============================================================================
// Deslizadores: faders de consola (tapa con línea central) en vez de puntos.

int RaaadioLookAndFeel::getSliderThumbRadius(Slider& s)
{
    if (s.getSliderStyle() == Slider::LinearVertical)   return 10;
    if (s.getSliderStyle() == Slider::LinearHorizontal) return 9;
    return LookAndFeel_V4::getSliderThumbRadius(s);
}

void RaaadioLookAndFeel::drawLinearSlider(Graphics& g, int x, int y, int width, int height,
                                          float sliderPos, float minSliderPos, float maxSliderPos,
                                          Slider::SliderStyle style, Slider& s)
{
    if (style != Slider::LinearVertical && style != Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, s);
        return;
    }

    ignoreUnused(minSliderPos, maxSliderPos);  // solo aplican a deslizadores de dos valores

    auto& t = ThemeManager::getInstance();
    const bool vertical = style == Slider::LinearVertical;
    const float alpha = s.isEnabled() ? 1.0f : 0.45f;
    const auto track = s.findColour(Slider::trackColourId).withMultipliedAlpha(alpha);
    const auto thumb = s.findColour(Slider::thumbColourId).withMultipliedAlpha(alpha);
    const float grooveWidth = 4.0f;

    // El recorrido útil deja un margen igual al radio de la tapa en cada extremo.
    const float inset = (float) getSliderThumbRadius(s);
    Rectangle<float> groove = vertical
        ? Rectangle<float>((float) x + (float) width * 0.5f - grooveWidth * 0.5f, (float) y + inset,
                           grooveWidth, (float) height - inset * 2.0f)
        : Rectangle<float>((float) x + inset, (float) y + (float) height * 0.5f - grooveWidth * 0.5f,
                           (float) width - inset * 2.0f, grooveWidth);
    const float startPos = vertical ? groove.getBottom() : groove.getX();

    g.setColour(t.getRaisedColour().interpolatedWith(t.getBaseBackgroundColour(), 0.5f).withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(groove.expanded(1.0f), grooveWidth);
    g.setColour(track);
    g.fillRoundedRectangle(groove, grooveWidth * 0.5f);

    // Relleno opcional: desde el mínimo ("fill" = "start") o desde el centro ("fill" = "centre").
    const auto fillMode = s.getProperties()["fill"].toString();
    if (fillMode.isNotEmpty())
    {
        const float from = fillMode == "centre"
            ? (vertical ? groove.getCentreY() : groove.getCentreX())
            : startPos;
        Rectangle<float> filled = vertical
            ? Rectangle<float>(groove.getX(), jmin(from, sliderPos), grooveWidth, std::abs(from - sliderPos))
            : Rectangle<float>(jmin(from, sliderPos), groove.getY(), std::abs(sliderPos - from), grooveWidth);
        g.setColour(thumb.withMultipliedAlpha(0.8f));
        g.fillRoundedRectangle(filled, grooveWidth * 0.5f);
    }

    // Tapa del fader
    Rectangle<float> cap = vertical
        ? Rectangle<float>(jmin((float) width - 2.0f, 34.0f), 20.0f)
        : Rectangle<float>(18.0f, jmin((float) height - 2.0f, 30.0f));
    cap.setCentre(vertical ? (float) x + (float) width * 0.5f : sliderPos,
                  vertical ? sliderPos : (float) y + (float) height * 0.5f);

    const auto capFill = t.getRaisedColour().interpolatedWith(t.getPrimaryTextColour(),
                                                             s.isMouseOverOrDragging() ? 0.16f : 0.1f);
    g.setColour(Colours::black.withAlpha(0.25f * alpha));
    g.fillRoundedRectangle(cap.translated(0.0f, 1.5f), 4.0f);
    g.setColour(capFill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(cap, 4.0f);
    g.setColour(t.getStrongOutlineColour().withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(cap.reduced(0.5f), 4.0f, 1.0f);

    g.setColour(thumb);
    if (vertical)
        g.fillRoundedRectangle(cap.withSizeKeepingCentre(cap.getWidth() - 10.0f, 2.5f), 1.0f);
    else
        g.fillRoundedRectangle(cap.withSizeKeepingCentre(2.5f, cap.getHeight() - 10.0f), 1.0f);
}

//==============================================================================

int RaaadioLookAndFeel::getDefaultScrollbarWidth() { return 10; }

void RaaadioLookAndFeel::drawScrollbar(Graphics& g, ScrollBar& sb, int x, int y, int width, int height,
                                       bool isScrollbarVertical, int thumbStartPosition, int thumbSize,
                                       bool isMouseOver, bool isMouseDown)
{
    Rectangle<float> thumb = isScrollbarVertical
        ? Rectangle<float>((float) x + 2.0f, (float) thumbStartPosition + 1.0f, (float) width - 4.0f, (float) thumbSize - 2.0f)
        : Rectangle<float>((float) thumbStartPosition + 1.0f, (float) y + 2.0f, (float) thumbSize - 2.0f, (float) height - 4.0f);

    if (thumb.isEmpty()) return;

    const auto base = sb.findColour(ScrollBar::thumbColourId);
    g.setColour(base.withAlpha(isMouseDown ? 0.85f : (isMouseOver ? 0.65f : 0.4f)));
    g.fillRoundedRectangle(thumb, jmin(thumb.getWidth(), thumb.getHeight()) * 0.5f);
}

//==============================================================================

void RaaadioLookAndFeel::fillTextEditorBackground(Graphics& g, int width, int height, TextEditor& editor)
{
    g.setColour(editor.findColour(TextEditor::backgroundColourId));
    g.fillRoundedRectangle(Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f),
                           UiStyle::controlRadius);
}

void RaaadioLookAndFeel::drawTextEditorOutline(Graphics& g, int width, int height, TextEditor& editor)
{
    const auto r = Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);

    if (editor.hasKeyboardFocus(true) && !editor.isReadOnly())
    {
        g.setColour(editor.findColour(TextEditor::focusedOutlineColourId));
        g.drawRoundedRectangle(r.reduced(1.0f), UiStyle::controlRadius, 1.6f);
    }
    else
    {
        g.setColour(editor.findColour(TextEditor::outlineColourId).withMultipliedAlpha(editor.isEnabled() ? 1.0f : 0.5f));
        g.drawRoundedRectangle(r.reduced(0.5f), UiStyle::controlRadius, 1.0f);
    }
}

void RaaadioLookAndFeel::drawComboBox(Graphics& g, int width, int height, bool,
                                      int, int, int, int, ComboBox& box)
{
    const auto r = Rectangle<float>(0.0f, 0.0f, (float) width, (float) height).reduced(0.5f);
    const float alpha = box.isEnabled() ? 1.0f : 0.5f;

    auto fill = box.findColour(ComboBox::backgroundColourId);
    if (box.isMouseOver(true))
        fill = fill.interpolatedWith(ThemeManager::getInstance().getPrimaryTextColour(), 0.06f);

    g.setColour(fill.withMultipliedAlpha(alpha));
    g.fillRoundedRectangle(r, UiStyle::controlRadius);

    g.setColour(box.findColour(box.hasKeyboardFocus(true) ? ComboBox::focusedOutlineColourId
                                                          : ComboBox::outlineColourId).withMultipliedAlpha(alpha));
    g.drawRoundedRectangle(r, UiStyle::controlRadius, 1.0f);

    UiStyle::drawIcon(g, "chevron-down",
                      Rectangle<float>((float) width - 28.0f, 0.0f, 22.0f, (float) height).withSizeKeepingCentre(13.0f, 13.0f),
                      box.findColour(ComboBox::arrowColourId).withMultipliedAlpha(alpha));
}

Font RaaadioLookAndFeel::getComboBoxFont(ComboBox&) { return UiStyle::ui(14.5f); }

void RaaadioLookAndFeel::positionComboBoxText(ComboBox& box, Label& label)
{
    label.setBounds(4, 1, box.getWidth() - 32, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

//==============================================================================

void RaaadioLookAndFeel::drawPopupMenuBackground(Graphics& g, int width, int height)
{
    g.fillAll(findColour(PopupMenu::backgroundColourId));
    g.setColour(ThemeManager::getInstance().getStrongOutlineColour());
    g.drawRect(0, 0, width, height, 1);
}

void RaaadioLookAndFeel::drawPopupMenuItem(Graphics& g, const Rectangle<int>& area,
                                           bool isSeparator, bool isActive, bool isHighlighted, bool isTicked,
                                           bool hasSubMenu, const String& text, const String& shortcutKeyText,
                                           const Drawable* icon, const Colour* textColourToUse)
{
    auto& t = ThemeManager::getInstance();

    if (isSeparator)
    {
        g.setColour(t.getOutlineColour());
        g.fillRect(area.reduced(10, 0).withSizeKeepingCentre(area.getWidth() - 20, 1));
        return;
    }

    auto r = area.reduced(4, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour(findColour(PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle(r, 4.0f);
    }

    auto colour = textColourToUse != nullptr ? *textColourToUse : findColour(PopupMenu::textColourId);
    if (!isActive) colour = colour.withMultipliedAlpha(0.4f);

    auto content = r.reduced(8.0f, 0.0f);
    auto iconArea = content.removeFromLeft(20.0f);

    if (icon != nullptr)
        icon->drawWithin(g, iconArea.withSizeKeepingCentre(14.0f, 14.0f), RectanglePlacement::centred, 1.0f);
    else if (isTicked)
    {
        Path tick;
        auto b = iconArea.withSizeKeepingCentre(12.0f, 12.0f);
        tick.startNewSubPath(b.getX(), b.getCentreY());
        tick.lineTo(b.getX() + b.getWidth() * 0.4f, b.getBottom() - 1.0f);
        tick.lineTo(b.getRight(), b.getY() + 1.0f);
        g.setColour(t.getHighlightColour());
        g.strokePath(tick, PathStrokeType(1.8f, PathStrokeType::curved, PathStrokeType::rounded));
    }

    content.removeFromLeft(6.0f);

    if (hasSubMenu)
    {
        auto arrowArea = content.removeFromRight(14.0f).withSizeKeepingCentre(7.0f, 10.0f);
        Path arrow;
        arrow.addTriangle(arrowArea.getX(), arrowArea.getY(), arrowArea.getX(), arrowArea.getBottom(),
                          arrowArea.getRight(), arrowArea.getCentreY());
        g.setColour(colour.withMultipliedAlpha(0.7f));
        g.fillPath(arrow);
    }

    g.setColour(colour);
    g.setFont(getPopupMenuFont());
    g.drawFittedText(text, content.toNearestInt(), Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setColour(colour.withMultipliedAlpha(0.6f));
        g.setFont(UiStyle::ui(13.0f));
        g.drawText(shortcutKeyText, content, Justification::centredRight, true);
    }
}

Font RaaadioLookAndFeel::getPopupMenuFont() { return UiStyle::ui(14.5f); }

void RaaadioLookAndFeel::getIdealPopupMenuItemSize(const String& text, bool isSeparator, int,
                                                   int& idealWidth, int& idealHeight)
{
    if (isSeparator)
    {
        idealWidth = 50;
        idealHeight = 9;
        return;
    }

    idealHeight = 30;
    idealWidth = roundToInt(GlyphArrangement::getStringWidth(getPopupMenuFont(), text)) + 70;
}

//==============================================================================

void RaaadioLookAndFeel::drawTableHeaderBackground(Graphics& g, TableHeaderComponent& header)
{
    auto r = header.getLocalBounds();
    g.setColour(header.findColour(TableHeaderComponent::backgroundColourId));
    g.fillRect(r);

    g.setColour(header.findColour(TableHeaderComponent::outlineColourId));
    g.fillRect(r.removeFromBottom(1));

    for (int i = header.getNumColumns(true); --i >= 0;)
    {
        const int right = header.getColumnPosition(i).getRight() - 1;
        g.fillRect(right, 8, 1, jmax(0, header.getHeight() - 16));
    }
}

void RaaadioLookAndFeel::drawTableHeaderColumn(Graphics& g, TableHeaderComponent& header, const String& columnName,
                                               int, int width, int height, bool isMouseOver,
                                               bool isMouseDown, int columnFlags)
{
    if (isMouseOver || isMouseDown)
    {
        g.setColour(header.findColour(TableHeaderComponent::highlightColourId).withAlpha(isMouseDown ? 0.9f : 0.5f));
        g.fillRect(0, 0, width - 1, height - 1);
    }

    auto area = Rectangle<int>(width, height).reduced(8, 0);

    if ((columnFlags & (TableHeaderComponent::sortedForwards | TableHeaderComponent::sortedBackwards)) != 0)
    {
        auto arrowArea = area.removeFromRight(12).toFloat().withSizeKeepingCentre(8.0f, 5.0f);
        Path arrow;
        if ((columnFlags & TableHeaderComponent::sortedForwards) != 0)
            arrow.addTriangle(arrowArea.getX(), arrowArea.getBottom(), arrowArea.getRight(), arrowArea.getBottom(),
                              arrowArea.getCentreX(), arrowArea.getY());
        else
            arrow.addTriangle(arrowArea.getX(), arrowArea.getY(), arrowArea.getRight(), arrowArea.getY(),
                              arrowArea.getCentreX(), arrowArea.getBottom());
        g.setColour(ThemeManager::getInstance().getHighlightColour());
        g.fillPath(arrow);
    }

    g.setColour(header.findColour(TableHeaderComponent::textColourId));
    g.setFont(UiStyle::caption(11.5f));
    g.drawFittedText(UiStyle::toUpper(columnName), area, Justification::centredLeft, 1);
}

//==============================================================================
// Pestañas: texto con subrayado de acento en la activa. Con la propiedad "compact" (si no caben
// todas), solo el icono; el nombre queda en la ayuda emergente.

static bool isCompactTab(TabBarButton& button)
{
    return (bool) button.getProperties()["compact"] && button.getProperties()["icon"].toString().isNotEmpty();
}

int RaaadioLookAndFeel::getTabButtonBestWidth(TabBarButton& button, int tabDepth)
{
    if (isCompactTab(button)) return 48;
    const float textWidth = GlyphArrangement::getStringWidth(getTabButtonFont(button, (float) tabDepth),
                                                             button.getButtonText().trim());
    const bool hasIcon = button.getProperties()["icon"].toString().isNotEmpty();
    return roundToInt(textWidth) + 34 + (hasIcon ? 24 : 0);
}

int RaaadioLookAndFeel::getTabButtonOverlap(int) { return 0; }

Font RaaadioLookAndFeel::getTabButtonFont(TabBarButton&, float) { return UiStyle::semibold(14.5f); }

void RaaadioLookAndFeel::drawTabButton(TabBarButton& button, Graphics& g, bool isMouseOver, bool)
{
    auto& t = ThemeManager::getInstance();
    auto area = button.getActiveArea().toFloat();
    const bool front = button.isFrontTab();

    auto colour = front ? t.getPrimaryTextColour()
                        : (isMouseOver ? t.getPrimaryTextColour().withAlpha(0.85f) : t.getSecondaryTextColour());

    drawIconAndText(g, area.reduced(12.0f, 0.0f), button.getProperties()["icon"].toString(),
                    isCompactTab(button) ? String() : button.getButtonText().trim(),
                    getTabButtonFont(button, area.getHeight()), colour);

    if (front)
    {
        g.setColour(t.getHighlightColour());
        g.fillRoundedRectangle(area.getX() + 10.0f, area.getBottom() - 3.0f, area.getWidth() - 20.0f, 3.0f, 1.5f);
    }
}

void RaaadioLookAndFeel::drawTabbedButtonBarBackground(TabbedButtonBar&, Graphics&) {}

void RaaadioLookAndFeel::drawTabAreaBehindFrontButton(TabbedButtonBar&, Graphics& g, int w, int h)
{
    g.setColour(ThemeManager::getInstance().getOutlineColour());
    g.fillRect(0, h - 1, w, 1);
}

//==============================================================================

void RaaadioLookAndFeel::drawGroupComponentOutline(Graphics& g, int w, int h, const String& text,
                                                   const Justification&, GroupComponent& group)
{
    const float titleHeight = 18.0f;
    auto box = Rectangle<float>(0.0f, titleHeight + 4.0f, (float) w, (float) h - titleHeight - 4.0f).reduced(0.5f);

    g.setColour(group.findColour(GroupComponent::outlineColourId));
    g.drawRoundedRectangle(box, UiStyle::panelRadius, 1.0f);

    g.setColour(group.findColour(GroupComponent::textColourId));
    g.setFont(UiStyle::caption(12.0f));
    g.drawText(UiStyle::toUpper(text), Rectangle<float>(2.0f, 0.0f, (float) w - 4.0f, titleHeight),
               Justification::centredLeft, true);
}

void RaaadioLookAndFeel::drawProgressBar(Graphics& g, ProgressBar& bar, int width, int height,
                                         double progress, const String& textToShow)
{
    if (progress < 0.0 || progress > 1.0)
    {
        LookAndFeel_V4::drawProgressBar(g, bar, width, height, progress, textToShow);
        return;
    }

    auto r = Rectangle<float>(0.0f, 0.0f, (float) width, (float) height);
    const float radius = jmin(r.getHeight() * 0.5f, 6.0f);

    g.setColour(bar.findColour(ProgressBar::backgroundColourId));
    g.fillRoundedRectangle(r, radius);

    g.setColour(bar.findColour(ProgressBar::foregroundColourId));
    g.fillRoundedRectangle(r.withWidth(jmax(radius * 2.0f, r.getWidth() * (float) progress)), radius);

    if (textToShow.isNotEmpty())
    {
        g.setColour(ThemeManager::getInstance().getPrimaryTextColour());
        g.setFont(UiStyle::ui(jmin(13.0f, (float) height * 0.7f), true));
        g.drawText(textToShow, r, Justification::centred, false);
    }
}

void RaaadioLookAndFeel::drawStretchableLayoutResizerBar(Graphics& g, int w, int h, bool isVerticalBar,
                                                         bool isMouseOver, bool isMouseDragging)
{
    auto& t = ThemeManager::getInstance();
    auto r = Rectangle<float>((float) w, (float) h);
    auto handle = isVerticalBar ? r.withSizeKeepingCentre(3.0f, jmin(48.0f, r.getHeight()))
                                : r.withSizeKeepingCentre(jmin(48.0f, r.getWidth()), 3.0f);

    g.setColour(isMouseOver || isMouseDragging ? t.getHighlightColour() : t.getStrongOutlineColour());
    g.fillRoundedRectangle(handle, 1.5f);
}

//==============================================================================

Font RaaadioLookAndFeel::getAlertWindowTitleFont()   { return UiStyle::semibold(18.0f); }
Font RaaadioLookAndFeel::getAlertWindowMessageFont() { return UiStyle::ui(15.0f); }
Font RaaadioLookAndFeel::getAlertWindowFont()        { return UiStyle::ui(14.5f); }
int RaaadioLookAndFeel::getAlertWindowButtonHeight() { return 34; }
