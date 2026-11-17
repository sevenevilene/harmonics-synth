#include "PluginEditor.h"

using namespace harmonics;

namespace
{
const juce::Colour background { 0xff1d1f24 }, panel { 0xff2a2d34 }, accent { 0xff5fb3f0 },
                   faint { 0xff454a54 }, textColour { 0xffc8ccd4 };
}

//==============================================================================
GraphComponent::GraphComponent (Table& t, float minimum, float maximum, juce::String name)
    : table (t), lo (minimum), hi (maximum), title (std::move (name))
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void GraphComponent::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float slotWidth = area.getWidth() / (float) numSlots;
    const auto yFor = [&] (float v) { return area.getHeight() * (hi - v) / (hi - lo); };
    const float zero = yFor (0.0f);

    g.setColour (panel);
    g.fillRect (area);

    g.setColour (faint);
    g.fillRect ((float) rootSlot * slotWidth, 0.0f, slotWidth, area.getHeight()); // the root
    g.fillRect (0.0f, zero - 0.5f, area.getWidth(), 1.0f);

    g.setColour (accent);
    for (int i = 0; i < numSlots; ++i)
    {
        const float y = yFor (juce::jlimit (lo, hi, table[(size_t) i].load (std::memory_order_relaxed)));
        g.fillRect ((float) i * slotWidth, juce::jmin (y, zero), juce::jmax (1.0f, slotWidth - 0.5f),
                    juce::jmax (1.0f, std::abs (y - zero)));
    }

    g.setColour (textColour);
    g.drawText (title, getLocalBounds().reduced (6, 3), juce::Justification::topLeft);
}

void GraphComponent::drawAt (juce::Point<float> position, bool continuing)
{
    const int slot = juce::jlimit (0, numSlots - 1, (int) (position.x / (float) getWidth() * (float) numSlots));
    const float value = juce::jlimit (lo, hi, hi - position.y / (float) getHeight() * (hi - lo));

    if (! continuing)
    {
        lastSlot = slot;
        lastValue = value;
    }

    // Fill in the slots between the previous mouse position and this one.
    const int from = juce::jmin (slot, lastSlot), to = juce::jmax (slot, lastSlot);
    for (int i = from; i <= to; ++i)
    {
        const float t = slot == lastSlot ? 1.0f : (float) (i - lastSlot) / (float) (slot - lastSlot);
        table[(size_t) i].store (lastValue + t * (value - lastValue), std::memory_order_relaxed);
    }

    lastSlot = slot;
    lastValue = value;
    repaint();
}

void GraphComponent::mouseDown (const juce::MouseEvent& e) { drawAt (e.position, false); }
void GraphComponent::mouseDrag (const juce::MouseEvent& e) { drawAt (e.position, true); }

//==============================================================================
void WindowBar::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float slotWidth = area.getWidth() / (float) numSlots;
    const int first = rootSlot - subharmonics.load (std::memory_order_relaxed);

    g.setColour (panel);
    g.fillRect (area);
    g.setColour (accent.withAlpha (0.5f));
    g.fillRect ((float) first * slotWidth, area.getHeight() * 0.5f, (float) numPartials * slotWidth, area.getHeight() * 0.5f);
    g.setColour (accent);
    g.fillRect ((float) rootSlot * slotWidth, 0.0f, juce::jmax (2.0f, slotWidth), area.getHeight());
}

//==============================================================================
HarmonicsAudioProcessorEditor::HarmonicsAudioProcessorEditor (HarmonicsAudioProcessor& p)
    : AudioProcessorEditor (&p),
      synth (p),
      amps (p.engine.tables.amps, -1.0f, 1.0f, "levels: subharmonics | root | harmonics"),
      panL (p.engine.tables.panL, 0.0f, 1.0f, "left pan"),
      panR (p.engine.tables.panR, 0.0f, 1.0f, "right pan"),
      windowBar (p.engine.lastSubharmonics),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    for (auto* c : std::initializer_list<juce::Component*> { &amps, &panL, &panR, &windowBar, &keyboard })
        addAndMakeVisible (c);

    const auto makeButtons = [this] (juce::OwnedArray<juce::TextButton>& buttons, const juce::StringArray& names, auto onClick)
    {
        for (int i = 0; i < names.size(); ++i)
        {
            auto* b = buttons.add (new juce::TextButton (names[i]));
            b->setColour (juce::TextButton::buttonColourId, panel);
            b->setColour (juce::TextButton::buttonOnColourId, accent.darker (0.3f));
            b->onClick = [onClick, i] { onClick (i); };
            addAndMakeVisible (b);
        }
    };
    makeButtons (spectrumButtons, HarmonicsAudioProcessor::spectrumNames, [this] (int i) { synth.applySpectrum ((Spectrum) i); });
    makeButtons (panButtons, HarmonicsAudioProcessor::panCurveNames, [this] (int i) { synth.applyPanCurve ((PanCurve) i); });

    addControl (tilt, "tilt", "tilt (tilt + lfo)");
    addControl (center, "center", "center (formant)");
    addControl (width, "width", "width (formant)");
    addControl (speed, "speed", "speed (animate)");
    addControl (subs, "subs", "subs");
    addControl (spread, "spread", "spread");
    addControl (reach, "reach", "reach (partials)");
    addControl (attack, "attack", "attack (ms)");
    addControl (release, "release", "release (ms)");
    addControl (volume, "volume", "volume");
    addToggle (animate, animateAttachment, "animate");
    addToggle (glide, glideAttachment, "glide");

    keyboard.setAvailableRange (24, 108);

    setSize (920, 640);
    startTimerHz (30);
}

void HarmonicsAudioProcessorEditor::addControl (Control& c, const juce::String& parameterID, const juce::String& text)
{
    c.label.setText (text, juce::dontSendNotification);
    c.label.setColour (juce::Label::textColourId, textColour);
    c.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    c.slider.setColour (juce::Slider::thumbColourId, accent);
    c.slider.setColour (juce::Slider::trackColourId, accent.darker (0.4f));
    c.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (c.label);
    addAndMakeVisible (c.slider);
    c.attachment = std::make_unique<APVTS::SliderAttachment> (synth.apvts, parameterID, c.slider);
}

void HarmonicsAudioProcessorEditor::addToggle (juce::ToggleButton& b, std::unique_ptr<APVTS::ButtonAttachment>& attachment,
                                               const juce::String& parameterID)
{
    b.setColour (juce::ToggleButton::textColourId, textColour);
    b.setColour (juce::ToggleButton::tickColourId, accent);
    addAndMakeVisible (b);
    attachment = std::make_unique<APVTS::ButtonAttachment> (synth.apvts, parameterID, b);
}

void HarmonicsAudioProcessorEditor::timerCallback()
{
    const int spectrum = juce::roundToInt (synth.apvts.getRawParameterValue ("spectrum")->load());
    const int panCurve = juce::roundToInt (synth.apvts.getRawParameterValue ("panCurve")->load());

    for (int i = 0; i < spectrumButtons.size(); ++i)
        spectrumButtons[i]->setToggleState (i == spectrum, juce::dontSendNotification);
    for (int i = 0; i < panButtons.size(); ++i)
        panButtons[i]->setToggleState (i == panCurve, juce::dontSendNotification);

    amps.repaint();
    panL.repaint();
    panR.repaint();
    windowBar.repaint();
}

void HarmonicsAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void HarmonicsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    keyboard.setBounds (area.removeFromBottom (70));
    keyboard.setKeyWidth ((float) keyboard.getWidth() / 50.0f); // C1..C8 = 50 white keys
    area.removeFromBottom (10);

    amps.setBounds (area.removeFromTop (150));
    area.removeFromTop (4);
    panL.setBounds (area.removeFromTop (64));
    area.removeFromTop (4);
    panR.setBounds (area.removeFromTop (64));
    area.removeFromTop (4);
    windowBar.setBounds (area.removeFromTop (12));
    area.removeFromTop (12);

    const auto layoutButtons = [] (juce::Rectangle<int> row, juce::OwnedArray<juce::TextButton>& buttons, int buttonWidth)
    {
        for (auto* b : buttons)
            b->setBounds (row.removeFromLeft (buttonWidth).reduced (2, 0));
    };

    // Each row is split into equal cells holding a label above a slider (or a toggle).
    const auto layoutRow = [] (juce::Rectangle<int> row, int cells, std::initializer_list<Control*> controls,
                               juce::ToggleButton* toggle)
    {
        const int cellWidth = row.getWidth() / cells;
        for (auto* c : controls)
        {
            auto cell = row.removeFromLeft (cellWidth).reduced (4, 0);
            c->label.setBounds (cell.removeFromTop (18));
            c->slider.setBounds (cell.removeFromTop (22));
        }
        if (toggle != nullptr)
            toggle->setBounds (row.removeFromLeft (cellWidth).reduced (4, 0).withTrimmedTop (16).withHeight (24));
    };

    layoutButtons (area.removeFromTop (26), spectrumButtons, area.getWidth() / spectrumButtons.size());
    area.removeFromTop (8);
    layoutRow (area.removeFromTop (44), 6, { &tilt, &center, &width, &speed, &subs }, &animate);
    area.removeFromTop (12);

    auto panRow = area.removeFromTop (44);
    layoutButtons (panRow.removeFromLeft (panRow.getWidth() / 2).withTrimmedTop (14).withHeight (26), panButtons, 88);
    layoutRow (panRow, 2, { &spread, &reach }, nullptr);
    area.removeFromTop (12);

    layoutRow (area.removeFromTop (44), 6, { &attack, &release, &volume }, &glide);
}
