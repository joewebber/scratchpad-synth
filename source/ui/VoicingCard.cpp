#include "ui/VoicingCard.h"

#include "plugin/Parameters.h"

#include <cmath>

namespace scratchpad::ui
{

//==============================================================================
VoicingCard::VoicingCard (plugin::ScratchpadProcessor& p)
    : mode (p.apvts, params::voicingMode, "Voice mode", p.undoManager, &Palette::amp),
      voices (p.apvts, params::voicingVoices, "Voices", p.undoManager, "Voices", layout::knobs::voicing),
      glide (p.apvts, params::voicingGlide, "Glide", p.undoManager, "Glide time", layout::knobs::voicing),
      bendRange (p.apvts, params::voicingBendRange, "Bend", p.undoManager, "Pitch bend range", layout::knobs::voicing),
      drift (p.apvts, params::voicingDrift, "Drift", p.undoManager, "Analogue pitch and cutoff drift", layout::knobs::voicing),
      modeWatcher (*p.apvts.getParameter (params::voicingMode),
                   [this] (float v)
                   {
                       modeIndex = (int) std::lround (v);
                       voices.setEnabled (modeIndex == 0); // mono and legato are one voice
                       repaint();
                   })
{
    setTitle ("Voicing");
    setExplainKey ("voicing");

    for (auto* c : std::initializer_list<juce::Component*> { &mode, &voices, &glide, &bendRange, &drift })
        addAndMakeVisible (c);

    applyColours();
    modeWatcher.sendInitialUpdate();
    voices.setDisabledNote ("Not in use: mono and legato play one voice.");
    setFocusOrder ({ &mode, &voices, &glide, &bendRange, &drift });
}

void VoicingCard::applyColours()
{
    const auto ink = paletteOf (*this).ampInk;
    for (auto* k : { &voices, &glide, &bendRange, &drift })
        k->setFamilyColour (ink);
}

void VoicingCard::lookAndFeelChanged()
{
    applyColours();
}

void VoicingCard::resized()
{
    constexpr int captionHeight = 12, captionGap = 6, segmentHeight = 24, segmentsWidth = layout::segments::voiceMode;

    // The two groups are centred on the card's height, as the design's flex row centres them.
    const int segmentsTop = (getHeight() - (captionHeight + captionGap + segmentHeight)) / 2;
    captionArea = { 0, segmentsTop, segmentsWidth, captionHeight };
    mode.setBounds (0, segmentsTop + captionHeight + captionGap, segmentsWidth, segmentHeight);

    int x = segmentsWidth + 16;
    for (auto* k : { &voices, &glide, &bendRange, &drift })
    {
        k->setBounds (x, (getHeight() - k->height()) / 2, k->getSize().cell, k->height());
        x += k->getSize().cell + 4;
    }
}

void VoicingCard::paint (juce::Graphics& g)
{
    drawCaption (g, "Voicing", captionArea.toFloat(), paletteOf (*this).ink2);
}

} // namespace scratchpad::ui
