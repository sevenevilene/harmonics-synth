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
WheelComponent::WheelComponent (std::atomic<float>& v, float minimum, float maximum, bool springBack, juce::String name)
    : value (v), lo (minimum), hi (maximum), springs (springBack), title (std::move (name))
{
    setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
}

juce::Rectangle<float> WheelComponent::wheelArea() const
{
    return getLocalBounds().toFloat().withTrimmedBottom (16.0f);
}

void WheelComponent::set (float v)
{
    value.store (juce::jlimit (lo, hi, v), std::memory_order_relaxed);
    repaint();
}

void WheelComponent::paint (juce::Graphics& g)
{
    const auto area = wheelArea();
    const float t = (juce::jlimit (lo, hi, value.load (std::memory_order_relaxed)) - lo) / (hi - lo);
    const float thumbHeight = 8.0f;
    const float y = area.getBottom() - thumbHeight * 0.5f - t * (area.getHeight() - thumbHeight);

    g.setColour (panel);
    g.fillRoundedRectangle (area, 4.0f);

    // The wheel's ridges, shifting as it turns.
    g.setColour (faint);
    const float spacing = 6.0f;
    for (float ridge = std::fmod (y - area.getY(), spacing); ridge < area.getHeight(); ridge += spacing)
        g.fillRect (area.getX() + 4.0f, area.getY() + ridge, area.getWidth() - 8.0f, 1.0f);

    if (springs) // the centre mark
    {
        g.setColour (textColour.withAlpha (0.4f));
        g.fillRect (area.getX(), std::round (area.getCentreY()), 3.0f, 1.0f);
        g.fillRect (area.getRight() - 3.0f, std::round (area.getCentreY()), 3.0f, 1.0f);
    }

    // The part of the travel the wheel has moved through from where it rests.
    const float rest = area.getBottom() - thumbHeight * 0.5f
                       - (resting() - lo) / (hi - lo) * (area.getHeight() - thumbHeight);
    g.setColour (accent.withAlpha (0.25f));
    g.fillRect (area.getX() + 2.0f, juce::jmin (y, rest), area.getWidth() - 4.0f, std::abs (y - rest));

    g.setColour (accent);
    g.fillRoundedRectangle (area.getX() + 2.0f, y - thumbHeight * 0.5f, area.getWidth() - 4.0f, thumbHeight, 2.0f);

    g.setColour (textColour);
    g.setFont (12.0f);
    g.drawText (title, getLocalBounds().removeFromBottom (14), juce::Justification::centred);
}

void WheelComponent::mouseDown (const juce::MouseEvent&)
{
    dragStartValue = value.load (std::memory_order_relaxed);
}

void WheelComponent::mouseDrag (const juce::MouseEvent& e)
{
    // Relative, like a real wheel: dragging the full height covers the whole range.
    const float travel = juce::jmax (1.0f, wheelArea().getHeight() - 8.0f);
    set (dragStartValue - (float) e.getDistanceFromDragStartY() / travel * (hi - lo));
}

void WheelComponent::mouseUp (const juce::MouseEvent&)
{
    if (springs)
        set (resting());
}

void WheelComponent::mouseDoubleClick (const juce::MouseEvent&)
{
    set (resting());
}

//==============================================================================
void SpectrumAnalyser::update (double sampleRate, double rootFrequency)
{
    rate = sampleRate > 0.0 ? sampleRate : 44100.0;
    rootHz = rootFrequency;

    for (size_t channel = 0; channel < 2; ++channel)
    {
        std::fill (scratch.begin(), scratch.end(), 0.0f);
        tap.copyLatest ((int) channel, scratch.data(), fftSize);
        window.multiplyWithWindowingTable (scratch.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (scratch.data(), true);

        // The window is normalised, so a full-scale sine reads 0 dB. Levels rise
        // at once and fall back slowly, which makes the picture easier to read.
        for (size_t bin = 0; bin < (size_t) numBins; ++bin)
        {
            const float db = juce::Decibels::gainToDecibels (scratch[bin] * 2.0f / (float) fftSize, minDb);
            levels[channel][bin] = juce::jmax (db, levels[channel][bin] - 1.5f);
        }
    }

    repaint();
}

void SpectrumAnalyser::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const float w = area.getWidth(), h = area.getHeight();
    const float octaves = std::log (maxHz / minHz);
    const auto xFor = [&] (float hz) { return w * std::log (hz / minHz) / octaves; };
    const auto yFor = [&] (float db) { return h * (maxDb - juce::jlimit (minDb, maxDb, db)) / (maxDb - minDb); };

    g.setColour (panel);
    g.fillRect (area);

    g.setFont (11.0f);
    for (const float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
    {
        const float x = std::round (xFor (hz));
        g.setColour (faint);
        g.fillRect (x, 0.0f, 1.0f, h);

        if (juce::exactlyEqual (hz, 100.0f) || juce::exactlyEqual (hz, 1000.0f) || juce::exactlyEqual (hz, 10000.0f))
        {
            g.setColour (textColour.withAlpha (0.6f));
            g.drawText (hz < 1000.0f ? juce::String ((int) hz) : juce::String ((int) hz / 1000) + "k",
                        juce::Rectangle<float> (x + 3.0f, h - 15.0f, 40.0f, 13.0f), juce::Justification::bottomLeft);
        }
    }
    for (const float db : { -24.0f, -48.0f, -72.0f })
    {
        const float y = std::round (yFor (db));
        g.setColour (faint);
        g.fillRect (0.0f, y, w, 1.0f);
        g.setColour (textColour.withAlpha (0.6f));
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (w - 44.0f, y - 14.0f, 40.0f, 13.0f),
                    juce::Justification::bottomRight);
    }

    // Green at the root, reaching full blue five octaves above it (the 32nd
    // harmonic) and full red five octaves below (the 32nd subharmonic).
    const auto colourFor = [&] (float hz)
    {
        const float distance = juce::jlimit (-1.0f, 1.0f, std::log2 (hz / (float) rootHz) / 5.0f);
        return juce::Colour::fromHSV ((1.0f + distance) / 3.0f, 0.7f, 1.0f, 1.0f);
    };

    const int steps = 64;
    juce::ColourGradient scale (colourFor (minHz), 0.0f, 0.0f, colourFor (maxHz), w, 0.0f, false);
    for (int i = 1; i < steps; ++i)
        scale.addColour ((double) i / steps, colourFor (minHz * std::exp (octaves * (float) i / (float) steps)));

    if (rootHz >= minHz && rootHz <= maxHz)
    {
        g.setColour (colourFor ((float) rootHz).withAlpha (0.35f));
        g.fillRect (std::round (xFor ((float) rootHz)), 0.0f, 1.0f, h);
    }

    const float binsPerHz = (float) fftSize / (float) rate;
    const auto binFor = [&] (float x) { return minHz * std::exp (octaves * x / w) * binsPerHz; };

    for (const size_t channel : { (size_t) 1, (size_t) 0 })
    {
        const auto& level = levels[channel];
        juce::Path line;
        bool started = false;

        for (float x = 0.0f; x <= w; x += 1.0f)
        {
            const float from = binFor (x), to = binFor (x + 1.0f);
            if (from >= (float) (numBins - 1))
                break; // above half the sample rate

            const auto first = (size_t) from;
            float db;
            if (to - from < 1.0f) // bins are wider than a pixel down here: interpolate
            {
                db = level[first] + (from - (float) first) * (level[first + 1] - level[first]);
            }
            else // several bins per pixel: show the loudest
            {
                const auto last = juce::jmin ((size_t) to, (size_t) numBins - 1);
                db = *std::max_element (level.begin() + (std::ptrdiff_t) first, level.begin() + (std::ptrdiff_t) last + 1);
            }

            // (Path::isEmpty() stays true until the path has a line in it.)
            if (! std::exchange (started, true))
                line.startNewSubPath (x, yFor (db));
            else
                line.lineTo (x, yFor (db));
        }

        if (! started)
            continue;

        // The left channel is drawn bright and filled, the right one faint on top of it.
        if (channel == 0)
        {
            auto fill = line;
            fill.lineTo (line.getCurrentPosition().x, h);
            fill.lineTo (0.0f, h);
            fill.closeSubPath();

            auto faintScale = scale;
            faintScale.multiplyOpacity (0.15f);
            g.setGradientFill (faintScale);
            g.fillPath (fill);
            g.setGradientFill (scale);
            g.strokePath (line, juce::PathStrokeType (1.4f));
        }
        else
        {
            auto faintScale = scale;
            faintScale.multiplyOpacity (0.45f);
            g.setGradientFill (faintScale);
            g.strokePath (line, juce::PathStrokeType (1.0f));
        }
    }

    // Label the loudest peaks with their frequency. A peak is a bin that stands
    // at least 6 dB above the bins three to either side, in either channel.
    struct Peak
    {
        float db, hz;
    };
    std::vector<Peak> peaks;
    const auto both = [&] (size_t bin) { return juce::jmax (levels[0][bin], levels[1][bin]); };

    for (size_t bin = 3; bin + 3 < (size_t) numBins; ++bin)
    {
        const float below = both (bin - 1), db = both (bin), above = both (bin + 1);
        if (db < -72.0f || db < below || db <= above || db - juce::jmax (both (bin - 3), both (bin + 3)) < 6.0f)
            continue;

        // Fit a parabola through the three bins to find the frequency between them.
        const float curve = below - 2.0f * db + above;
        const float offset = curve < 0.0f ? 0.5f * (below - above) / curve : 0.0f;
        const float hz = ((float) bin + offset) / binsPerHz;
        if (hz >= minHz && hz <= maxHz)
            peaks.push_back ({ db, hz });
    }

    std::sort (peaks.begin(), peaks.end(), [] (const Peak& a, const Peak& b) { return a.db > b.db; });

    const float labelWidth = 44.0f;
    std::vector<float> labelled;
    g.setFont (11.0f);

    for (const auto& peak : peaks)
    {
        const float x = xFor (peak.hz), y = yFor (peak.db);
        if (std::any_of (labelled.begin(), labelled.end(), [&] (float other) { return std::abs (other - x) < labelWidth; }))
            continue; // would overlap the label of a louder peak

        const auto text = peak.hz < 100.0f    ? juce::String (peak.hz, 1)
                          : peak.hz < 1000.0f ? juce::String (juce::roundToInt (peak.hz))
                                              : juce::String (peak.hz / 1000.0f, 2) + "k";
        const float left = juce::jlimit (0.0f, w - labelWidth, x - labelWidth * 0.5f);

        g.setColour (textColour);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
        g.drawText (text, juce::Rectangle<float> (left, juce::jmax (18.0f, y - 16.0f), labelWidth, 13.0f),
                    juce::Justification::centred);

        labelled.push_back (x);
        if (labelled.size() == 8)
            break;
    }

    auto legend = getLocalBounds().reduced (6, 3).removeFromTop (14);
    g.setFont (juce::FontOptions().withHeight (13.0f));
    g.setColour (textColour.withAlpha (0.45f));
    g.drawText ("right", legend.removeFromRight (34), juce::Justification::topRight);
    g.setColour (textColour);
    g.drawText ("left", legend.removeFromRight (30), juce::Justification::topRight);
    g.setColour (textColour);
    g.drawText ("output spectrum", legend, juce::Justification::topLeft);
}

//==============================================================================
HarmonicsAudioProcessorEditor::HarmonicsAudioProcessorEditor (HarmonicsAudioProcessor& p)
    : AudioProcessorEditor (&p),
      synth (p),
      amps (p.engine.tables.amps, -1.0f, 1.0f, "levels: subharmonics | root | harmonics"),
      panL (p.engine.tables.panL, 0.0f, 1.0f, "left pan"),
      panR (p.engine.tables.panR, 0.0f, 1.0f, "right pan"),
      windowBar (p.engine.lastSubharmonics),
      analyser (p.outputTap),
      pitchWheel (p.pitchWheel, -1.0f, 1.0f, true, "pitch"),
      modWheel (p.modWheel, 0.0f, 1.0f, false, "mod"),
      keyboard (p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard)
{
    for (auto* c : std::initializer_list<juce::Component*> { &amps, &panL, &panR, &windowBar, &analyser, &pitchWheel, &modWheel, &keyboard })
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
    addControl (amount, "amount", "amount (mod wheel)");
    addControl (subs, "subs", "subs");
    addControl (spread, "spread", "spread");
    addControl (reach, "reach", "reach (partials)");
    addControl (attack, "attack", "attack (ms)");
    addControl (release, "release", "release (ms)");
    addControl (volume, "volume", "volume");
    addToggle (glide, glideAttachment, "glide");

    keyboard.setAvailableRange (24, 108);

    setSize (920, 780);
    startTimerHz (30);
}

void HarmonicsAudioProcessorEditor::LearnSlider::mouseDown (const juce::MouseEvent& e)
{
    menuClick = e.mods.isPopupMenu();
    if (menuClick)
    {
        if (onMenu)
            onMenu();
        return;
    }
    juce::Slider::mouseDown (e);
}

void HarmonicsAudioProcessorEditor::addControl (Control& c, const juce::String& parameterID, const juce::String& text)
{
    c.text = text;
    c.learnIndex = HarmonicsAudioProcessor::learnableIDs.indexOf (parameterID);
    c.label.setText (text, juce::dontSendNotification);
    if (c.learnIndex >= 0)
    {
        c.slider.onMenu = [this, &c] { showLearnMenu (c); };
        c.label.addMouseListener (this, false);
        c.label.setTooltip ("Right-click to assign a MIDI controller");
        c.slider.setTooltip ("Right-click to assign a MIDI controller");
    }
    c.label.setColour (juce::Label::textColourId, textColour);
    c.slider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 18);
    c.slider.setColour (juce::Slider::thumbColourId, accent);
    c.slider.setColour (juce::Slider::trackColourId, accent.darker (0.4f));
    c.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    addAndMakeVisible (c.label);
    addAndMakeVisible (c.slider);
    c.attachment = std::make_unique<APVTS::SliderAttachment> (synth.apvts, parameterID, c.slider);
}

void HarmonicsAudioProcessorEditor::showLearnMenu (Control& c)
{
    const int index = c.learnIndex;
    const int cc = synth.controllerFor (index);
    const bool learning = synth.learningIndex() == index;

    juce::PopupMenu menu;
    menu.addSectionHeader (c.text);
    if (learning)
        menu.addItem ("Cancel learning", [this] { synth.cancelLearning(); });
    else
        menu.addItem (cc >= 0 ? "Learn another MIDI CC (now CC " + juce::String (cc) + ")" : "Learn MIDI CC",
                      [this, index] { synth.startLearning (index); });
    menu.addItem ("Forget MIDI CC", cc >= 0, false, [this, index] { synth.forgetController (index); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&c.slider));
}

void HarmonicsAudioProcessorEditor::mouseDown (const juce::MouseEvent& e)
{
    if (! e.mods.isPopupMenu())
        return;
    for (auto* c : { &tilt, &center, &width, &amount, &subs, &spread, &reach, &attack, &release, &volume })
        if (e.eventComponent == &c->label)
            showLearnMenu (*c);
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

    // Show each control's MIDI controller, or that it is waiting for one.
    const int learning = synth.learningIndex();
    for (auto* c : { &tilt, &center, &width, &amount, &subs, &spread, &reach, &attack, &release, &volume })
    {
        if (c->learnIndex < 0)
            continue;
        const int cc = synth.controllerFor (c->learnIndex);
        const auto text = learning == c->learnIndex ? c->text + "  (learning...)"
                          : cc >= 0                 ? c->text + "  [CC " + juce::String (cc) + "]"
                                                    : c->text;
        if (c->label.getText() != text)
        {
            c->label.setText (text, juce::dontSendNotification);
            c->label.setColour (juce::Label::textColourId, learning == c->learnIndex ? accent : textColour);
        }
    }

    amps.repaint();
    panL.repaint();
    panR.repaint();
    windowBar.repaint();
    pitchWheel.repaint();
    modWheel.repaint();
    analyser.update (synth.getSampleRate(),
                     juce::MidiMessage::getMidiNoteInHertz (synth.engine.lastNote.load (std::memory_order_relaxed)));
}

void HarmonicsAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
}

void HarmonicsAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (12);

    auto bottom = area.removeFromBottom (70);
    pitchWheel.setBounds (bottom.removeFromLeft (28));
    bottom.removeFromLeft (6);
    modWheel.setBounds (bottom.removeFromLeft (28));
    bottom.removeFromLeft (10);
    keyboard.setBounds (bottom);
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
    layoutRow (area.removeFromTop (44), 6, { &tilt, &center, &width, &amount, &subs }, nullptr);
    area.removeFromTop (12);

    auto panRow = area.removeFromTop (44);
    layoutButtons (panRow.removeFromLeft (panRow.getWidth() / 2).withTrimmedTop (14).withHeight (26), panButtons, 88);
    layoutRow (panRow, 2, { &spread, &reach }, nullptr);
    area.removeFromTop (12);

    layoutRow (area.removeFromTop (44), 6, { &attack, &release, &volume }, &glide);
    area.removeFromTop (12);

    analyser.setBounds (area);
}
