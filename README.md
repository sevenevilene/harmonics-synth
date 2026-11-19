# harmonics-synth

An additive synthesizer plugin (AU, VST3 and standalone app) built with [JUCE](https://juce.com/). Each note is a sum of 129 sine waves: subharmonics below the root, the root, and harmonics above it. Their levels and stereo positions live in three tables that you can draw on, fill from a preset, or have an algorithm write and animate.

The synth started as a Pure Data patch, which is kept in [prototype/](prototype/) with its own [README](prototype/README.md). That README explains the sound design in detail (moving root, presets, algorithms, pan curves, glide); the plugin follows it.

## How a note is calculated

For a MIDI note with frequency *f*:

```
out(t) = envelope(t) × Σ amps[slot] × pan[slot] × sin(2π × f × ratio[slot] × t)
```

summed over the note's 129 slots, where `ratio` is 1/n for a subharmonic, 1 for the root and n for a harmonic. The tables hold 257 slots (128 subharmonics, the root, 128 harmonics) and each note uses a 129-slot window that slides with pitch, so the partials stay between 20 Hz and 18 kHz. Level changes are ramped over 30 ms so drawing on a table doesn't click.

## Building

You need CMake 3.22 or newer and a C++20 compiler (on macOS the Xcode command line tools are enough). JUCE 9.0.5 is downloaded by CMake the first time you configure.

```bash
cmake -S . -B build
```

```bash
cmake --build build -j 8
```

The results are in `build/Harmonics_artefacts/Release/`: `Standalone/Harmonics.app`, `AU/Harmonics.component` and `VST3/Harmonics.vst3`. To have them copied into `~/Library/Audio/Plug-Ins` after each build so that a DAW finds them, configure with `-DHARMONICS_COPY_PLUGINS=ON`.

## Using it

Play from a MIDI keyboard or click the on-screen one. Draw on the three graphs with the mouse; click a preset or pan curve to overwrite them. The algorithm sliders (tilt, center, width) only redraw the levels while tilt, formant or lfo is selected, and spread and reach only redraw the pans while a curve other than centre is selected. Moving subs always redraws the levels. The tables are saved with the plugin state.

The graph above the keyboard is a spectrum analyser showing what actually comes out (left channel bright, right channel faint), from 20 Hz to 20 kHz and 0 dB (full scale) down to -96 dB. The loudest peaks are labelled with their frequency in Hz. The colour shows where a frequency lies relative to the last played note: green at its root, towards blue for harmonics and towards red for subharmonics, reaching full blue and red five octaves away.

All controls are automation parameters: Attack, Release, Volume, Subs, Tilt, Center, Width, Speed, Animate, Spread, Reach, Glide, Spectrum and Pan Curve.

## Files

- `CMakeLists.txt`: the JUCE plugin project
- `Source/SynthEngine.h/.cpp`: the synth itself (tables, presets, pan curves, voices), plain C++ with no JUCE dependency
- `Source/PluginProcessor.h/.cpp`: parameters, MIDI, state saving
- `Source/PluginEditor.h/.cpp`: the window (graphs, buttons, sliders, spectrum analyser, keyboard)
- `prototype/`: the original Pure Data patch
