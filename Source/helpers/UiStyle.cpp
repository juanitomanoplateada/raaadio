#include "UiStyle.h"
#include "ThemeManager.h"

namespace UiStyle
{

//==============================================================================
// Tipografía

static juce::String monoTypefaceName()
{
   #if JUCE_WINDOWS
    return "Consolas";
   #else
    return juce::Font::getDefaultMonospacedFontName();
   #endif
}

juce::Font ui(float height, bool bold)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font semibold(float height)
{
   #if JUCE_WINDOWS
    return juce::Font(juce::FontOptions("Segoe UI", height, juce::Font::plain).withStyle("Semibold"));
   #else
    return ui(height, true);
   #endif
}

juce::Font mono(float height, bool bold)
{
    return juce::Font(juce::FontOptions(monoTypefaceName(), height, bold ? juce::Font::bold : juce::Font::plain));
}

juce::Font caption(float height)
{
    return juce::Font(juce::FontOptions(height, juce::Font::bold).withKerningFactor(0.06f));
}

//==============================================================================
// Iconos: se definen en un cuadrado unitario y se escalan al área pedida.

static juce::Path stroke(const juce::Path& source, float width = 0.09f)
{
    juce::Path out;
    juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)
        .createStrokedPath(out, source);
    return out;
}

static juce::Path line(float x1, float y1, float x2, float y2)
{
    juce::Path p;
    p.startNewSubPath(x1, y1);
    p.lineTo(x2, y2);
    return p;
}

static juce::Path circularArrow(float cx, float cy, float r, float from, float to)
{
    juce::Path arc;
    arc.addCentredArc(cx, cy, r, r, 0.0f, from, to, true);
    juce::Path p = stroke(arc);

    // Punta de flecha tangente al final del arco (sentido horario).
    const float ex = cx + r * std::sin(to);
    const float ey = cy - r * std::cos(to);
    const float tx = std::cos(to), ty = std::sin(to);   // tangente
    const float nx = std::sin(to), ny = -std::cos(to);  // normal hacia fuera
    juce::Path head;
    head.addTriangle(ex + tx * 0.14f, ey + ty * 0.14f,
                     ex + nx * 0.12f, ey + ny * 0.12f,
                     ex - nx * 0.12f, ey - ny * 0.12f);
    p.addPath(head);
    return p;
}

static juce::Path unitIcon(const juce::String& name)
{
    using juce::MathConstants;
    juce::Path p;

    if (name == "play")
    {
        juce::Path t;
        t.addTriangle(0.28f, 0.17f, 0.28f, 0.83f, 0.84f, 0.5f);
        p = t.createPathWithRoundedCorners(0.06f);
    }
    else if (name == "pause")
    {
        p.addRoundedRectangle(0.22f, 0.18f, 0.2f, 0.64f, 0.05f);
        p.addRoundedRectangle(0.58f, 0.18f, 0.2f, 0.64f, 0.05f);
    }
    else if (name == "stop")
    {
        p.addRoundedRectangle(0.22f, 0.22f, 0.56f, 0.56f, 0.08f);
    }
    else if (name == "skip")
    {
        juce::Path t;
        t.addTriangle(0.16f, 0.2f, 0.16f, 0.8f, 0.62f, 0.5f);
        p = t.createPathWithRoundedCorners(0.05f);
        p.addRoundedRectangle(0.66f, 0.2f, 0.15f, 0.6f, 0.04f);
    }
    else if (name == "loop" || name == "refresh")
    {
        p = circularArrow(0.5f, 0.5f, 0.31f, 0.55f, MathConstants<float>::twoPi - 0.25f);
    }
    else if (name == "eject")
    {
        juce::Path t;
        t.addTriangle(0.5f, 0.16f, 0.84f, 0.58f, 0.16f, 0.58f);
        p = t.createPathWithRoundedCorners(0.05f);
        p.addRoundedRectangle(0.16f, 0.68f, 0.68f, 0.14f, 0.04f);
    }
    else if (name == "info")
    {
        juce::Path c;
        c.addEllipse(0.12f, 0.12f, 0.76f, 0.76f);
        p = stroke(c, 0.08f);
        p.addEllipse(0.445f, 0.27f, 0.11f, 0.11f);
        p.addRoundedRectangle(0.455f, 0.43f, 0.09f, 0.3f, 0.03f);
    }
    else if (name == "settings")
    {
        for (int i = 0; i < 8; ++i)
        {
            juce::Path tooth;
            tooth.addRoundedRectangle(0.42f, 0.05f, 0.16f, 0.22f, 0.04f);
            tooth.applyTransform(juce::AffineTransform::rotation((float) i * MathConstants<float>::pi / 4.0f, 0.5f, 0.5f));
            p.addPath(tooth);
        }
        p.addEllipse(0.19f, 0.19f, 0.62f, 0.62f);

        // Agujero central: un círculo con el sentido invertido se resta con la regla non-zero.
        juce::Path hole;
        hole.addEllipse(0.37f, 0.37f, 0.26f, 0.26f);
        hole.applyTransform(juce::AffineTransform::scale(-1.0f, 1.0f, 0.5f, 0.5f));
        p.addPath(hole);
    }
    else if (name == "folder")
    {
        juce::Path f;
        f.startNewSubPath(0.1f, 0.26f);
        f.lineTo(0.4f, 0.26f);
        f.lineTo(0.48f, 0.35f);
        f.lineTo(0.9f, 0.35f);
        f.lineTo(0.9f, 0.8f);
        f.lineTo(0.1f, 0.8f);
        f.closeSubPath();
        p = f.createPathWithRoundedCorners(0.05f);
    }
    else if (name == "record")
    {
        p.addEllipse(0.2f, 0.2f, 0.6f, 0.6f);
    }
    else if (name == "mic")
    {
        p.addRoundedRectangle(0.37f, 0.1f, 0.26f, 0.46f, 0.13f);
        juce::Path arc;
        arc.addCentredArc(0.5f, 0.44f, 0.23f, 0.23f, 0.0f, MathConstants<float>::halfPi, 1.5f * MathConstants<float>::pi, true);
        p.addPath(stroke(arc, 0.07f));
        p.addPath(stroke(line(0.5f, 0.68f, 0.5f, 0.84f), 0.07f));
        p.addPath(stroke(line(0.36f, 0.86f, 0.64f, 0.86f), 0.07f));
    }
    else if (name == "auto")
    {
        juce::Path c;
        c.addEllipse(0.1f, 0.1f, 0.8f, 0.8f);
        p = stroke(c, 0.08f);
        juce::Path t;
        t.addTriangle(0.41f, 0.32f, 0.41f, 0.68f, 0.7f, 0.5f);
        p.addPath(t.createPathWithRoundedCorners(0.03f));
    }
    else if (name == "broadcast")
    {
        p.addEllipse(0.41f, 0.41f, 0.18f, 0.18f);
        for (float r : { 0.24f, 0.4f })
        {
            juce::Path right, left;
            right.addCentredArc(0.5f, 0.5f, r, r, 0.0f, MathConstants<float>::halfPi - 0.7f, MathConstants<float>::halfPi + 0.7f, true);
            left.addCentredArc(0.5f, 0.5f, r, r, 0.0f, 1.5f * MathConstants<float>::pi - 0.7f, 1.5f * MathConstants<float>::pi + 0.7f, true);
            p.addPath(stroke(right, 0.08f));
            p.addPath(stroke(left, 0.08f));
        }
    }
    else if (name == "headphones")
    {
        juce::Path arc;
        arc.addCentredArc(0.5f, 0.56f, 0.34f, 0.34f, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
        p = stroke(arc, 0.08f);
        p.addRoundedRectangle(0.12f, 0.52f, 0.17f, 0.32f, 0.06f);
        p.addRoundedRectangle(0.71f, 0.52f, 0.17f, 0.32f, 0.06f);
    }
    else if (name == "import" || name == "drop")
    {
        p.addPath(stroke(line(0.5f, 0.12f, 0.5f, 0.58f)));
        juce::Path head;
        head.startNewSubPath(0.31f, 0.41f);
        head.lineTo(0.5f, 0.6f);
        head.lineTo(0.69f, 0.41f);
        p.addPath(stroke(head));
        juce::Path tray;
        tray.startNewSubPath(0.15f, 0.62f);
        tray.lineTo(0.15f, 0.85f);
        tray.lineTo(0.85f, 0.85f);
        tray.lineTo(0.85f, 0.62f);
        p.addPath(stroke(tray));
    }
    else if (name == "search")
    {
        juce::Path c;
        c.addEllipse(0.16f, 0.16f, 0.52f, 0.52f);
        p = stroke(c, 0.09f);
        p.addPath(stroke(line(0.62f, 0.62f, 0.84f, 0.84f), 0.11f));
    }
    else if (name == "plus")
    {
        p.addPath(stroke(line(0.5f, 0.18f, 0.5f, 0.82f), 0.1f));
        p.addPath(stroke(line(0.18f, 0.5f, 0.82f, 0.5f), 0.1f));
    }
    else if (name == "sparkle")
    {
        p.addStar({ 0.44f, 0.56f }, 4, 0.1f, 0.36f);
        p.addStar({ 0.8f, 0.2f }, 4, 0.04f, 0.14f);
    }
    else if (name == "music")
    {
        juce::Path head;
        head.addEllipse(0.16f, 0.62f, 0.3f, 0.22f);
        head.applyTransform(juce::AffineTransform::rotation(-0.35f, 0.31f, 0.73f));
        p.addPath(head);
        p.addRectangle(0.39f, 0.16f, 0.07f, 0.58f);
        juce::Path flag;
        flag.startNewSubPath(0.42f, 0.16f);
        flag.quadraticTo(0.62f, 0.2f, 0.76f, 0.38f);
        flag.lineTo(0.72f, 0.44f);
        flag.quadraticTo(0.6f, 0.32f, 0.46f, 0.3f);
        flag.closeSubPath();
        p.addPath(flag);
    }
    else if (name == "chevron-up" || name == "chevron-down")
    {
        juce::Path c;
        c.startNewSubPath(0.24f, 0.62f);
        c.lineTo(0.5f, 0.36f);
        c.lineTo(0.76f, 0.62f);
        p = stroke(c, 0.11f);
        if (name == "chevron-down")
            p.applyTransform(juce::AffineTransform::verticalFlip(1.0f));
    }
    else if (name == "close")
    {
        p.addPath(stroke(line(0.24f, 0.24f, 0.76f, 0.76f), 0.1f));
        p.addPath(stroke(line(0.76f, 0.24f, 0.24f, 0.76f), 0.1f));
    }
    else if (name == "list")
    {
        for (float y : { 0.26f, 0.5f, 0.74f })
        {
            p.addEllipse(0.12f, y - 0.06f, 0.12f, 0.12f);
            p.addPath(stroke(line(0.36f, y, 0.88f, y), 0.09f));
        }
    }
    else if (name == "history")
    {
        juce::Path c;
        c.addEllipse(0.12f, 0.12f, 0.76f, 0.76f);
        p = stroke(c, 0.08f);
        juce::Path hands;
        hands.startNewSubPath(0.5f, 0.28f);
        hands.lineTo(0.5f, 0.5f);
        hands.lineTo(0.66f, 0.6f);
        p.addPath(stroke(hands, 0.08f));
    }
    else if (name == "megaphone")
    {
        juce::Path m;
        m.startNewSubPath(0.12f, 0.4f);
        m.lineTo(0.32f, 0.4f);
        m.lineTo(0.8f, 0.16f);
        m.lineTo(0.8f, 0.84f);
        m.lineTo(0.32f, 0.6f);
        m.lineTo(0.12f, 0.6f);
        m.closeSubPath();
        p = m.createPathWithRoundedCorners(0.04f);
        p.addRoundedRectangle(0.24f, 0.6f, 0.13f, 0.24f, 0.04f);
    }
    else if (name == "lock")
    {
        juce::Path shackle;
        shackle.startNewSubPath(0.32f, 0.5f);
        shackle.lineTo(0.32f, 0.36f);
        shackle.addCentredArc(0.5f, 0.36f, 0.18f, 0.18f, 0.0f, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, false);
        shackle.lineTo(0.68f, 0.5f);
        p = stroke(shackle, 0.09f);
        p.addRoundedRectangle(0.2f, 0.46f, 0.6f, 0.42f, 0.08f);
    }
    else if (name == "grid")
    {
        for (float x : { 0.14f, 0.54f })
            for (float y : { 0.14f, 0.54f })
                p.addRoundedRectangle(x, y, 0.32f, 0.32f, 0.06f);
    }

    return p;
}

juce::Path icon(const juce::String& name, juce::Rectangle<float> area)
{
    auto p = unitIcon(name);
    const float size = juce::jmin(area.getWidth(), area.getHeight());
    p.applyTransform(juce::AffineTransform::scale(size)
                        .translated(area.getCentreX() - size * 0.5f, area.getCentreY() - size * 0.5f));
    return p;
}

void drawIcon(juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour colour)
{
    g.setColour(colour);
    g.fillPath(icon(name, area));
}

//==============================================================================

void setVariant(juce::Button& button, const juce::String& variant)
{
    button.getProperties().set("variant", variant);
    button.repaint();
}

void setIcon(juce::Button& button, const juce::String& iconName)
{
    button.getProperties().set("icon", iconName);
    button.repaint();
}

//==============================================================================

void drawPanel(juce::Graphics& g, juce::Rectangle<float> area, juce::Colour borderOverride, float borderWidth)
{
    auto& theme = ThemeManager::getInstance();
    g.setColour(theme.getPanelBackgroundColour());
    g.fillRoundedRectangle(area, panelRadius);

    const auto border = borderOverride.isTransparent() ? theme.getOutlineColour() : borderOverride;
    g.setColour(border);
    g.drawRoundedRectangle(area.reduced(borderWidth * 0.5f), panelRadius, borderWidth);
}

float getChipWidth(const juce::String& text, float height, bool withDot)
{
    auto font = caption(height * 0.5f);
    float w = juce::GlyphArrangement::getStringWidth(font, text) + height * 0.9f;
    if (withDot) w += height * 0.5f;
    return std::ceil(w);
}

float drawChip(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& text,
               juce::Colour fill, juce::Colour textColour, bool withDot)
{
    const float h = area.getHeight();
    const float w = getChipWidth(text, h, withDot);
    auto chip = area.withWidth(w);

    g.setColour(fill);
    g.fillRoundedRectangle(chip, h * 0.5f);

    auto content = chip.reduced(h * 0.45f, 0.0f);
    if (withDot)
    {
        const float d = h * 0.32f;
        g.setColour(textColour);
        g.fillEllipse(content.getX(), chip.getCentreY() - d * 0.5f, d, d);
        content.removeFromLeft(h * 0.5f);
    }

    g.setColour(textColour);
    g.setFont(caption(h * 0.5f));
    g.drawText(text, content, juce::Justification::centredLeft, false);
    return w;
}

void drawSectionTitle(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& text)
{
    g.setColour(ThemeManager::getInstance().getSecondaryTextColour());
    g.setFont(caption(12.0f));
    g.drawText(toUpper(text), area, juce::Justification::centredLeft, true);
}

void drawEmptyState(juce::Graphics& g, juce::Rectangle<int> area, const juce::String& iconName,
                    const juce::String& title, const juce::String& subtitle)
{
    auto& theme = ThemeManager::getInstance();
    const float iconSize = juce::jlimit(24.0f, 44.0f, area.getHeight() * 0.22f);
    const float blockHeight = iconSize + 12.0f + 22.0f + (subtitle.isNotEmpty() ? 20.0f : 0.0f);

    auto block = area.toFloat().withSizeKeepingCentre((float) area.getWidth(), blockHeight);

    drawIcon(g, iconName, block.removeFromTop(iconSize).withSizeKeepingCentre(iconSize, iconSize),
             theme.getMutedTextColour());
    block.removeFromTop(12.0f);

    g.setColour(theme.getSecondaryTextColour());
    g.setFont(semibold(16.0f));
    g.drawText(title, block.removeFromTop(22.0f), juce::Justification::centred, true);

    if (subtitle.isNotEmpty())
    {
        g.setColour(theme.getMutedTextColour());
        g.setFont(ui(13.5f));
        g.drawText(subtitle, block.removeFromTop(20.0f), juce::Justification::centred, true);
    }
}

//==============================================================================

juce::String toUpper(const juce::String& text)
{
    auto result = text.toUpperCase();
    //                               á      é      í      ó      ú      ñ      ü
    const juce::juce_wchar lower[] = { 0xE1, 0xE9, 0xED, 0xF3, 0xFA, 0xF1, 0xFC };
    const juce::juce_wchar upper[] = { 0xC1, 0xC9, 0xCD, 0xD3, 0xDA, 0xD1, 0xDC };
    for (size_t i = 0; i < std::size(lower); ++i)
        result = result.replaceCharacter(lower[i], upper[i]);
    return result;
}

juce::String formatTime(double seconds)
{
    const int total = juce::jmax(0, (int) seconds);
    if (total >= 3600)
        return juce::String::formatted("%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    return juce::String::formatted("%02d:%02d", total / 60, total % 60);
}

juce::String formatLongDuration(double seconds)
{
    const int totalMinutes = juce::jmax(0, (int) std::round(seconds / 60.0));
    if (totalMinutes >= 60)
        return juce::String(totalMinutes / 60) + " h " + juce::String(totalMinutes % 60).paddedLeft('0', 2) + " min";
    return juce::String(totalMinutes) + " min";
}

void styleTable(juce::TableListBox& table)
{
    table.setRowHeight(rowHeight);
    table.setHeaderHeight(headerHeight);
    table.setOutlineThickness(0);
    table.getHeader().setPopupMenuActive(false);
}

} // namespace UiStyle
