"""WAV comparison for batch F's sound checks (f_sound.py, #3 and #4):
standard library only. Not a check itself.

DOSBox-X records 48 kHz stereo (F12+W), eclplay --wav renders 44.1 kHz
mono; both are read as mono floats with their own rate and compared in
seconds: an envelope (RMS every 5 ms) for aligning and for the tempo, the
strongest spectral peaks of short frames for the pitches, and the sounds
(stretches above a threshold between silences) for their onsets and lengths.
"""
import cmath
import math
import struct
import wave


def read(path):
    """(rate, samples as floats -1..1, mono)."""
    with wave.open(str(path)) as w:
        rate, chans, width, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
        raw = w.readframes(n)
    if width != 2:
        raise ValueError(f"{path}: {width * 8}-bit samples")
    vals = struct.unpack(f"<{len(raw) // 2}h", raw)
    if chans == 2:
        vals = [(a + b) / 2 for a, b in zip(vals[0::2], vals[1::2])]
    return rate, [v / 32768 for v in vals]


def envelope(rate, samples, hop=0.005):
    """RMS per HOP seconds, each step's bounds rounded from k * rate * HOP
    so that step k starts at k * HOP seconds whatever the rate (a whole
    number of samples a step, int(rate * HOP), stretched 44.1 kHz's time
    by 220.5 / 220 against 48 kHz's: a 0.227 % drift)."""
    out = []
    k = 0
    while True:
        a, b = round(k * rate * hop), round((k + 1) * rate * hop)
        if b > len(samples) or b <= a:
            break
        chunk = samples[a:b]
        n = len(chunk)
        mean = sum(chunk) / n
        out.append(math.sqrt(sum((v - mean) ** 2 for v in chunk) / n))
        k += 1
    return out


def best_lag(a, b, lo, hi, start=0, length=None):
    """The lag L (in envelope steps) in [lo, hi] that best matches a[i] with
    b[i + L] (normalized correlation over a[start:start+length])."""
    length = length or len(a) - start
    seg = a[start:start + length]
    na = math.sqrt(sum(v * v for v in seg)) or 1.0
    best, best_r = None, -2.0
    for lag in range(lo, hi + 1):
        j0 = start + lag
        if j0 < 0 or j0 + length > len(b):
            continue
        other = b[j0:j0 + length]
        nb = math.sqrt(sum(v * v for v in other)) or 1.0
        r = sum(x * y for x, y in zip(seg, other)) / (na * nb)
        if r > best_r:
            best, best_r = lag, r
    return best, best_r


def onset(env, threshold=None):
    """The first envelope step above THRESHOLD (default a tenth of the peak)."""
    peak = max(env) if env else 0
    threshold = threshold if threshold is not None else peak / 10
    for i, v in enumerate(env):
        if v > threshold and v > 0:
            return i
    return None


def sounds(env, hop=0.005, threshold=0.002, gap=0.02):
    """Stretches of sound: (start s, length s, peak RMS) between silences
    of at least GAP seconds below THRESHOLD."""
    out, start, quiet, peak = [], None, 0, 0.0
    need = max(1, int(gap / hop))
    for i, v in enumerate(env):
        if v > threshold:
            if start is None:
                start, peak = i, 0.0
            quiet = 0
            peak = max(peak, v)
        elif start is not None:
            quiet += 1
            if quiet >= need:
                out.append((start * hop, (i - quiet + 1 - start) * hop, peak))
                start = None
    if start is not None:
        out.append((start * hop, (len(env) - quiet - start) * hop, peak))
    return out


def _fft(x):
    n = len(x)
    if n == 1:
        return list(x)
    even, odd = _fft(x[0::2]), _fft(x[1::2])
    out = [0j] * n
    for k in range(n // 2):
        t = cmath.exp(-2j * math.pi * k / n) * odd[k]
        out[k], out[k + n // 2] = even[k] + t, even[k] - t
    return out


def decimate(rate, samples, factor):
    """Average FACTOR samples into one (a crude low-pass)."""
    out = [sum(samples[i:i + factor]) / factor for i in range(0, len(samples) - factor + 1, factor)]
    return rate / factor, out


def peaks(rate, samples, t, size=2048, count=3, floor=1e-4):
    """The COUNT strongest spectral peaks (Hz, magnitude) of SIZE samples
    from T seconds, Hann-windowed, the frequency refined by a parabola."""
    i0 = int(t * rate)
    chunk = samples[i0:i0 + size]
    if len(chunk) < size:
        return []
    win = [chunk[k] * (0.5 - 0.5 * math.cos(2 * math.pi * k / size)) for k in range(size)]
    mags = [abs(c) for c in _fft(win)[:size // 2]]
    found = []
    for k in range(2, size // 2 - 1):
        if mags[k] > floor * size and mags[k] >= mags[k - 1] and mags[k] > mags[k + 1]:
            a, b, c = (math.log(mags[k - 1] + 1e-12), math.log(mags[k] + 1e-12),
                       math.log(mags[k + 1] + 1e-12))
            d = (a - c) / (2 * (a - 2 * b + c)) if a - 2 * b + c else 0.0
            found.append(((k + d) * rate / size, mags[k]))
    found.sort(key=lambda p: -p[1])
    return found[:count]


def same_pitch(f1, f2, tolerance=0.03):
    return abs(f1 - f2) <= tolerance * max(f1, f2)


def compare_pitches(ra, a, rb, b, offset, times, size_s=0.045):
    """At each time T (seconds in A), the strongest peak of A at T and B at
    T + OFFSET: (agreeing frames, frames with sound in both, a few that
    differ)."""
    agree = both = 0
    differ = []
    na, nb = 1 << round(math.log2(size_s * ra)), 1 << round(math.log2(size_s * rb))
    for t in times:
        pa, pb = peaks(ra, a, t, na, 3), peaks(rb, b, t + offset, nb, 3)
        if not pa or not pb:
            continue
        both += 1
        # The strongest peak of one among the other's three strongest.
        if any(same_pitch(pa[0][0], f) for f, _ in pb) or any(same_pitch(pb[0][0], f) for f, _ in pa):
            agree += 1
        elif len(differ) < 8:
            differ.append((round(t, 2), round(pa[0][0], 1), round(pb[0][0], 1)))
    return agree, both, differ


def tempo(env_a, env_b, lag, hop=0.005, window=4.0, search=0.3):
    """The best lag (seconds) of B against A in successive WINDOW-second
    stretches of A, searched SEARCH seconds around LAG (steps): a constant
    lag means the same tempo; a drift, a different one."""
    w, s = int(window / hop), int(search / hop)
    out = []
    for start in range(0, len(env_a) - w, w):
        if max(env_a[start:start + w]) < 1e-3:
            continue
        found, r = best_lag(env_a, env_b, lag - s, lag + s, start, w)
        if found is not None:
            out.append((round(start * hop, 1), round(found * hop, 3), round(r, 3)))
    return out


def match(a, b, same):
    """The longest sequence of pairs (i, j), increasing in both, with
    same(a[i], b[j]) (a longest common subsequence)."""
    n, m = len(a), len(b)
    best = [[0] * (m + 1) for _ in range(n + 1)]
    for i in range(n - 1, -1, -1):
        for j in range(m - 1, -1, -1):
            best[i][j] = (best[i + 1][j + 1] + 1 if same(a[i], b[j])
                          else max(best[i + 1][j], best[i][j + 1]))
    pairs, i, j = [], 0, 0
    while i < n and j < m:
        if same(a[i], b[j]) and best[i][j] == best[i + 1][j + 1] + 1:
            pairs.append((i, j))
            i, j = i + 1, j + 1
        elif best[i + 1][j] >= best[i][j + 1]:
            i += 1
        else:
            j += 1
    return pairs
