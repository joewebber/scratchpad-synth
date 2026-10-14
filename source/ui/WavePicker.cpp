#include "ui/WavePicker.h"

#include "ui/LookAndFeel.h"
#include "ui/WaveGlyphs.h"

namespace scratchpad::ui
{

namespace
{
    constexpr int cardWidth = 236;
    constexpr int cardGap = 24;
    constexpr int cardPad = 20;
    constexpr int cardMargin = 48;     // room around a card for its lifted shadow
    constexpr int cardLift = 4;        // the chosen card rises by this much
    constexpr int descLineHeight = 18;
    constexpr int descMinHeight = 36;
    constexpr int buttonHeight = 42;
    constexpr int panelPadTop = 40, panelPadBottom = 28;
    constexpr int sectionGap = 30;
    constexpr int cardAboveDescription = 263; // padding, header, wave, harmonics and the gaps between them
    constexpr int cardFixedHeight = cardAboveDescription + 14 + buttonHeight + cardPad; // all but the description
    constexpr float descFontSize = 13.0f;

    /** A button that reports keyboard focus changes. */
    class FocusButton final : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void (bool)> onFocusChange;

        void focusGained (FocusChangeType t) override
        {
            juce::TextButton::focusGained (t);
            if (onFocusChange) onFocusChange (true);
        }

        void focusLost (FocusChangeType t) override
        {
            juce::TextButton::focusLost (t);
            if (onFocusChange) onFocusChange (false);
        }
    };
}

//==============================================================================
class WavePicker::Card final : public juce::Component
{
public:
    Card (WavePicker& ownerPicker, int index)
        : owner (ownerPicker), wave ((dsp::Wave) index), waveIndex (index),
          descLines (wrapLines (uiFont (descFontSize), waveDescription (wave), (float) (cardWidth - 2 * cardPad)))
    {
        setTitle (waveName (wave));
        setDescription (waveDescription (wave));

        choose.setTitle ("Start with " + waveName (wave).toLowerCase());
        choose.setPaintingIsUnclipped (true);
        choose.getProperties().set (buttonProps::radius, 14.0);
        choose.getProperties().set (buttonProps::tracking, 0.04);
        choose.getProperties().set (buttonProps::fontSize, 14.0);
        choose.onClick = [this]
        {
            if (owner.onChoose)
                owner.onChoose (waveIndex);
        };

        // Hover, keyboard focus and the button all preview the wave.
        choose.onFocusChange = [this] (bool focused)
        {
            if (focused)
            {
                owner.setHighlighted (waveIndex);
                owner.startPreview (waveIndex);
            }
            else
            {
                owner.stopPreview (waveIndex);
            }
        };

        addAndMakeVisible (choose);
        addMouseListener (this, true);
    }

    juce::TextButton& getButton() noexcept { return choose; }

    /** Height of the card's visible face for this wave's description. */
    int getVisibleHeight() const noexcept { return cardFixedHeight + descHeight(); }

    int descHeight() const noexcept
    {
        return juce::jmax (descMinHeight, descLines.size() * descLineHeight);
    }

    /** The visible face, in this component's coordinates (shadow margin removed, lift applied). */
    juce::Rectangle<float> face (int height) const
    {
        const bool on = owner.highlighted == waveIndex;
        return juce::Rectangle<float> ((float) cardMargin, (float) cardMargin + (on ? -(float) cardLift : 0.0f),
                                       (float) cardWidth, (float) height);
    }

    // The shadow margin is not part of the card for mouse purposes.
    bool hitTest (int x, int y) override
    {
        return face (visibleHeight).expanded (0.0f, (float) cardLift).contains ((float) x, (float) y);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = paletteOf (*this);
        const bool on = owner.highlighted == waveIndex;
        const auto card = face (visibleHeight);

        drawRaised (g, card, 22.0f, p, on ? Elevation::lifted : Elevation::small, p.surface);

        if (on)
        {
            g.setColour (p.srcInk);
            g.drawRoundedRectangle (card.expanded (1.0f), 23.0f, 2.0f);
        }

        auto inner = card.reduced ((float) cardPad);

        auto header = inner.removeFromTop (28.0f);
        g.setColour (p.ink);
        g.setFont (uiFont (24.0f));
        g.drawText (waveName (wave), header, juce::Justification::centredLeft, false);
        drawCaption (g, waveTag (wave), header.translated (0.0f, 3.0f), p.mute, juce::Justification::centredRight);

        inner.removeFromTop (14.0f);
        const auto waveWell = inner.removeFromTop (100.0f);
        drawInset (g, waveWell, 14.0f, p, p.well);

        const float dashes[] = { 2.0f, 4.0f };
        g.setColour (p.track);
        g.drawDashedLine ({ waveWell.getX() + 8.0f, waveWell.getCentreY(), waveWell.getRight() - 8.0f, waveWell.getCentreY() },
                          dashes, 2, 1.0f);
        g.setColour (p.srcInk);
        g.strokePath (wavePath (wave, waveWell.reduced (10.0f, 0.0f), 2.0f, 0.64f),
                      juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        inner.removeFromTop (14.0f);
        drawCaption (g, "Harmonics", inner.removeFromTop (13.0f), p.mute);
        inner.removeFromTop (6.0f);
        const auto harmonicsWell = inner.removeFromTop (54.0f);
        drawInset (g, harmonicsWell, 12.0f, p, p.well);
        drawHarmonics (g, harmonicsWell, wave, 12, p.srcInk);

        inner.removeFromTop (14.0f);
        g.setColour (p.ink2);
        g.setFont (uiFont (descFontSize));
        drawLines (g, descLines, inner.getPosition(), inner.getWidth(), (float) descLineHeight);
    }

    void setVisibleHeight (int h)
    {
        visibleHeight = h;
        layoutButton();
    }

    void updateButton()
    {
        const bool on = owner.highlighted == waveIndex;
        choose.setButtonText (on ? "Start with " + waveName (wave).toLowerCase() : "Choose");
        choose.setToggleState (on, juce::dontSendNotification);
        choose.setColour (juce::TextButton::buttonOnColourId, paletteOf (*this).accent);
        layoutButton();
        repaint();
    }

    void mouseEnter (const juce::MouseEvent&) override
    {
        owner.setHighlighted (waveIndex);
        owner.startPreview (waveIndex);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (! isMouseOver (true) && ! choose.hasKeyboardFocus (false))
            owner.stopPreview (waveIndex);
    }

private:
    void layoutButton()
    {
        const auto card = face (visibleHeight);
        const int y = juce::roundToInt (card.getY()) + cardAboveDescription + descHeight() + 14;
        choose.setBounds (cardMargin + cardPad, y, cardWidth - 2 * cardPad, buttonHeight);
    }

    WavePicker& owner;
    dsp::Wave wave;
    int waveIndex;
    juce::StringArray descLines;
    int visibleHeight = cardFixedHeight + descMinHeight;
    FocusButton choose { "Choose" };
};

//==============================================================================
WavePicker::WavePicker()
{
    setTitle ("Wave picker");
    setFocusContainerType (FocusContainerType::focusContainer);
    setPaintingIsUnclipped (true);
    setInterceptsMouseClicks (false, true);

    for (int i = 0; i < dsp::numWaves; ++i)
    {
        cards[(size_t) i] = std::make_unique<Card> (*this, i);
        addAndMakeVisible (*cards[(size_t) i]);
    }
}

WavePicker::~WavePicker() = default;

void WavePicker::setHighlighted (int wave)
{
    highlighted = wave;
    for (auto& c : cards)
        c->updateButton();

    cards[(size_t) wave]->toFront (false);
    repaint();
}

void WavePicker::startPreview (int wave)
{
    if (previewing == wave)
        return;

    previewing = wave;
    if (onPreview)
        onPreview (wave);

    repaint (statusStrip);
}

void WavePicker::stopPreview (int wave)
{
    if (wave < 0 || previewing != wave)
        return;

    previewing = -1;
    if (onPreview)
        onPreview (-1);

    repaint (statusStrip);
}

void WavePicker::visibilityChanged()
{
    if (! isShowing())
        stopPreview (previewing);
}

void WavePicker::moveFocus (int delta)
{
    int current = highlighted;

    for (int i = 0; i < dsp::numWaves; ++i)
        if (cards[(size_t) i]->getButton().hasKeyboardFocus (false))
            current = i;

    const int next = (current + delta + dsp::numWaves) % dsp::numWaves;
    cards[(size_t) next]->getButton().grabKeyboardFocus();
}

bool WavePicker::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::rightKey) { moveFocus (1);  return true; }
    if (key == juce::KeyPress::leftKey)  { moveFocus (-1); return true; }
    return false;
}

juce::String WavePicker::statusText() const
{
    if (previewing < 0)
        return "Hover or focus a wave to hear it";

    return "Playing " + waveName ((dsp::Wave) previewing).toLowerCase()
           + juce::String (juce::CharPointer_UTF8 (" \xc2\xb7 A3, 220 Hz"));
}

void WavePicker::paint (juce::Graphics& g)
{
    const auto& p = paletteOf (*this);
    const auto area = getLocalBounds().toFloat();

    drawRaised (g, area, 22.0f, p, Elevation::raised, p.surface);

    // Heading block, centred.
    const float cx = area.getCentreX();
    float y = (float) panelPadTop;

    drawCaption (g, "Every patch starts here", { cx - 200.0f, y, 400.0f, 13.0f }, p.mute, juce::Justification::centred);
    y += 13.0f + 10.0f;

    g.setColour (p.ink);
    g.setFont (uiFont (38.0f, Weight::light));
    g.drawText ("Pick a wave to build on", juce::Rectangle<float> (cx - 300.0f, y, 600.0f, 44.0f),
                juce::Justification::centred, false);
    y += 44.0f + 10.0f;

    g.setColour (p.ink2);
    const auto intro = uiFont (15.0f);
    g.setFont (intro);
    drawLines (g, wrapLines (intro, "Nothing else is wired up yet. Hover or focus a wave to hear it held, then choose one. "
                                    "Everything you add after this, you'll add yourself.", 600.0f),
               { cx, y }, 600.0f, 22.0f, true);

    // Status pill and the keyboard hint, centred together.
    const auto statusFont = uiFont (13.0f);
    const auto hint = juce::String (juce::CharPointer_UTF8 ("\xe2\x86\x90 \xe2\x86\x92 to move between waves \xc2\xb7 Enter to choose"));
    const float pillWidth = textWidth (statusFont, statusText()) + 28.0f;
    const float hintWidth = textWidth (statusFont, hint);
    const float x0 = cx - (pillWidth + 22.0f + hintWidth) * 0.5f;
    const juce::Rectangle<float> pill (x0, (float) statusStrip.getY(), pillWidth, (float) statusStrip.getHeight());

    drawInset (g, pill, 15.0f, p, p.well);
    g.setFont (statusFont);
    g.setColour (p.ink2);
    g.drawText (statusText(), pill.reduced (14.0f, 0.0f), juce::Justification::centredLeft, false);
    g.setColour (p.mute);
    g.drawText (hint, juce::Rectangle<float> (pill.getRight() + 22.0f, pill.getY(), hintWidth + 2.0f, pill.getHeight()),
                juce::Justification::centredLeft, false);
}

void WavePicker::resized()
{
    // Heading: caption 13 + 10 + title 44 + 10 + two 22 px lines.
    const int introLines = (int) wrapLines (uiFont (15.0f),
                                            "Nothing else is wired up yet. Hover or focus a wave to hear it held, then choose one. "
                                            "Everything you add after this, you'll add yourself.", 600.0f).size();
    const int cardsTop = panelPadTop + 13 + 10 + 44 + 10 + 22 * introLines + sectionGap;

    int visibleHeight = 0;
    for (auto& c : cards)
        visibleHeight = juce::jmax (visibleHeight, c->getVisibleHeight());

    constexpr int rowWidth = cardWidth * dsp::numWaves + cardGap * (dsp::numWaves - 1);
    int x = (getWidth() - rowWidth) / 2;

    for (auto& c : cards)
    {
        c->setBounds (x - cardMargin, cardsTop - cardMargin, cardWidth + 2 * cardMargin, visibleHeight + 2 * cardMargin);
        c->setVisibleHeight (visibleHeight);
        c->updateButton();
        x += cardWidth + cardGap;
    }

    statusStrip = juce::Rectangle<int> (0, cardsTop + visibleHeight + sectionGap, getWidth(), 30);
}

} // namespace scratchpad::ui
