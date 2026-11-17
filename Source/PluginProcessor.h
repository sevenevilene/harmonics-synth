#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "SynthEngine.h"

class HarmonicsAudioProcessor final : public juce::AudioProcessor
{
public:
    HarmonicsAudioProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // Select a preset / pan curve and write it into the tables, even if it is
    // already the selected one (this replaces whatever was drawn by hand).
    void applySpectrum (harmonics::Spectrum);
    void applyPanCurve (harmonics::PanCurve);

    static const juce::StringArray spectrumNames, panCurveNames;

    juce::AudioProcessorValueTreeState apvts;
    harmonics::Engine engine;
    juce::MidiKeyboardState keyboardState;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateTables (int numSamples);
    harmonics::EnvelopeSettings envelopeSettings() const;

    struct Controls
    {
        float spectrum, tilt, center, width, subs, panCurve, spread, reach;
    };
    Controls readControls() const;

    std::atomic<float>* attack, *release, *volume, *subs, *tilt, *center, *width, *speed,
                      *animate, *spread, *reach, *glide, *spectrum, *panCurve;

    Controls seen {};
    bool seenValid = false;
    std::atomic<bool> spectrumRefill { false }, panRefill { false }, keepTables { false };
    juce::SpinLock stateLock;
    double sampleRate = 44100.0, animationTime = 0.0, animationCountdown = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonicsAudioProcessor)
};
