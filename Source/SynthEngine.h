#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

// The synth itself, with no JUCE dependency.
//
// Every note is a plain sum of sine waves:
//
//     out(t) = envelope(t) * sum over the note's 129 slots of
//              amps[slot] * pan[slot] * sin (2 pi * f * ratio[slot] * t)
//
// where f is the note's frequency and ratio is 1/n for a subharmonic, 1 for
// the root and n for a harmonic. The tables hold 257 slots (128 subharmonics,
// the root at slot 128, 128 harmonics); each note uses a 129-slot window of
// them that slides with pitch so the partials stay audible.
namespace harmonics
{
constexpr int numSlots = 257;
constexpr int rootSlot = 128;
constexpr int numPartials = 129;
constexpr int numVoices = 8;

enum class Spectrum { saw, square, triangle, sine, even, organ, bell, clear, tilt, formant, lfo };
enum class PanCurve { centre, spread, alternate, log, logSide };

// Written by the UI thread (drawing) and the audio thread (presets), read by both.
using Table = std::array<std::atomic<float>, numSlots>;

struct Tables
{
    Table amps;  // level of each partial, -1..1 (negative = inverted phase)
    Table panL;  // share of each partial in the left speaker, 0..1
    Table panR;  // ... and in the right speaker
};

struct SpectrumSettings
{
    Spectrum mode = Spectrum::saw;
    float tilt = 1.0f, center = 8.0f, width = 2.0f, subs = 0.5f, time = 0.0f;
};

void fillSpectrum (Table& amps, const SpectrumSettings&);
void fillPans (Table& panL, Table& panR, PanCurve, float spread, float reach);

// How many of a note's 129 partials are subharmonics.
int subharmonicsFor (double frequency);

struct EnvelopeSettings
{
    float attackMs = 10.0f, releaseMs = 300.0f;
    bool glide = false;
};

class Voice
{
public:
    void prepare (double newSampleRate) { sampleRate = newSampleRate; }
    void start (int midiNote, float velocity, const Tables&, const EnvelopeSettings&);
    void release (const EnvelopeSettings&);
    void refresh (const Tables&, const EnvelopeSettings&, bool snap = false);
    // Adds numSamples (<= maxChunk) of output; bend scales every partial's frequency.
    void render (float* left, float* right, int numSamples, float bend);

    static constexpr int maxChunk = 64;

    bool active = false, held = false;
    int note = -1, subharmonics = 0;
    uint64_t serial = 0;

private:
    struct Ramp
    {
        float value = 0.0f, target = 0.0f, delta = 0.0f;
        int remaining = 0;
    };

    void setTarget (Ramp&, float target, float ms);
    int samplesFor (float ms) const;

    double sampleRate = 44100.0;
    std::array<uint32_t, numPartials> phase {}, increment {};
    std::array<int, numPartials> slot {};
    std::array<bool, numPartials> audible {};
    std::array<Ramp, numPartials> gainL, gainR;
    Ramp envelope;
    bool frozen = false;     // glide release: the levels fade out and stop following the tables
    int offCountdown = -1;   // glide release: samples until the voice switches off
};

class Engine
{
public:
    Engine();

    void prepare (double sampleRate);
    void noteOn (int midiNote, float velocity, const EnvelopeSettings&);
    void noteOff (int midiNote, const EnvelopeSettings&);
    void allNotesOff (const EnvelopeSettings&);

    // Pitch wheel position, -1..1, bending every note by up to bendRange semitones.
    void setPitchBend (float wheel) { bendTarget = std::exp2 (wheel * bendRange / 12.0f); }
    static constexpr float bendRange = 2.0f;

    // Overwrites left and right with numSamples of output.
    void render (float* left, float* right, int numSamples, const EnvelopeSettings&, float masterVolume);

    Tables tables;
    std::atomic<int> lastSubharmonics { 0 }; // window of the last note played, for the display
    std::atomic<int> lastNote { 60 };        // ... and its MIDI note number

private:
    std::array<Voice, numVoices> voices;
    double sampleRate = 44100.0;
    uint64_t counter = 0;
    float master = 0.0f;
    float bendTarget = 1.0f, bend = 1.0f; // frequency ratios
};
} // namespace harmonics
