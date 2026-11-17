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
    juce::OwnedArray<juce::TextButton> spectrumButtons, panButtons;
    Control tilt, center, width, speed, subs, spread, reach, attack, release, volume;
    juce::ToggleButton animate { "animate" }, glide { "glide" };
    std::unique_ptr<APVTS::ButtonAttachment> animateAttachment, glideAttachment;
    juce::MidiKeyboardComponent keyboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HarmonicsAudioProcessorEditor)
};
