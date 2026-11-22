#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>

#include "SynthEngine.h"

// The most recent output samples, written by the audio thread and read by the
// spectrum analyser in the editor.
struct OutputTap
{
    static constexpr int size = 1 << 14;

    void push (const float* left, const float* right, int numSamples) noexcept;
    void copyLatest (int channel, float* dest, int numSamples) const noexcept;

private:
    std::array<std::array<std::atomic<float>, size>, 2> samples {};
    std::atomic<int> writePos { 0 };
};

class HarmonicsAudioProcessor final : public juce::AudioProcessor,
                                      private juce::Timer
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

    // The wheels, set by incoming MIDI (pitch bend, CC 1) and by the on-screen
    // wheels; whichever moved last wins. Pitch -1..1, mod 0..1. The mod wheel
    // morphs the formant and lfo algorithms.
    std::atomic<float> pitchWheel { 0.0f }, modWheel { 0.0f };

    // MIDI learn: any of these parameters can follow a MIDI controller (CC 0-119),
    // one controller per parameter. Indices below are into learnableIDs.
    static const juce::StringArray learnableIDs;
    void startLearning (int index) { learning = index; }   // the next CC that moves is assigned
    void cancelLearning() { learning = -1; }
    int learningIndex() const { return learning.load(); }
    void forgetController (int index);
    int controllerFor (int index) const;                    // -1 if none

    juce::AudioProcessorValueTreeState apvts;
    harmonics::Engine engine;
    juce::MidiKeyboardState keyboardState;
    OutputTap outputTap;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void updateTables();
    harmonics::EnvelopeSettings envelopeSettings() const;

    struct Controls
    {
        float spectrum, tilt, center, width, subs, panCurve, spread, reach, morph;
    };
    Controls readControls() const;

    std::atomic<float>* attack, *release, *volume, *subs, *tilt, *center, *width, *amount,
                      *spread, *reach, *glide, *spectrum, *panCurve;

    Controls seen {};
    bool seenValid = false;
    std::atomic<bool> spectrumRefill { false }, panRefill { false }, keepTables { false };
    juce::SpinLock stateLock;
    double sampleRate = 44100.0;

    // controllerTarget[cc] = learnable parameter it drives, or -1. Written by the
    // audio thread (learning) and the message thread (forget, state), read by both.
    std::array<std::atomic<int>, 120> controllerTarget;
    std::atomic<int> learning { -1 };

    // Controller moves wait here (normalised 0..1, < 0 = none) until the message
    // thread hands them to the parameters, so the host is told from its own thread.
    std::array<std::atomic<float>, 16> pendingValue;
    void timerCallback() override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonicsAudioProcessor)
};
