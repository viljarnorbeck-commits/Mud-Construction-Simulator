"""Render the Valley's original sound effects as 48 kHz mono WAV files."""

from array import array
from math import cos, exp, pi, sin, tanh
from pathlib import Path
from random import Random
import sys
import wave


RATE = 48000
TAU = 2 * pi
ENGINES = {
    # Firing frequency, body resonance, exhaust thump, mechanical rattle, turbo.
    "EngineDozer": (36, 62, 0.88, 0.52, 0.10),
    "EngineExcavator": (45, 100, 0.45, 0.20, 0.11),
    "EngineDumper": (33, 76, 1.00, 0.17, 0.23),
    "EngineTruck": (39, 58, 0.76, 0.29, 0.33),
    "EngineLoader": (48, 124, 0.62, 0.34, 0.39),
}
LOOPS = {
    **{f"{name}{suffix}": 3 for name in ENGINES for suffix in ("", "Load")},
    "Tyres": 4, "Tracks": 3, "TrackScrub": 4, "Hydraulic": 3, "Soil": 4, "Gravel": 4, "GravelLift": 3,
    "WaterVehicle": 4, "Reverse": 1, "Wind": 10, "River": 8,
}
CUES = {
    "Door": 0.52, "Switch": 0.16, "Ignition": 1.05, "Impact": 0.68, "Tree": 1.55,
    "Goal": 1.12, "Notice": 0.29, "Brake": 0.60, "Jump": 0.29, "Land": 0.40, "BirdA": 1.5, "BirdB": 2.1,
    "TruckBrakeRelease": 0.57, "TruckAirPurge": 0.96,
    **{f"Step{ground}{variant}": 0.43 for ground in ("Soil", "Mud", "Stone", "Water") for variant in "AB"},
}


def tone(frequency, t):
    return sin(TAU * frequency * t)


def noise_bands(count, rng, frequencies, loop):
    gains = [1 - exp(-TAU * frequency / RATE) for frequency in frequencies]
    white = [rng.uniform(-1, 1) for _ in range(count)]
    states = [0.0] * len(gains)
    bands = []
    # Warm the filters with the end of the period so the join retains the same texture.
    for sample in (white[-min(count, RATE):] if loop else []):
        states = [state + gain * (sample - state) for gain, state in zip(gains, states)]
    for sample in white:
        states = [state + gain * (sample - state) for gain, state in zip(gains, states)]
        bands.append((states[0], states[1] - states[0], states[2] - states[1], sample - states[2]))
    return bands


def grains(count, duration, rng, density, decay, pitch, loop=False):
    signal = [0.0] * count
    for _ in range(max(1, round(density * duration))):
        start = rng.randrange(count)
        frequency = rng.uniform(*pitch)
        amplitude = rng.uniform(0.18, 1.0)
        length = rng.uniform(*decay)
        tail = round(length * RATE * 4)
        for i in range(tail if loop else min(tail, count - start)):
            t = i / RATE
            ring = tone(frequency, t) + 0.35 * tone(frequency * 1.71, t)
            signal[(start + i) % count] += amplitude * ring * exp(-t / length) * min(1.0, i / 8)
    return signal


def motor(t, firing, resonance, weight, rattle, turbo, low, mid, high, loaded=False):
    wobble = 0.075 * tone(2 / 3, t) + 0.037 * tone(5 / 3, t)
    fire = TAU * firing * t + wobble
    pulse = (0.5 + 0.5 * cos(fire)) ** (5 if loaded else 9)
    pulse *= 0.80 + 0.20 * cos(fire / 3)
    rumble = 0.34 * sin(fire - 0.55) + 0.18 * sin(fire / 3 - 0.3) + 0.18 * tone(resonance, t)
    clatter = (0.28 * mid + 0.21 * high + 0.085 * sin(fire * 7)) * (0.26 + pulse) * rattle
    exhaust = weight * (rumble + (0.95 if loaded else 0.39) * (pulse - 0.18))
    if loaded:
        turbo_phase = TAU * (resonance * 7 + 360) * t + 0.37 * tone(7 / 3, t)
        spool = turbo * (0.10 * sin(turbo_phase) + 0.38 * high)
        return exhaust + clatter + spool + 0.82 * low + (0.42 + 0.72 * pulse) * mid
    return exhaust + 1.28 * clatter + 0.57 * low + 0.26 * mid


GRAINS = {
    "Tracks": (60, (0.0025, 0.012), (260, 2100)),
    "TrackScrub": (49, (0.004, 0.017), (240, 1340)),
    "Soil": (52, (0.004, 0.018), (75, 430)),
    "Gravel": (150, (0.0015, 0.008), (300, 2900)),
    "GravelLift": (83, (0.0014, 0.007), (540, 3500)),
    "WaterVehicle": (78, (0.002, 0.010), (180, 870)),
    "River": (100, (0.002, 0.008), (270, 2100)),
    "Door": (25, (0.004, 0.024), (170, 1250)),
    "Tree": (53, (0.002, 0.014), (85, 1900)),
    "Stone": (120, (0.001, 0.008), (270, 2800)),
    "Mud": (56, (0.004, 0.014), (95, 400)),
    "Water": (130, (0.001, 0.012), (210, 1300)),
}


def render_loop(name, t, low, mid, high, air, detail):
    if name.startswith("Engine"):
        return motor(t, *ENGINES[name.removesuffix("Load")], low, mid, high, loaded=name.endswith("Load"))
    if name == "Tyres":
        return (1 + 0.24 * tone(5, t) + 0.13 * tone(12, t)) * (1.5 * low + 0.86 * mid + 0.13 * high)
    if name == "Tracks":
        links = (0.5 + 0.5 * tone(15, t + 0.0014 * tone(2, t))) ** 8
        impacts = links * (0.67 + 0.33 * tone(7 / 3, t))
        steel = 0.17 * tone(339, t) + 0.11 * tone(785, t)
        return 1.42 * low + (0.30 + impacts * 1.6) * mid + (0.18 + impacts) * detail + impacts * steel
    if name == "TrackScrub":
        drag = 0.72 + 0.19 * tone(1.25, t) + 0.09 * tone(4.75, t)
        slip = (0.5 + 0.5 * tone(2.75, t)) ** 6
        squeak = sin(TAU * 483 * t + 3.2 * tone(1.5, t))
        return drag * (1.27 * low + 0.66 * mid + 0.42 * detail) + slip * (0.18 * squeak + 0.78 * high)
    if name == "Hydraulic":
        modulation = 0.72 + 0.20 * tone(3, t) + 0.08 * tone(7, t)
        return modulation * (0.31 * tone(178, t) + 0.21 * tone(356, t) + 0.10 * tone(713, t)) + 0.66 * mid + 0.20 * high
    if name == "Reverse":
        envelope = max(0, min(t / 0.017, (0.22 - t) / 0.029, 1.0))
        return envelope * (0.76 * tone(1130, t) + 0.15 * tone(2260, t) + 0.07 * tone(1065, t))
    if name == "Soil":
        return (0.85 + 0.15 * tone(2, t)) * (2.0 * low + 1.42 * mid + 0.32 * high) + 0.32 * detail
    if name == "Gravel":
        return (0.88 + 0.12 * tone(3.5, t)) * (0.87 * low + 1.0 * mid + 0.76 * high) + 0.73 * detail
    if name == "GravelLift":
        cascade = 0.57 + 0.29 * tone(10 / 3, t) + 0.14 * tone(7 / 3, t)
        return cascade * (0.13 * low + 0.52 * mid + 0.66 * high + 0.95 * detail)
    if name == "WaterVehicle":
        slap = 0.67 + 0.19 * tone(2.25, t) + 0.14 * tone(5.5, t)
        return slap * (1.1 * low + 1.3 * mid + 0.67 * high + 0.30 * detail)
    if name == "River":
        flow = 0.72 + 0.15 * tone(0.375, t) + 0.13 * tone(0.625, t)
        return flow * (0.88 * mid + 1.30 * high + 0.24 * detail)
    gust = 0.50 + 0.26 * tone(0.2, t) + 0.13 * tone(0.3, t) + 0.10 * tone(0.7, t)
    return gust * (2.2 * low + 0.74 * mid + 0.12 * high + 0.013 * air)


def strike(t, delay, decay, attack=0.004):
    elapsed = t - delay
    return 0.0 if elapsed < 0 else min(1.0, elapsed / attack) * exp(-elapsed / decay)


def footstep(name, t, low, mid, high, detail):
    heel = strike(t, 0, 0.058) + 0.5 * strike(t, 0.098, 0.050)
    scrape = strike(t, 0.035, 0.102, 0.023)
    if name.startswith("StepWater"):
        return heel * (0.68 * mid + 1.40 * high + 0.55 * detail) + 0.54 * scrape * (mid + high)
    if name.startswith("StepStone"):
        return heel * (1.2 * low + 0.75 * mid + 0.82 * detail) + scrape * (0.74 * high + 0.6 * detail)
    if name.startswith("StepMud"):
        suction = strike(t, 0.125, 0.067, 0.020) * tone(275 - 215 * t, t)
        return heel * (1.7 * low + 1.08 * mid + 0.33 * detail) + 0.21 * suction
    return heel * (1.8 * low + 0.90 * mid) + scrape * (0.77 * mid + 0.50 * high)


def render_cue(name, t, low, mid, high, air, detail):
    if name.startswith("Step"):
        return footstep(name, t, low, mid, high, detail)
    if name.startswith("Bird"):
        notes = (0.09, 0.32, 0.57, 1.02) if name.endswith("A") else (0.08, 0.19, 0.37, 0.84, 1.27, 1.47)
        voice = 0.0
        for ordinal, delay in enumerate(notes):
            x = t - delay
            if not 0 < x < 0.16:
                continue
            freq = 2050 + (ordinal % 3) * 260 + (380 if name.endswith("B") else 0)
            phase = TAU * (freq * x + 760 * x**2) + 0.71 * tone(53, x)
            voice += strike(t, delay, 0.06, 0.018) * (sin(phase) + 0.18 * sin(2 * phase)) * min(1.0, (0.16 - x) / 0.04)
        return voice
    if name in ("Goal", "Notice"):
        notes = ((0.0, 392.0), (0.13, 493.88), (0.26, 587.33), (0.40, 783.99)) if name == "Goal" else ((0, 548.0), (0.069, 746.0))
        return sum(strike(t, delay, 0.18 if name == "Goal" else 0.042) *
                   (0.74 * tone(freq, t - delay) + 0.12 * tone(freq * 2, t - delay)) for delay, freq in notes)
    if name == "Door":
        return (strike(t, 0, 0.069) + strike(t, 0.14, 0.091)) * (1.4 * low + 0.86 * mid + 0.66 * detail) + strike(t, 0.19, 0.043) * 0.38 * tone(128, t)
    if name == "Ignition":
        crank = strike(t, 0.05, 0.37, 0.10)
        return 0.52 * crank * motor(t, 22, 43, 0.58, 0.42, 0, low, mid, high) + strike(t, 0.0, 0.034) * (mid + 0.18 * tone(470, t))
    if name == "Impact":
        hit = strike(t, 0, 0.085) + 0.32 * strike(t, 0.10, 0.082)
        return hit * (0.73 * tone(64, t) + 0.18 * tone(119, t) + 0.95 * low + 0.64 * mid + 0.22 * high)
    if name == "Tree":
        snap = strike(t, 0, 0.072) + 0.58 * strike(t, 0.26, 0.118) + 0.39 * strike(t, 0.67, 0.18)
        return snap * (1.4 * low + 0.75 * mid + 0.64 * high + 0.62 * detail) + 0.21 * strike(t, 0.61, 0.125) * tone(51, t)
    if name == "Switch":
        return strike(t, 0, 0.016, 0.001) * (0.56 * mid + 0.38 * tone(870, t) + 0.20 * air) + 0.27 * strike(t, 0.047, 0.017) * tone(680, t)
    if name == "Brake":
        return strike(t, 0.024, 0.12, 0.032) * (0.61 * mid + 1.45 * high + 0.26 * air) + 0.32 * strike(t, 0, 0.04) * tone(105, t)
    if name == "TruckBrakeRelease":
        valve = strike(t, 0, 0.018, 0.0015) * (0.56 * tone(192, t) + 0.72 * mid)
        release = strike(t, 0.012, 0.105, 0.011) * (0.46 * mid + 1.63 * high + 0.66 * air)
        return valve + release
    if name == "TruckAirPurge":
        pop = strike(t, 0, 0.029, 0.002) * (1.34 * low + 0.52 * tone(155, t))
        burst = strike(t, 0.009, 0.154, 0.013) * (0.86 * mid + 1.52 * high + 0.75 * air)
        tail = strike(t, 0.105, 0.27, 0.055) * (0.72 * mid + 0.51 * high)
        return pop + burst + tail
    if name == "Jump":
        return strike(t, 0, 0.074) * (1.1 * low + 0.72 * mid + 0.39 * high)
    return strike(t, 0, 0.08) * (0.61 * tone(86, t) + 1.1 * low + 0.50 * mid)


def render(name, duration, destination):
    count = round(duration * RATE)
    rng = Random(name)
    loop = name in LOOPS
    bands = noise_bands(count, rng, (140, 850, 4600), loop)
    grain_name = name[4:-1] if name.startswith("Step") else name
    texture = grains(count, duration, rng, *GRAINS[grain_name], loop=loop) if grain_name in GRAINS else [0.0] * count
    voice = render_loop if loop else render_cue
    signal = []
    for i, ((low, mid, high, air), detail) in enumerate(zip(bands, texture)):
        sample = tanh(voice(name, i / RATE, low, mid, high, air, detail))
        if loop:
            # A sub-millisecond taper removes the sample-value jump at the wrap point.
            sample *= min(1.0, i / 36, (count - 1 - i) / 36)
        else:
            sample *= min(1.0, i / (0.004 * RATE), (count - 1 - i) / (0.037 * RATE))
        signal.append(sample)
    peak = max(abs(value) for value in signal)
    gain = min(3.0, (0.73 if loop else 0.84) / peak)
    if name in ("Wind", "River"):
        gain *= 0.72
    pcm = array("h", (round(value * gain * 32767) for value in signal))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(destination / f"{name}.wav"), "wb") as stream:
        stream.setparams((1, 2, RATE, 0, "NONE", "not compressed"))
        stream.writeframes(pcm.tobytes())


def build(destination):
    destination.mkdir(parents=True, exist_ok=True)
    for name, duration in {**LOOPS, **CUES}.items():
        render(name, duration, destination)


if __name__ == "__main__":
    build(Path(sys.argv[1]))
