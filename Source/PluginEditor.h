#pragma once

#include "PluginProcessor.h"

// One of the three tables as a bar graph you can draw on with the mouse.
class GraphComponent final : public juce::Component
{
public:
    GraphComponent (harmonics::Table&, float minimum, float maximum, juce::String title);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    void drawAt (juce::Point<float>, bool continuing);

    harmonics::Table& table;
    const float lo, hi;
    const juce::String title;
    int lastSlot = 0;
    float lastValue = 0.0f;
};

// Shows which 129 slots of the tables the last played note uses.
class WindowBar final : public juce::Component
{
public:
    explicit WindowBar (const std::atomic<int>& subs) : subharmonics (subs) {}
    void paint (juce::Graphics&) override;

private:
    const std::atomic<int>& subharmonics;
};

// A pitch or mod wheel: drag it up and down. The pitch wheel springs back to
// the middle when let go; double-click either one to reset it. It also shows
// moves coming in from a MIDI keyboard.
class WheelComponent final : public juce::Component
{
public:
    WheelComponent (std::atomic<float>& value, float minimum, float maximum, bool springBack, juce::String name);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> wheelArea() const;
    float resting() const { return springs ? 0.5f * (lo + hi) : lo; }
    void set (float v);

    std::atomic<float>& value;
    const float lo, hi;
    const bool springs;
    const juce::String title;
    float dragStartValue = 0.0f;
};

// The spectrum of the output, left and right, on a log frequency axis. The
// colour shows where a frequency lies relative to the last played note: green
// at its root, towards blue for harmonics and towards red for subharmonics.
class SpectrumAnalyser final : public juce::Component
{
public:
    explicit SpectrumAnalyser (const OutputTap& t) : tap (t) {}

    void update (double sampleRate, double rootFrequency); // call regularly from the message thread
    void paint (juce::Graphics&) override;

private:
    static constexpr int fftOrder = 13, fftSize = 1 << fftOrder, numBins = fftSize / 2;
    static constexpr float minDb = -96.0f, maxDb = 0.0f, minHz = 20.0f, maxHz = 20000.0f;

    const OutputTap& tap;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::vector<float> scratch = std::vector<float> ((size_t) fftSize * 2);
    std::array<std::vector<float>, 2> levels { std::vector<float> ((size_t) numBins, minDb),
                                               std::vector<float> ((size_t) numBins, minDb) };
    double rate = 44100.0, rootHz = 261.63;
};

class HarmonicsAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit HarmonicsAudioProcessorEditor (HarmonicsAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using APVTS = juce::AudioProcessorValueTreeState;

    struct Control
    {
        juce::Label label;
        juce::Slider slider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
        std::unique_ptr<APVTS::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void addControl (Control&, const juce::String& parameterID, const juce::String& text);
    void addToggle (juce::ToggleButton&, std::unique_ptr<APVTS::ButtonAttachment>&, const juce::String& parameterID);

    HarmonicsAudioProcessor& synth;

    GraphComponent amps, panL, panR;
    WindowBar windowBar;
    SpectrumAnalyser analyser;
    juce::OwnedArray<juce::TextButton> spectrumButtons, panButtons;
    Control tilt, center, width, amount, subs, spread, reach, attack, release, volume;
    juce::ToggleButton glide { "glide" };
    std::unique_ptr<APVTS::ButtonAttachment> glideAttachment;
    WheelComponent pitchWheel, modWheel;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonicsAudioProcessorEditor)
};
