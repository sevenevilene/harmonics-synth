#include "PluginProcessor.h"

#include "PluginEditor.h"

using namespace harmonics;

const juce::StringArray HarmonicsAudioProcessor::spectrumNames {
    "saw", "square", "triangle", "sine", "even", "organ", "bell", "clear", "tilt", "formant", "lfo"
};
const juce::StringArray HarmonicsAudioProcessor::panCurveNames { "centre", "spread", "alternate", "log", "log-side" };

namespace
{
juce::NormalisableRange<float> logRange (float lo, float hi)
{
    return { lo, hi,
             [] (float a, float b, float t) { return a * std::pow (b / a, t); },
             [] (float a, float b, float v) { return std::log (v / a) / std::log (b / a); },
             [] (float a, float b, float v) { return juce::jlimit (a, b, v); } };
}

juce::String tableToString (const Table& table)
{
    juce::StringArray values;
    for (auto& v : table)
        values.add (juce::String (v.load(), 6));
    return values.joinIntoString (" ");
}

void tableFromString (Table& table, const juce::String& text)
{
    const auto values = juce::StringArray::fromTokens (text, " ", {});
    if (values.size() == numSlots)
        for (int i = 0; i < numSlots; ++i)
            table[(size_t) i].store (values[i].getFloatValue());
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout HarmonicsAudioProcessor::createLayout()
{
    using Float = juce::AudioParameterFloat;
    using Range = juce::NormalisableRange<float>;
    const auto id = [] (const char* name) { return juce::ParameterID { name, 1 }; };
    const auto decimals = [] (int places)
    {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction (
            [places] (float v, int) { return juce::String (v, places); });
    };

    // Same names, ranges and defaults as the Pd prototype.
    return {
        std::make_unique<Float> (id ("attack"), "Attack", logRange (1.0f, 2000.0f), 10.0f, decimals (0)),
        std::make_unique<Float> (id ("release"), "Release", logRange (5.0f, 5000.0f), 300.0f, decimals (0)),
        std::make_unique<Float> (id ("volume"), "Volume", Range (0.0f, 1.0f), 0.7f, decimals (2)),
        std::make_unique<Float> (id ("subs"), "Subs", Range (0.0f, 1.0f), 0.5f, decimals (2)),
        std::make_unique<Float> (id ("tilt"), "Tilt", Range (0.0f, 3.0f), 1.0f, decimals (2)),
        std::make_unique<Float> (id ("center"), "Center", Range (1.0f, 32.0f), 8.0f, decimals (1)),
        std::make_unique<Float> (id ("width"), "Width", Range (0.5f, 8.0f), 2.0f, decimals (2)),
        std::make_unique<Float> (id ("speed"), "Speed", Range (0.0f, 4.0f), 1.0f, decimals (2)),
        std::make_unique<juce::AudioParameterBool> (id ("animate"), "Animate", false),
        std::make_unique<Float> (id ("spread"), "Spread", Range (-1.0f, 1.0f), 0.8f, decimals (2)),
        std::make_unique<Float> (id ("reach"), "Reach", logRange (1.0f, 64.0f), 16.0f, decimals (1)),
        std::make_unique<juce::AudioParameterBool> (id ("glide"), "Glide", false),
        std::make_unique<juce::AudioParameterChoice> (id ("spectrum"), "Spectrum", spectrumNames, (int) Spectrum::saw),
        std::make_unique<juce::AudioParameterChoice> (id ("panCurve"), "Pan Curve", panCurveNames, (int) PanCurve::log)
    };
}

HarmonicsAudioProcessor::HarmonicsAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Harmonics", createLayout())
{
    attack = apvts.getRawParameterValue ("attack");
    release = apvts.getRawParameterValue ("release");
    volume = apvts.getRawParameterValue ("volume");
    subs = apvts.getRawParameterValue ("subs");
    tilt = apvts.getRawParameterValue ("tilt");
    center = apvts.getRawParameterValue ("center");
    width = apvts.getRawParameterValue ("width");
    speed = apvts.getRawParameterValue ("speed");
    animate = apvts.getRawParameterValue ("animate");
    spread = apvts.getRawParameterValue ("spread");
    reach = apvts.getRawParameterValue ("reach");
    glide = apvts.getRawParameterValue ("glide");
    spectrum = apvts.getRawParameterValue ("spectrum");
    panCurve = apvts.getRawParameterValue ("panCurve");
}

void HarmonicsAudioProcessor::prepareToPlay (double newSampleRate, int)
{
    sampleRate = newSampleRate;
    engine.prepare (sampleRate);
    keyboardState.reset();
}

bool HarmonicsAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

EnvelopeSettings HarmonicsAudioProcessor::envelopeSettings() const
{
    return { attack->load(), release->load(), glide->load() > 0.5f };
}

HarmonicsAudioProcessor::Controls HarmonicsAudioProcessor::readControls() const
{
    return { spectrum->load(), tilt->load(), center->load(), width->load(),
             subs->load(), panCurve->load(), spread->load(), reach->load() };
}

// Rewrites the tables when a preset, pan curve or one of their sliders changed,
// following the prototype's rules: the algorithm sliders only redraw while an
// algorithm is selected and the pan sliders only while a curve is selected, so
// that hand-drawn tables are kept.
void HarmonicsAudioProcessor::updateTables (int numSamples)
{
    const juce::SpinLock::ScopedTryLockType lock (stateLock);
    if (! lock.isLocked())
        return; // a saved state is being loaded right now

    const auto now = readControls();
    const auto mode = (Spectrum) juce::roundToInt (now.spectrum);
    const auto curve = (PanCurve) juce::roundToInt (now.panCurve);

    bool fillAmps = spectrumRefill.exchange (false);
    bool fillPan = panRefill.exchange (false);

    if (keepTables.exchange (false))
    {
        // The tables were just restored from a saved state: don't redraw them.
    }
    else if (! seenValid)
    {
        fillAmps = fillPan = true;
    }
    else
    {
        const auto changed = [] (float a, float b) { return ! juce::exactlyEqual (a, b); };
        fillAmps = fillAmps || changed (now.spectrum, seen.spectrum) || changed (now.subs, seen.subs)
                   || (mode >= Spectrum::tilt && (changed (now.tilt, seen.tilt) || changed (now.center, seen.center)
                                                  || changed (now.width, seen.width)));
        fillPan = fillPan || changed (now.panCurve, seen.panCurve)
                  || (curve != PanCurve::centre && (changed (now.spread, seen.spread) || changed (now.reach, seen.reach)));
    }

    seen = now;
    seenValid = true;

    if (animate->load() > 0.5f)
    {
        animationCountdown -= numSamples;
        while (animationCountdown <= 0.0)
        {
            animationCountdown += 0.03 * sampleRate;
            animationTime += (double) speed->load() * 0.03;
            fillAmps = fillAmps || mode >= Spectrum::formant;
        }
    }
    else
    {
        animationTime = animationCountdown = 0.0;
    }

    if (fillAmps)
        fillSpectrum (engine.tables.amps, { mode, now.tilt, now.center, now.width, now.subs, (float) animationTime });
    if (fillPan)
        fillPans (engine.tables.panL, engine.tables.panR, curve, now.spread, now.reach);
}

void HarmonicsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();

    if (buffer.getNumChannels() < 2)
    {
        buffer.clear();
        return;
    }

    updateTables (numSamples);
    keyboardState.processNextMidiBuffer (midi, 0, numSamples, true);

    const auto settings = envelopeSettings();
    const float master = volume->load();
    auto* left = buffer.getWritePointer (0);
    auto* right = buffer.getWritePointer (1);
    int pos = 0;

    for (const auto metadata : midi)
    {
        const int at = juce::jlimit (pos, numSamples, metadata.samplePosition);
        engine.render (left + pos, right + pos, at - pos, settings, master);
        pos = at;

        const auto message = metadata.getMessage();
        if (message.isNoteOn())
            engine.noteOn (message.getNoteNumber(), message.getFloatVelocity(), settings);
        else if (message.isNoteOff())
            engine.noteOff (message.getNoteNumber(), settings);
        else if (message.isAllNotesOff() || message.isAllSoundOff())
            engine.allNotesOff (settings);
    }

    engine.render (left + pos, right + pos, numSamples - pos, settings, master);

    for (int channel = 2; channel < buffer.getNumChannels(); ++channel)
        buffer.clear (channel, 0, numSamples);
}

void HarmonicsAudioProcessor::applySpectrum (Spectrum mode)
{
    if (auto* p = apvts.getParameter ("spectrum"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) mode));
        p->endChangeGesture();
    }
    spectrumRefill = true;
}

void HarmonicsAudioProcessor::applyPanCurve (PanCurve curve)
{
    if (auto* p = apvts.getParameter ("panCurve"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 ((float) curve));
        p->endChangeGesture();
    }
    panRefill = true;
}

void HarmonicsAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.removeChild (state.getChildWithName ("Tables"), nullptr);

    juce::ValueTree tables ("Tables");
    tables.setProperty ("amps", tableToString (engine.tables.amps), nullptr);
    tables.setProperty ("panL", tableToString (engine.tables.panL), nullptr);
    tables.setProperty ("panR", tableToString (engine.tables.panR), nullptr);
    state.appendChild (tables, nullptr);

    if (const auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void HarmonicsAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    const auto state = juce::ValueTree::fromXml (*xml);
    const auto tables = state.getChildWithName ("Tables");

    const juce::SpinLock::ScopedLockType lock (stateLock);
    apvts.replaceState (state);

    if (tables.isValid())
    {
        tableFromString (engine.tables.amps, tables["amps"].toString());
        tableFromString (engine.tables.panL, tables["panL"].toString());
        tableFromString (engine.tables.panR, tables["panR"].toString());
        keepTables = true;
    }
}

juce::AudioProcessorEditor* HarmonicsAudioProcessor::createEditor()
{
    return new HarmonicsAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HarmonicsAudioProcessor();
}
