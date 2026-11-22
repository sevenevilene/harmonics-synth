#include "SynthEngine.h"

#include <algorithm>
#include <bit>
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
constexpr float bendSmoothingMs = 10.0f;     // pitch wheel moves

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
            // Morph sweeps the peak up two octaves per unit, evenly in pitch.
            const float peak = s.center * std::exp2 (2.0f * s.morph);
            const float w = std::max (s.width, 0.01f);
            return std::min (1.0f, 0.3f / fn + std::exp (-(fn - peak) * (fn - peak) / (2.0f * w * w)));
        }
        case Spectrum::lfo:
        {
            // Morph moves each harmonic along its own fade cycle: one unit takes
            // the 2nd harmonic through about one cycle and higher ones through more.
            const float phase = s.morph * 4.0f * (float) pi;
            return n == 1 ? 1.0f
                          : std::pow (fn, -s.tilt) * (0.5f + 0.5f * std::sin (phase * (0.3f + 0.07f * fn) + fn * 2.4f));
        }
    }
    return 0.0f;
}
} // namespace

void fillSpectrum (Table& amps, const SpectrumSettings& s)
{
    // The partials a preset uses alternate in sign, root positive, so every
    // shape fills the graph's whole -1..1 range symmetrically. With every
    // partial starting in sine phase these are the textbook series: saw
    // (1, -1/2, 1/3 ...) and triangle (1, -1/9, 1/25 ...) come out as the true
    // waveforms. The algorithms count every partial, so their signs stay put
    // while they morph.
    std::array<float, numPartials> levels {};
    int used = 0;
    for (int n = 1; n <= numPartials; ++n)
    {
        const float level = levelFor (s, n);
        levels[(size_t) n - 1] = (used % 2) && level != 0.0f ? -level : level;
        if (s.mode >= Spectrum::tilt || level != 0.0f)
            ++used;
    }

    // Mirrored: subharmonic 1/n gets the same level as harmonic n, scaled by subs.
    for (int i = 0; i < numSlots; ++i)
    {
        const int n = std::abs (i - rootSlot) + 1;
        amps[(size_t) i].store (levels[(size_t) n - 1] * (i < rootSlot ? s.subs : 1.0f), std::memory_order_relaxed);
    }
}

namespace
{
// Distance from the root in octaves: log2 n, negative for subharmonics.
float octavesFromRoot (int d)
{
    return d < 0 ? -std::log2 ((float) (1 - d)) : std::log2 ((float) (d + 1));
}

// Spread and log-side send the harmonics one way, so the louder harmonics would
// pull the sound to that side. Each has a knob (pivot, or how far the bass
// moves the other way) that shifts energy back; this finds the setting where
// a middle-C note's levels balance between the speakers. xFor (d, knob) is the
// position of the partial d slots from the root, before spread is applied,
// and the result must lean less towards the harmonics' side as knob rises.
template <typename Position>
float balance (const Table& amps, float spread, float lo, float hi, Position xFor)
{
    const int first = rootSlot - subharmonicsFor (261.63);
    std::array<float, numPartials> energy {};
    double total = 0.0;
    for (int p = 0; p < numPartials; ++p)
    {
        const float a = amps[(size_t) (first + p)].load (std::memory_order_relaxed);
        energy[(size_t) p] = a * a;
        total += (double) (a * a);
    }
    if (total < 1.0e-12 || std::abs (spread) <= 0.0f)
        return std::clamp (0.0f, lo, hi); // silent, or no stereo at all: leave it neutral

    const auto lean = [&] (float knob)
    {
        double sum = 0.0;
        for (int p = 0; p < numPartials; ++p)
            sum += (double) energy[(size_t) p]
                   * std::sin (xFor (first + p - rootSlot, knob) * std::abs (spread) * (float) pi * 0.5f);
        return sum;
    };

    if (lean (lo) <= 0.0) return lo;
    if (lean (hi) >= 0.0) return hi;
    for (int step = 0; step < 20; ++step)
    {
        const float mid = 0.5f * (lo + hi);
        (lean (mid) > 0.0 ? lo : hi) = mid;
    }
    return 0.5f * (lo + hi);
}
} // namespace

void fillPans (Table& panL, Table& panR, PanCurve curve, float spread, float reach, const Table& amps)
{
    // Spread: subharmonics left, harmonics right, by octave, most of the way
    // out (tanh 1.5 = 90%) at harmonic "reach", pivoting where the levels balance.
    const float spreadWidth = std::max (std::log2 (std::max (reach, 2.0f)), 1.0f) / 1.5f;
    const auto spreadX = [spreadWidth] (int d, float pivot) { return std::tanh ((octavesFromRoot (d) - pivot) / spreadWidth); };

    // Log: subharmonics, the root and 2x centred; from 3x up the harmonics
    // widen evenly per octave, fully wide at harmonic "reach".
    const float logReach = std::log (std::max (reach, 4.0f) / 2.0f);
    const auto logWidth = [logReach] (int d)
    {
        return d > 0 ? std::clamp (std::log ((float) (d + 1) / 2.0f) / logReach, 0.0f, 1.0f) : 0.0f;
    };

    // Log-side: the log widths, all towards one side. To balance, the knob
    // (0..2) first moves the centred bass towards the other side (up to fully
    // out at 1), then narrows the harmonics (down to centre at 2).
    const auto logSideX = [&logWidth] (int d, float knob)
    {
        const float w = logWidth (d) * std::min (1.0f, 2.0f - knob);
        return w - (1.0f - logWidth (d)) * std::min (knob, 1.0f);
    };

    const float knob = curve == PanCurve::spread  ? balance (amps, spread, -2.0f, 2.0f, spreadX)
                     : curve == PanCurve::logSide ? balance (amps, spread, 0.0f, 2.0f, logSideX)
                                                  : 0.0f;

    for (int i = 0; i < numSlots; ++i)
    {
        const int d = i - rootSlot;         // distance from the root, negative = subharmonic
        const int a = std::abs (d);
        const float side = (a % 2) ? 1.0f : -1.0f;  // alternate: neighbours take turns
        float x = 0.0f;                     // -1 left, 0 centre, 1 right

        switch (curve)
        {
            case PanCurve::centre: break;
            case PanCurve::spread:    x = spreadX (d, knob); break;
            case PanCurve::alternate: x = std::tanh ((float) a / std::max (reach, 1.0f)) * side; break;
            case PanCurve::logSide:   x = logSideX (d, knob); break;
            case PanCurve::log:
                // Each harmonic's side comes from the Thue-Morse sequence (the
                // parity of the bits of n - 1): neighbours mostly take turns,
                // and unlike plain odd/even turns the octaves of the root don't
                // all land on one side, so smooth and sparse spectra alike stay
                // balanced.
                x = logWidth (d) * ((std::popcount ((unsigned) a) % 2) ? 1.0f : -1.0f);
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

void Voice::render (float* left, float* right, int n, float bend)
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
        const uint32_t inc = bend == 1.0f ? increment[i] : (uint32_t) ((float) increment[i] * bend);
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
    fillPans (tables.panL, tables.panR, PanCurve::log, 0.8f, 16.0f, tables.amps);
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
    const float bendCoeff = 1.0f - std::exp (-(float) Voice::maxChunk / (float) (bendSmoothingMs * 0.001 * sampleRate));

    for (int pos = 0; pos < numSamples; pos += Voice::maxChunk)
    {
        const int n = std::min (Voice::maxChunk, numSamples - pos);

        // The bend glides towards the wheel once per chunk, landing exactly on it.
        bend += (bendTarget - bend) * bendCoeff;
        if (std::abs (bendTarget - bend) < 1.0e-5f)
            bend = bendTarget;

        for (auto& v : voices)
        {
            if (! v.active)
                continue;
            v.refresh (tables, s); // follow the tables while the note sounds
            v.render (left + pos, right + pos, n, bend);
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
