#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace harmonics
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr int sineBits = 11;
constexpr int sineSize = 1 << sineBits;
constexpr int fracBits = 32 - sineBits;
constexpr float fracScale = 1.0f / (float) (1u << fracBits);
constexpr float voiceGain = 0.0424f;   // headroom for 8 voices of 129 partials
constexpr float smoothingMs = 30.0f;   // level changes while a note sounds
constexpr float masterSmoothingMs = 50.0f;

const std::array<float, sineSize + 1>& sineTable()
{
    static const auto table = []
    {
        std::array<float, sineSize + 1> t {};
        for (int i = 0; i <= sineSize; ++i)
            t[(size_t) i] = (float) std::sin (2.0 * pi * i / sineSize);
        return t;
    }();
    return table;
}

float levelFor (const SpectrumSettings& s, int n)
{
    const auto fn = (float) n;
    const bool odd = (n % 2) == 1;
    const auto is = [n] (int k) { return n == k ? 1.0f : 0.0f; };

    switch (s.mode)
    {
        case Spectrum::saw:      return 1.0f / fn;
        case Spectrum::square:   return odd ? 1.0f / fn : 0.0f;
        case Spectrum::triangle: return odd ? 1.0f / (fn * fn) : 0.0f;
        case Spectrum::sine:     return is (1);
        case Spectrum::even:     return (n == 1 || ! odd) ? 1.0f / fn : 0.0f;
        case Spectrum::organ:    return is (1) + 0.8f * is (2) + 0.6f * is (3) + 0.5f * is (4)
                                        + 0.4f * is (6) + 0.35f * is (8) + 0.2f * is (12) + 0.2f * is (16);
        case Spectrum::bell:     return 0.6f * is (1) + 0.3f * is (2) + 0.5f * is (3) + 0.45f * is (5)
                                        + 0.35f * is (7) + 0.3f * is (9) + 0.2f * is (11) + 0.15f * is (13);
        case Spectrum::clear:    return 0.0f;
        case Spectrum::tilt:     return std::pow (fn, -s.tilt);
        case Spectrum::formant:
        {
            const float peak = s.center * (1.0f + 0.5f * std::sin (s.time));
            const float w = std::max (s.width, 0.01f);
            return std::min (1.0f, 0.3f / fn + std::exp (-(fn - peak) * (fn - peak) / (2.0f * w * w)));
        }
        case Spectrum::lfo:
            return n == 1 ? 1.0f
                          : std::pow (fn, -s.tilt) * (0.5f + 0.5f * std::sin (s.time * (0.3f + 0.07f * fn) + fn * 2.4f));
    }
    return 0.0f;
}
} // namespace

void fillSpectrum (Table& amps, const SpectrumSettings& s)
{
    // Mirrored: subharmonic 1/n gets the same level as harmonic n, scaled by subs.
    for (int i = 0; i < numSlots; ++i)
    {
        const int n = std::abs (i - rootSlot) + 1;
        amps[(size_t) i].store (levelFor (s, n) * (i < rootSlot ? s.subs : 1.0f), std::memory_order_relaxed);
    }
}

void fillPans (Table& panL, Table& panR, PanCurve curve, float spread, float reach)
{
    for (int i = 0; i < numSlots; ++i)
    {
        const int d = i - rootSlot;         // distance from the root, negative = subharmonic
        const int a = std::abs (d);
        const float side = (a % 2) ? 1.0f : -1.0f;
        float x = 0.0f;                     // -1 left, 0 centre, 1 right

        switch (curve)
        {
            case PanCurve::centre: break;
            case PanCurve::spread:    x = std::tanh ((float) d / std::max (reach, 1.0f)); break;
            case PanCurve::alternate: x = std::tanh ((float) a / std::max (reach, 1.0f)) * side; break;
            case PanCurve::log:
            case PanCurve::logSide:
                // Subharmonics, the root and 2x stay centred; from 3x up the
                // harmonics widen evenly per octave, fully wide at harmonic "reach".
                if (d > 0)
                {
                    const float width = std::log ((float) (a + 1) / 2.0f) / std::log (std::max (reach, 4.0f) / 2.0f);
                    x = std::clamp (width, 0.0f, 1.0f) * (curve == PanCurve::log ? side : 1.0f);
                }
                break;
        }

        // Equal-power pan law.
        const double angle = ((double) (x * spread) + 1.0) * pi / 4.0;
        panL[(size_t) i].store ((float) std::cos (angle), std::memory_order_relaxed);
        panR[(size_t) i].store ((float) std::sin (angle), std::memory_order_relaxed);
    }
}

int subharmonicsFor (double f)
{
    // Audible subharmonics reach down to 20 Hz, audible harmonics up to 18 kHz.
    // The 128 non-root partials are shared out in that proportion, never
    // giving the subharmonic side more than can be heard.
    const int subs = std::max (0, (int) (f / 20.0) - 1);
    const int harmonics = std::max (0, (int) (18000.0 / f) - 1);
    return std::min (subs, (int) (128.0 * subs / std::max (1, subs + harmonics) + 0.5));
}

//==============================================================================
int Voice::samplesFor (float ms) const
{
    return std::max (1, (int) (ms * 0.001 * sampleRate));
}

void Voice::setTarget (Ramp& r, float target, float ms)
{
    r.target = target;
    r.remaining = samplesFor (ms);
    r.delta = (target - r.value) / (float) r.remaining;
}

void Voice::start (int midiNote, float velocity, const Tables& tables, const EnvelopeSettings& s)
{
    const bool wasSilent = ! active;
    const double f = 440.0 * std::pow (2.0, (midiNote - 69) / 12.0);

    note = midiNote;
    held = active = true;
    frozen = false;
    offCountdown = -1;
    subharmonics = subharmonicsFor (f);

    for (int p = 0; p < numPartials; ++p)
    {
        const auto i = (size_t) p;
        const int offset = p - subharmonics; // negative = subharmonic
        const double pf = f * (offset < 0 ? 1.0 / (1 - offset) : (double) (offset + 1));

        slot[i] = rootSlot + offset;
        audible[i] = pf > 20.0 && pf < 18000.0 && pf < 0.5 * sampleRate;
        increment[i] = audible[i] ? (uint32_t) (pf / sampleRate * 4294967296.0) : 0u;
        phase[i] = 0; // every partial restarts in sine phase, so a negative level is a true inversion

        if (s.glide) // every level starts from 0 and rises over the attack time
            gainL[i] = gainR[i] = {};
    }

    refresh (tables, s, wasSilent && ! s.glide);
    setTarget (envelope, velocity, s.glide ? 1.0f : s.attackMs);
}

void Voice::release (const EnvelopeSettings& s)
{
    held = false;

    if (s.glide)
    {
        // The partials fade out themselves; the envelope holds, then resets.
        frozen = true;
        offCountdown = samplesFor (s.releaseMs + 50.0f);
        for (int p = 0; p < numPartials; ++p)
        {
            setTarget (gainL[(size_t) p], 0.0f, s.releaseMs);
            setTarget (gainR[(size_t) p], 0.0f, s.releaseMs);
        }
    }
    else
    {
        setTarget (envelope, 0.0f, s.releaseMs);
    }
}

void Voice::refresh (const Tables& tables, const EnvelopeSettings& s, bool snap)
{
    if (frozen)
        return;

    const auto update = [&] (Ramp& r, float target)
    {
        if (snap)
            r = { target, target, 0.0f, 0 };
        else if (std::abs (target - r.target) > 0.0f)
            setTarget (r, target, ! s.glide ? smoothingMs
                                            : std::abs (target) > std::abs (r.target) ? s.attackMs : s.releaseMs);
    };

    for (int p = 0; p < numPartials; ++p)
    {
        const auto i = (size_t) p;
        const auto k = (size_t) slot[i];
        const float amp = audible[i] ? tables.amps[k].load (std::memory_order_relaxed) : 0.0f;
        update (gainL[i], amp * tables.panL[k].load (std::memory_order_relaxed));
        update (gainR[i], amp * tables.panR[k].load (std::memory_order_relaxed));
    }
}

void Voice::render (float* left, float* right, int n)
{
    float sumL[maxChunk] = {}, sumR[maxChunk] = {};
    const auto& sine = sineTable();
    const float invN = 1.0f / (float) n;

    // Moves a ramp n samples on and returns the value it started from.
    const auto advance = [n] (Ramp& r)
    {
        const float from = r.value;
        if (r.remaining > n)       { r.value += r.delta * (float) n; r.remaining -= n; }
        else if (r.remaining > 0)  { r.value = r.target; r.remaining = 0; }
        return from;
    };

    for (int p = 0; p < numPartials; ++p)
    {
        const auto i = (size_t) p;
        float l = advance (gainL[i]), r = advance (gainR[i]);
        const float l1 = gainL[i].value, r1 = gainR[i].value;
        uint32_t ph = phase[i];
        const uint32_t inc = increment[i];
        phase[i] = ph + inc * (uint32_t) n;

        if (l == 0.0f && l1 == 0.0f && r == 0.0f && r1 == 0.0f)
            continue;

        const float dl = (l1 - l) * invN, dr = (r1 - r) * invN;

        for (int s = 0; s < n; ++s)
        {
            const uint32_t index = ph >> fracBits;
            const float frac = (float) (ph & ((1u << fracBits) - 1u)) * fracScale;
            const float v = sine[index] + frac * (sine[index + 1] - sine[index]);
            sumL[s] += v * l;
            sumR[s] += v * r;
            l += dl;
            r += dr;
            ph += inc;
        }
    }

    for (int s = 0; s < n; ++s)
    {
        if (envelope.remaining > 0)
        {
            envelope.value += envelope.delta;
            if (--envelope.remaining == 0)
                envelope.value = envelope.target;
        }
        left[s] += sumL[s] * envelope.value;
        right[s] += sumR[s] * envelope.value;
    }

    if (offCountdown >= 0)
    {
        offCountdown -= n;
        if (offCountdown < 0)
        {
            envelope = {};
            active = false;
        }
    }
    else if (! held && envelope.remaining == 0 && envelope.value == 0.0f)
    {
        active = false;
    }
}

//==============================================================================
Engine::Engine()
{
    fillSpectrum (tables.amps, {});
    fillPans (tables.panL, tables.panR, PanCurve::log, 0.8f, 16.0f);
}

void Engine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    for (auto& v : voices)
    {
        v = {};
        v.prepare (sampleRate);
    }
}

void Engine::noteOn (int midiNote, float velocity, const EnvelopeSettings& s)
{
    // A free voice if there is one (a silent one first, else the one released
    // longest ago), otherwise steal the oldest note.
    const auto rank = [] (const Voice& v) { return (v.held ? 2 : v.active ? 1 : 0); };
    Voice* pick = &voices[0];
    for (auto& v : voices)
        if (rank (v) < rank (*pick) || (rank (v) == rank (*pick) && v.serial < pick->serial))
            pick = &v;

    pick->serial = ++counter;
    pick->start (midiNote, velocity, tables, s);
    lastSubharmonics.store (pick->subharmonics, std::memory_order_relaxed);
    lastNote.store (midiNote, std::memory_order_relaxed);
}

void Engine::noteOff (int midiNote, const EnvelopeSettings& s)
{
    Voice* pick = nullptr;
    for (auto& v : voices)
        if (v.held && v.note == midiNote && (pick == nullptr || v.serial < pick->serial))
            pick = &v;

    if (pick != nullptr)
    {
        pick->serial = ++counter;
        pick->release (s);
    }
}

void Engine::allNotesOff (const EnvelopeSettings& s)
{
    for (auto& v : voices)
        if (v.held)
            v.release (s);
}

void Engine::render (float* left, float* right, int numSamples, const EnvelopeSettings& s, float masterVolume)
{
    std::fill_n (left, numSamples, 0.0f);
    std::fill_n (right, numSamples, 0.0f);

    const float masterStep = 1.0f / (float) std::max (1.0, masterSmoothingMs * 0.001 * sampleRate);

    for (int pos = 0; pos < numSamples; pos += Voice::maxChunk)
    {
        const int n = std::min (Voice::maxChunk, numSamples - pos);

        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            v.refresh (tables, s); // follow the tables while the note sounds
            v.render (left + pos, right + pos, n);
        }

        for (int i = pos; i < pos + n; ++i)
        {
            master += std::clamp (masterVolume - master, -masterStep, masterStep);
            left[i] = std::clamp (left[i] * voiceGain * master, -1.0f, 1.0f);
            right[i] = std::clamp (right[i] * voiceGain * master, -1.0f, 1.0f);
        }
    }
}
} // namespace harmonics
