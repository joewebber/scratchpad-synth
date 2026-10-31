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

    explainPanel.setCollapsed (settings.isExplainCollapsed (scale));
    explainPanel.onToggleRequested = [this] { setExplainCollapsed (! explainPanel.isCollapsed()); };
    topBar.getViewButton().onClick = [this] { showViewMenu(); };

    // After the children exist, so they all pick up the palette.
    lookAndFeel.setPalette (paletteFor (theme));
    setLookAndFeel (&lookAndFeel);

    // 75% to 200% of the stage, at the stage's shape. Set before the size so the first size is already checked.
    const auto stageSize = getStageSize();
    constrainer.setSizeLimits (juce::roundToInt (minScale * stageSize.x), juce::roundToInt (minScale * stageSize.y),
                               juce::roundToInt (maxScale * stageSize.x), juce::roundToInt (maxScale * stageSize.y));
    constrainer.setFixedAspectRatio ((double) stageSize.x / (double) stageSize.y);
    setConstrainer (&constrainer);

    // The window of the standalone app has its own frame to drag; a plugin's window gets a corner grip.
    setResizable (true, processor.wrapperType != juce::AudioProcessor::wrapperType_Standalone);

    layoutStage();
    setSize (juce::roundToInt ((double) stageSize.x * scale), juce::roundToInt ((double) stageSize.y * scale));

    juce::Desktop::getInstance().addFocusChangeListener (this);
}

ScratchpadEditor::~ScratchpadEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    scaleSaver.flush();
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

juce::Point<int> ScratchpadEditor::getStageSize() const noexcept
{
    return { explainPanel.isCollapsed() ? foldedWidth : baseWidth, baseHeight };
}

void ScratchpadEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    // Fit the stage in whatever size the window has. The constrainer keeps the shape right when the user drags, but a
    // host may set any size, and then the stage is centred with the background showing round it.
    const auto stageSize = getStageSize();
    scale = juce::jmin ((double) getWidth() / (double) stageSize.x, (double) getHeight() / (double) stageSize.y);

    const auto offset = juce::Point<float> (((float) getWidth() - (float) (stageSize.x * scale)) * 0.5f,
                                            ((float) getHeight() - (float) (stageSize.y * scale)) * 0.5f);

    shownStage = stageSize;
    stage.setBounds (0, 0, stageSize.x, stageSize.y);
    stage.setTransform (juce::AffineTransform::scale ((float) scale).translated (offset.x, offset.y));

    refreshExplainState();
    scaleSaver.schedule();
}

void ScratchpadEditor::layoutStage()
{
    namespace layout = ui::layout;

    topBar.setBounds (layout::topBar.rect().withWidth (getStageSize().x - 2 * layout::margin));
    patchView.setBounds (layout::patchView.rect());
    performanceBar.setBounds (layout::voicingKeyboard.rect());

    // Folded, the rail takes the panel's place at the right edge, and the stage has narrowed by the difference.
    const auto stageRight = getStageSize().x - layout::margin;
    const int panelWidth = explainPanel.isCollapsed() ? ui::ExplainPanel::railWidth : ui::ExplainPanel::width;
    explainPanel.setBounds (stageRight - panelWidth, layout::explain.y, panelWidth, layout::explain.h);
}

void ScratchpadEditor::applyStageSize()
{
    // The window follows the stage: same scale, new width. The constrainer keeps its limits and shape in step. A
    // window the host gave some other shape is left as it is; the stage is fitted inside it.
    const auto stageSize = getStageSize();
    const double windowShape = (double) getWidth() / (double) juce::jmax (1, getHeight());
    const bool followsStage = std::abs (windowShape - (double) shownStage.x / (double) shownStage.y) < 0.01;

    constrainer.setSizeLimits (juce::roundToInt (minScale * stageSize.x), juce::roundToInt (minScale * stageSize.y),
                               juce::roundToInt (maxScale * stageSize.x), juce::roundToInt (maxScale * stageSize.y));
    constrainer.setFixedAspectRatio ((double) stageSize.x / (double) stageSize.y);

    if (followsStage)
        setSize (juce::roundToInt ((double) stageSize.x * scale), juce::roundToInt ((double) stageSize.y * scale));

    resized();
}

void ScratchpadEditor::refreshExplainState()
{
    // With no choice made, the panel follows the window size: folded below 100%.
    const bool shouldCollapse = settings.isExplainCollapsed (scale);

    if (shouldCollapse != explainPanel.isCollapsed())
    {
        explainPanel.setCollapsed (shouldCollapse);
        applyStageSize();
    }

    layoutStage();
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
        applyStageSize();
        layoutStage();
    }

    if (hadFocus)
        explainPanel.focusToggleButton();
}

void ScratchpadEditor::showViewMenu()
{
    juce::Component::SafePointer<ScratchpadEditor> safe (this);

    ui::buildViewMenu (getViewState())
        .showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&topBar.getViewButton()),
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
