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

void ScratchpadEditor::SizeSaver::flush()
{
    stopTimer();

    if (! owner.isStandalone() || owner.getWidth() <= 0 || owner.getHeight() <= 0)
        return;

    const auto saved = owner.settings.getWindowSize();

    if (! saved || saved->x != owner.getWidth() || saved->y != owner.getHeight())
        owner.settings.setWindowSize (owner.getWidth(), owner.getHeight());
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
    uiScale = settings.getUiScale();
    scale = uiScale;

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

    // Not smaller than 75% of the smallest layout, as big as the limits allow, any shape. Set before the size so the
    // first size is already checked.
    updateConstrainer();
    setConstrainer (&constrainer);

    // The window of the standalone app has its own frame to drag; a plugin's window gets a corner grip.
    setResizable (true, ! isStandalone());

    // The standalone app opens as it was left; a plugin opens at Small, the layout of 1200 x 760 at the UI scale.
    const auto saved = isStandalone() ? settings.getWindowSize() : std::nullopt;
    const auto size = saved ? *saved : windowSizeFor ({ ui::layout::minLayoutWidth, ui::layout::minLayoutHeight });
    setSize (size.x, size.y);

    juce::Desktop::getInstance().addFocusChangeListener (this);
}

ScratchpadEditor::~ScratchpadEditor()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    sizeSaver.flush();
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

void ScratchpadEditor::resized()
{
    if (getWidth() <= 0 || getHeight() <= 0)
        return;

    namespace layout = ui::layout;

    // The layout size is the window's size over the UI scale. Under the smallest layout nothing rearranges: the whole
    // stage shrinks to fit (and the constrainer keeps that to 75% on the screen).
    const double minWidth = (double) layout::Plan::minWidthFor (explainPanel.isCollapsed());
    const double shrink = juce::jmin (1.0, ((double) getWidth() / uiScale) / minWidth, ((double) getHeight() / uiScale) / (double) layout::minLayoutHeight);
    scale = uiScale * shrink;

    const int stageWidth = juce::roundToInt (std::ceil ((double) getWidth() / scale));
    const int stageHeight = juce::roundToInt (std::ceil ((double) getHeight() / scale));

    plan = layout::Plan::make (stageWidth, stageHeight, explainPanel.isCollapsed());
    stage.setBounds (0, 0, stageWidth, stageHeight);
    stage.setTransform (juce::AffineTransform::scale ((float) scale));

    layoutStage();
    sizeSaver.schedule();
}

void ScratchpadEditor::layoutStage()
{
    topBar.setBounds (plan.topBar);
    patchView.setPlan (plan);
    patchView.setBounds (plan.patchView);
    performanceBar.setBounds (plan.voicingKeyboard);
    explainPanel.setBounds (plan.explain);
}

juce::Point<int> ScratchpadEditor::windowSizeFor (juce::Point<int> layoutSize) const noexcept
{
    using S = ui::ViewSettings;

    return { juce::jlimit (S::minWindowWidth, S::maxWindowWidth, juce::roundToInt ((double) layoutSize.x * uiScale)),
             juce::jlimit (S::minWindowHeight, S::maxWindowHeight, juce::roundToInt ((double) layoutSize.y * uiScale)) };
}

void ScratchpadEditor::updateConstrainer()
{
    // 75% of the smallest layout is the least (900 x 570, 744 x 570 with the panel folded); there is no fixed shape.
    namespace layout = ui::layout;
    using S = ui::ViewSettings;

    constrainer.setSizeLimits (juce::roundToInt (minScale * layout::Plan::minWidthFor (explainPanel.isCollapsed())),
                               juce::roundToInt (minScale * layout::minLayoutHeight), S::maxWindowWidth, S::maxWindowHeight);
    constrainer.setFixedAspectRatio (0.0);
}

//==============================================================================
ui::ViewState ScratchpadEditor::getViewState() const
{
    return { theme, ui::motion::isReduced(), ! explainPanel.isCollapsed(), uiScale };
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

void ScratchpadEditor::setUiScale (double newScale)
{
    const double wanted = ui::ViewSettings::clampScale (newScale);
    settings.setUiScale (wanted);

    if (std::abs (wanted - uiScale) < 1.0e-6)
        return;

    // The window follows by the same ratio, so the layout (and what is on show) stays the same.
    const double ratio = wanted / uiScale;
    uiScale = wanted;

    using S = ui::ViewSettings;
    setSize (juce::jlimit (S::minWindowWidth, S::maxWindowWidth, juce::roundToInt ((double) getWidth() * ratio)),
             juce::jlimit (S::minWindowHeight, S::maxWindowHeight, juce::roundToInt ((double) getHeight() * ratio)));
    resized(); // also when the host kept the old size
}

void ScratchpadEditor::setWindowPreset (ui::ViewChoice choice)
{
    const auto layoutSize = ui::windowLayoutSizeOf (choice);

    if (layoutSize.x > 0)
    {
        const auto size = windowSizeFor (layoutSize);
        setSize (size.x, size.y);
    }
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

        case ui::ViewChoice::uiScale75:
        case ui::ViewChoice::uiScale100:
        case ui::ViewChoice::uiScale125:
        case ui::ViewChoice::uiScale150:
        case ui::ViewChoice::uiScale175:
        case ui::ViewChoice::uiScale200:     setUiScale (ui::uiScaleOf (choice)); break;

        case ui::ViewChoice::windowSmall:
        case ui::ViewChoice::windowMedium:
        case ui::ViewChoice::windowLarge:    setWindowPreset (choice); break;
    }
}

} // namespace scratchpad::plugin
