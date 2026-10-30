# GRAVE PRIX sounds

Every sound of the game except the music is made by the program (`src/Audio.cpp`); there are no sample files.
`GraveSfx <dir>` writes them all as WAV (44.1 kHz, 16 bit) without opening an audio device:

```
cmake --build build/<yours> --config Release --target GraveSfx
build/<yours>/Release/GraveSfx.exe work/sfx
python tools/measure_sfx.py work/sfx [--spectra work/sfx_spectra]
```

- `<effect>.wav` — every `SfxE` (51), as played.
- `loop_<name>.wav` — every `LoopE`: the short loops 4 s, the ambiences 20 s (their bed with a few of their random
  events folded in, so the file still loops).
- `engine_<kind>.wav` — every `EngineE` as a 6 s sweep: idle 1 s, full throttle to the limiter (reached at 3.6 s,
  held to 4 s), off the throttle back to idle in 2 s. The revs and throttle are set once per 735 samples, as the game
  does once a frame.

GraveSfx also prints how long each group takes to make (the effects and the short loops are made at `init()`).

## How they are made

**Effects** (`makeSfx`, the `Fx` workshop): noise through moving state-variable filters, tones with pitch glides
(band-limited saw and square), falling-pitch thumps, modal impacts (decaying sine partials: metal, glass, bells,
coins), random resonant grains (crackle, debris, gravel, shards), FM brass, a drawbar organ, formant filters (horns,
crows), tanh saturation and a small Freeverb-style reverb. Explosions are layered: crack, punch, blast, rumble,
fireball and debris. The horns: *plain* two buzzing diaphragms a third apart; *truck* three air trumpets in a dark
chord; *dixie* a five-trumpet musical horn playing a jaunty tune of our own; *ice* an ice-cream van's worn music box
through a horn speaker, its tape warbling (own tune); *funeral* two bars of a mournful march on organ and brass (own);
*siren* a wail, three yelps and a fall; *klaxon* the a-OO-ga of a ratchet hammering a diaphragm.

**Loops** (`makeShortLoop`, `makeAmbience`, the `Bed`): noise layers are rendered a little longer than the loop and
crossfaded (equal power) over the seam; tones, chirps and placed events are periodic (their frequencies snapped to a
whole number of cycles per loop, the events wrapped round), so they need no crossfade. Slow movements (gusts, swells)
are sums of sines with whole cycles per loop. Each **ambience** is a 20 s stereo bed plus 4-7 event clips (crows,
crushers, owls, bells, gulls, foghorns, frogs, sirens, a broken calliope, artillery, thunder, ...), each made distant
(lowpass and reverb). The mixer drops these events in at random gaps, pans and pitches, so a venue never repeats in
an obvious way. An ambience is made on a worker thread the first time `loop()` asks for it (~0.3 s); until then it
is silent.

**Engines** (`EngineVoice`, synthesised live on the audio thread): a crank phase runs at the cycle frequency (revs /
60 / 2, the rotary per rotor face); each cylinder fires at its place in the cycle and blows an exhaust pulse (a front
of fixed time, a body that is a share of the cycle) into its bank. Firing amounts vary per cylinder (lumpiness at
idle) and per firing (jitter, rare weak firings). Each bank has an exhaust pipe (a comb with the open end's reflection),
then a muffler that opens with throttle and revs, a body resonance, combustion rasp riding on the pulses, tanh
saturation, intake noise, gear whine; per kind a turbo (whistle, whoosh, blow-off, flutter on the boxer), diesel knock,
valve tick, a tin-can rattle, V12 intake howl; overrun pops when lifting off at high revs and a rev limiter that cuts
the ignition at ~12 Hz when held at the limit. The turbine and the electric are made otherwise (whine, spool lag, roar;
motor whine, an inverter stepping up through a few notes, a soft warning hum at a standstill).

| kind | revs | what makes it |
|---|---|---|
| tin | 950-6800 | three cylinders, uneven, buzzy, tin-can rattle, valve tick |
| i4 | 850-7000 | four even pulses, a little overrun crackle |
| boxer | 850-7200 | unequal headers (two banks, pulses long-short: the burble), turbo with fluttering blow-off, anti-lag bangs |
| v6 | 800-7200 | six pulses over two banks of slightly different pipes, raspy |
| v8 | 750-6600 | cross-plane: each bank's pulses uneven, a lumpy cam (big per-cylinder variation, misfires at idle), deep muffler |
| v12 | 1000-9200 | twelve short even pulses, open muffler, intake howl: smooth and screaming |
| diesel | 650-4300 | a truck's straight six, heavy knock (clatter) strongest at idle, gentle turbo |
| rotary | 1000-9000 | two rotors, three faces each, long sharp port pulses, hard saturation, brap at idle, many pops |
| turbine | spool 0-1 | compressor whine 1.9-7 kHz, shaft hum, roar with throttle, the spool lagging 0.5-0.9 s |
| electric | 0-1 | motor whine (6th order), inverter notes at low speed, gear whine, a hum at a standstill |

## Levelling

Each effect's `finish( loud )` sets its loudest 400 ms (K-weighted momentary loudness, as `measure_sfx.py` reports
it as `maxM`) and keeps the peak at or below -3 dBFS; where the peak would go higher the sound is driven into a soft
clipper (at most 10 dB). Change a level by changing the number in its `finish( ... )`. The plan:

| group | maxM (LUFS) |
|---|---|
| explosions, wreck, barrel | -11 .. -12.5 (the loudest) |
| heavy crashes, cannon, rocket, nitro, tyre burst | -14.5 .. -16 |
| gun held down (12 shots/s, `burst` in the report) | about -16 (mg) / -15 (twinmg); one shot -23 / -22 |
| horns | -15 .. -17 |
| race signals | -15 .. -19 |
| pickups | -18 .. -20 |
| small drops, ricochets, bullet hits | -20 .. -26 |
| menus | -24 .. -36 |

The engines are levelled by `gain` in `makeSpec`: at volume 1, full throttle at half revs sits near -18 LUFS
(the turbine -17, the electric -20), idle about 8 dB lower. Short loops are levelled by RMS in `finish( rms, peak )`
of their `Bed`, the ambience beds to -28 dBFS RMS, their events by the peak given to `distant()`.

`measure_sfx.py` lists per file: length, channels, peak, maxM, integrated loudness, DC, a click score (the sharpest
second difference against its neighbourhood: percussive sounds score 10-30, smooth ones 2-5) and for loops where the
step over the seam ranks among all steps (a click at the seam would rank at 100 %); then the gun bursts and, per
engine, level, a rough pitch and the spectral centroid along the sweep. `--spectra` draws spectrograms (0-8 kHz).
