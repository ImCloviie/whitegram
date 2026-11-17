# Karaoke: timing and effects

## Architecture review

The path is `MusicIsland` → `lyrics_engine` (providers, parsers, cache) →
`track_analysis` (local PCM/FFT, energy, BPM, spectral onsets) → `lyrics_align`
→ the shared lyrics data in the island and `LyricsWindow`. Translation only
adds display text; it must not determine timing. `net_fetch` owns HTTP/proxy
handling. Drawing, particles, word effects and existing widgets remain in
`LyricsWindow`; their geometry and controls are unchanged.

The audit found these concrete timing problems:

- A track change could fetch lyrics using the previous track's duration.
- The provider cache grouped durations into five-second buckets, and selection
  accepted versions ten seconds apart.
- The island and full window extrapolated separate clocks and applied different
  line/word lead times. Only the full window applied the saved manual offset.
- Correlation with spectral attacks could move published timestamps by three
  seconds. Drums are not evidence of where a particular word is sung.
- Estimated words used a fixed syllable rate and could extend beyond the next
  line. LRC offset metadata was ignored.

The player position now drives both windows. Fresh mixer timestamps interpolate
between audio updates (up to 150 ms); pauses, buffering, stale samples and seeks
fall back to the reported media position rather than a free-running UI timer.
Supplied timestamps remain
authoritative; line-only lyrics interpolate within their cue interval. That
interpolation is still an estimate of individual word positions, not word sync.
Spectral automatic alignment is retained only for plain text and remains marked
AUTO. It cannot reliably distinguish vocals from instruments or recover missing
repeated verses. Exact matching of a recording still matters.

## Sources and formats

- [LRCLIB](https://lrclib.net/docs): public line-synced lyrics; duration matching
  now uses a three-second search tolerance for small encoder-padding differences
  (the exact API itself uses two seconds). Supplied timings are never stretched.
  New Lyricsfile word-level YAML is documented
  upstream but is not parsed by this client yet.
- Existing KuGou KRC and NetEase YRC supply word timing where available. These
  service endpoints can change; a result is not guaranteed for every recording.
- Genius and lyrics.ovh remain plain-text fallbacks, not timing providers.
- [Musixmatch](https://www.postman.com/musixmatch-dev/musixmatch-apis/request/pfer6zd/matcher-subtitle-get)
  provides a matching subtitle API. It needs authorized API access; no private
  application tokens or unsupported authentication bypass are shipped.
- Beside a downloaded audio file, `song.lrc`, `song.srt` or `song.vtt` takes
  priority. Enhanced LRC preserves individual word starts, ends and silent gaps.
  SRT/WebVTT supply line boundaries; styling tags are removed, not displayed.

## Optional local forced alignment

For plain/line-only lyrics, the worker can align the supplied transcript to the
actual audio using [Stable-ts](https://github.com/jianfch/stable-ts). This is a
separate local process, with a three-minute timeout and one concurrent model.
No audio is sent to a service. The upstream project is currently archived;
the integration is optional and failure leaves the existing lyrics available.

Install `stable-ts` in a Python environment compatible with PyTorch and provide
FFmpeg on PATH. Supply an existing multilingual Whisper checkpoint (for example
`small.pt`); the application does not download models automatically.
Before launching Whitegram, set:

```powershell
$env:WHITEGRAM_KARAOKE_PYTHON = 'C:\karaoke-env\Scripts\python.exe'
$env:WHITEGRAM_KARAOKE_MODEL = 'C:\models\small.pt'
# Optional: force the known language instead of detecting it from the audio.
$env:WHITEGRAM_KARAOKE_LANGUAGE = 'ru'
# Optional: cuda when supported; the default is cpu.
$env:WHITEGRAM_KARAOKE_DEVICE = 'cpu'
```

The build copies `karaoke_align.py` beside the executable. Model output must
preserve the supplied text and have monotonic, nonzero word intervals. Incorrect
or missing lyrics and dense singing can still produce poor alignment: this is
not a guarantee of accuracy on every track. Model inference requires the above
environment and is not covered by the lightweight regression tests.

## Effects

Additional Russian/English triggers cover light, fire, storms, sadness, shadows,
dancing, flight, space and breaking. Immediate negation suppresses word effects.
Existing widgets are retained. Audio energy is averaged over two seconds and
smoothed before controlling a warm background layer. Sad/rain lyrics introduce
rain; explicit dark/gothic imagery introduces dim pointed arches. This is a
transparent heuristic, not a claimed musical genre classifier.

## Checks

`tools\karaoke\test_windows.cmd` compiles tests against the production C++ parsers
and alignment code with the local Qt static SDK. Override `QT_ROOT`, `ZLIB_ROOT`
and `VS_ROOT` if needed. Tests cover LRC offsets, silent gaps, cue boundaries,
dense lyrics, SRT/WebVTT, malformed input, preservation of published timing,
clock interpolation, playback speed, pauses/buffering, stale samples and seeks.

`python tools/karaoke/test_align.py` tests alignment output validation without
loading or downloading a model.
