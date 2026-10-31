#include "plugin/PluginEditor.h"

#include "ui/Motion.h"

#include <cmath>

namespace scratchpad::plugin
{

namespace
{
    const ui::Palette& paletteFor (ui::ViewSettings::Theme t)
    {
        return t == ui::ViewSettings::Theme::light ? ui::lightPalette() : ui::darkPalette();
    }
}

void ScratchpadEditor::ScaleSaver::flush()
{
    stopTimer();
    const double wanted = ui::ViewSettings::clampScale (owner.scale);

    if (std::abs (wanted - owner.settings.getEditorScale()) > 0.0005)
        owner.settings.setEditorScale (wanted);
}

ScratchpadEditor::ScratchpadEditor (ScratchpadProcessor& p)
    : juce::AudioProcessorEditor (p),
      processor (p),
      topBar (p),
      patchView (p),
      performanceBar (p),
      explainPanel (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("Scratchpad Synth");

    addAndMakeVisible (stage);
    stage.addAndMakeVisible (topBar);
    stage.addAndMakeVisible (patchView);
    stage.addAndMakeVisible (performanceBar);
    stage.addAndMakeVisible (explainPanel);

    theme = settings.getTheme();
    ui::motion::setReduced (settings.getReducedMotion());
    scale = settings.getEditorScale();

    explainPanel.setCollapsed (settings.isExplainCollapsed());
    explainPanel.onToggleRequested = [this] { setExplainCollapsed (! explainPanel.isCollapsed()); };
    topBar.getViewButton().onClick = [this] { showViewMenu(); };

    // After the children exist, so they all pick up the palette.
    lookAndFeel.setPalette (paletteFor (theme));
    setLookAndFeel (&lookAndFeel);

    // The standalone app's audio settings dialog is a window of its own, which takes the default look. A plugin must
    // not change the default for the host and its other plugins.
    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

    // Not smaller than 75% of the design, as big as the screen allows, any shape. Set before the size so the first
    // size is already checked.
    updateConstrainer();
    setConstrainer (&constrainer);

    // The window of the standalone app has its own frame to drag; a plugin's window gets a corner grip.
    setResizable (true, processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone);

    layoutStage();
    const auto baseSize = getBaseSize();
    setSize (juce::roundToInt ((double) baseSize.x * scale), juce::roundToInt ((double) baseSize.y * scale));

    juce::Desktop::getInstance().addFocusChangeListener (this);
}

ScratchpadEditor::~ScratchpadEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    scaleSaver.flush();
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lookAndFeel)
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);

    setLookAndFeel (nullptr);
}

void ScratchpadEditor::globalFocusChanged (juce::Component* focused)
{
    // Don't leave notes hanging if keyboard focus leaves the window while a
    // computer key is held (alt-tab, a dialog opening).
    if (focused == nullptr || (focused != this && ! isParentOf (focused)))
        performanceBar.releaseAllNotes();
}

bool ScratchpadEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && patchView.cancelConnecting())
        return true;

    if (topBar.handleKeyPressed (key))
        return true;

    return performanceBar.handleKeyPressed (key);
}

bool ScratchpadEditor::keyStateChanged (bool isKeyDown)
{
    return performanceBar.handleKeyStateChanged (isKeyDown);
}

void ScratchpadEditor::paint (juce::Graphics& g)
{
    g.fillAll (ui::paletteOf (*this).bg);
}

juce::Point<int> ScratchpadEditor::getBaseSize() const noexcept
{
    return { explainPanel.isCollapsed() ? foldedWidth : baseWidth, baseHeight };
}

juce::Point<int> ScratchpadEditor::getStageSize() const noexcept
{
    const auto base = getBaseSize();

    if (getWidth() <= 0 || getHeight() <= 0)
        return base;

    return { juce::jmax (base.x, juce::roundToInt (std::ceil ((double) getWidth() / scale))),
             juce::jmax (base.y, juce::roundToInt (std::ceil ((double) getHeight() / scale))) };
}

void ScratchpadEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // Scale down to fit when the window is smaller than the design; never up. A bigger window is more room.
    const auto base = getBaseSize();
    scale = juce::jlimit (minScale, maxScale, juce::jmin ((double) getWidth() / (double) base.x, (double) getHeight() / (double) base.y));

    const auto stageSize = getStageSize();
    stage.setBounds (0, 0, stageSize.x, stageSize.y);
    stage.setTransform (juce::AffineTransform::scale ((float) scale));

    layoutStage();
    scaleSaver.schedule();
}

void ScratchpadEditor::layoutStage()
{
    namespace layout = ui::layout;

    const auto base = getBaseSize();
    const auto stageSize = getStageSize();
    const int extraWidth = stageSize.x - base.x, extraHeight = stageSize.y - base.y;
    const bool folded = explainPanel.isCollapsed();

    // The extra height goes into the cards (`layout::grown`); the gaps between them stay as they are.
    const auto grow = layout::Grow::forExtra (extraHeight);

    // Expanded, the Explain panel takes the extra width. Folded, nothing can, so the column and the rail sit in the
    // middle of the window.
    const int columnShift = folded ? extraWidth / 2 : 0;

    topBar.setBounds (layout::topBar.rect().withWidth (stageSize.x - 2 * layout::margin));

    auto chain = layout::patchView.rect();
    chain.translate (columnShift, 0);
    chain.setHeight (chain.getHeight() + grow.zone + grow.effects + grow.modulation);
    patchView.setGrow (grow);
    patchView.setBounds (chain);

    performanceBar.setBounds (layout::grown (layout::voicingKeyboard, grow).rect().translated (columnShift, 0));

    const int panelWidth = folded ? ui::ExplainPanel::railWidth : ui::ExplainPanel::width + extraWidth;
    const int right = folded ? stageSize.x - layout::margin - extraWidth / 2 : stageSize.x - layout::margin;
    explainPanel.setBounds (right - panelWidth, layout::explain.y, panelWidth, layout::explain.h + extraHeight);
}

void ScratchpadEditor::updateConstrainer()
{
    // The design's size is the least it shrinks to (75%); there is no most, and no fixed shape.
    const auto base = getBaseSize();
    constrainer.setSizeLimits (juce::roundToInt (minScale * base.x), juce::roundToInt (minScale * base.y), 8192, 8192);
    constrainer.setFixedAspectRatio (0.0);
}

//==============================================================================
ui::ViewState ScratchpadEditor::getViewState() const
{
    return { theme, ui::motion::isReduced(), ! explainPanel.isCollapsed() };
}

void ScratchpadEditor::applyPalette()
{
    lookAndFeel.setPalette (paletteFor (theme));
    sendLookAndFeelChange();
}

void ScratchpadEditor::setTheme (ui::ViewSettings::Theme newTheme)
{
    settings.setTheme (newTheme);

    if (newTheme != theme)
    {
        theme = newTheme;
        applyPalette();
    }
}

void ScratchpadEditor::setReducedMotion (bool on)
{
    settings.setReducedMotion (on);
    ui::motion::setReduced (on);
}

void ScratchpadEditor::setExplainCollapsed (bool collapsed)
{
    settings.setExplainCollapsed (collapsed);

    // The button that was pressed goes with the state it belonged to; keep the keyboard on its counterpart.
    const bool hadFocus = explainPanel.hasKeyboardFocus (true);

    if (collapsed != explainPanel.isCollapsed())
    {
        explainPanel.setCollapsed (collapsed);
        updateConstrainer();
        resized();
    }

    if (hadFocus)
        explainPanel.focusToggleButton();
}

void ScratchpadEditor::showViewMenu()
{
    juce::Component::SafePointer<ScratchpadEditor> safe (this);

    auto menu = ui::buildViewMenu (getViewState());
    ui::showStyledMenu (menu, juce::PopupMenu::Options().withTargetComponent (&topBar.getViewButton()),
                        [safe] (int result)
                        {
                            if (safe != nullptr)
                                safe->applyViewChoice ((ui::ViewChoice) result);
                        });
}

void ScratchpadEditor::applyViewChoice (ui::ViewChoice choice)
{
    switch (choice)
    {
        case ui::ViewChoice::themeDark:      setTheme (ui::ViewSettings::Theme::dark); break;
        case ui::ViewChoice::themeLight:     setTheme (ui::ViewSettings::Theme::light); break;
        case ui::ViewChoice::reducedMotion:  setReducedMotion (! ui::motion::isReduced()); break;
        case ui::ViewChoice::explainPanel:   setExplainCollapsed (! explainPanel.isCollapsed()); break;
        case ui::ViewChoice::none:           break;
    }
}

} // namespace scratchpad::plugin
