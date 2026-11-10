# Media Pipeline

## Overview

```
images ──► scale/crop ──► Ken Burns (zoompan) ──► ASS captions/headline
                                                        │
                                                   scene clip (H.264)
                                                        │
                                             xfade transition concat
                                                        │
narration (TTS cache) ──┐                               │
music (native WAV) ─────┼──► sidechain duck + amix ──► mux ──► MP4
                        │                               │
                        └───────────────────────────────┘
                                                        │
                                                   ffprobe verify
```

## Source assets

- Search providers: Openverse (licence filter `commercial,modification`),
  Wikimedia Commons, and Unsplash only with an official Access Key.
- The undocumented `unsplash.com/napi` endpoint used by the web app is not used.
- Every result records source, original URL, author, licence name, licence URL,
  attribution text and retrieval date.
- Downloads are streamed with a byte cap, content-type check and magic-byte
  verification (JPEG/PNG/WebP). Invalid payloads are rejected.
- Files are cached by SHA-256 of URL + provider. Project references are copies,
  so clearing the cache cannot destroy an existing project.
- The user can replace an image with a local file or another search result.

## Background music

`src/media/audio_synth.c` synthesizes a real stereo 16-bit PCM WAV in C:

- Cinematic: slow minor/major chord pads with long attack.
- Electronic: pulse bass and brighter triads.
- Dramatic: low drone with rising minor intervals.
- Ambient: soft seventh chords with slow envelopes.
- Motivational: rising major progression with rhythmic pulse.

Each preset defines tempo, chord table, oscillator mix and envelope. A gentle
low-pass is applied by summing weighted harmonics rather than a fake placeholder.
Synthesis is deterministic for a given duration/mood/seed and cached by hash.

## Narration cache

Key = SHA-256 of normalized text, provider ID, model ID, voice ID, style and
speech settings. Cache entries store the audio file plus a JSON sidecar.
Changing provider/model/voice cannot reuse another configuration's audio.
`force` bypasses reuse.

## Captions

Five styles are rendered natively through ASS:

| UI name | ASS characteristics |
|---|---|
| TikTok Viral | white bold, black box, yellow karaoke highlight |
| Karaoke Pop | cyan highlight, heavy outline, pop scale |
| Cinematic Glow | warm cream, soft shadow, letter-spaced |
| Minimal Clean | white, no box, thin outline |
| Neon Punch | white on magenta box, strong border |

Words are grouped in four-word chunks. Timing uses provider timings when
available; otherwise estimated timings are written and flagged
`"timing_source":"estimated"` in project JSON. The ASS karaoke tags use those
intervals. No alignment accuracy is claimed beyond recorded evidence.

Headlines use a separate ASS style at the top, uppercase, with an opaque box.

## Scene rendering

For each scene:

```
scale=<canvas*2>:<canvas*2>:force_original_aspect_ratio=increase,
crop=<canvas*2>:<canvas*2>,
zoompan=<motion>:s=<W>x<H>:fps=30:d=<frames>,
ass=<scene.ass>,
eq=contrast=1.03:saturation=1.05,
format=yuv420p
```

Motion is selected from scene camera/transition metadata:

- zoom in / out
- horizontal pan left / right
- vertical pan up / down
- static with a subtle push

Scene duration is `round(scene.duration * fps)` frames, plus a transition tail
used only when the scene is joined by `xfade`; the final composition is trimmed
to the project duration.

## Transitions

| Project value | FFmpeg xfade |
|---|---|
| crossfade | fade |
| zoom-in | zoomin |
| pan-left | slideleft |
| glitch | pixelize |
| fade-black | fadeblack |
| wipe | wipeleft |

Transition duration is 0.45 s (configurable later). The first scene has no
incoming transition; the last has no outgoing gap. Invalid values are normalized
to `crossfade` during storyboard validation.

## Audio mix

Inputs: narration (or `anullsrc` when absent), music WAV.

```
[voice] aformat=48000/stereo, apad
[music] aformat=48000/stereo, volume=music_gain
[music][voice] sidechaincompress=threshold=0.03:ratio=6:attack=20:release=350
[ducked][voice] amix=inputs=2:duration=longest:normalize=0
atrim=0:duration, afade out
```

Narration is the sidechain, so music ducks while speech is active and recovers
afterwards. The mix is AAC 192 kb/s.

## Output

| Ratio | Size |
|---|---|
| 9:16 | 1080×1920 |
| 16:9 | 1920×1080 |
| 1:1 | 1080×1080 |

H.264 (`libx264`, `yuv420p`, CRF 20, preset medium, faststart), AAC 192 kb/s,
30 fps, MP4 container. Audio is always present (music when speech is absent).

## Preview

A preview MP4 is rendered at 540×960 / 960×540 / 540×540 with `ultrafast`, CRF
28, and the same filters/audio mix. It is played with `GtkVideo` backed by
GStreamer. Playback, pause, seek and volume are native controls. This avoids a
second, divergent preview renderer.

## Export and verification

1. Verify output directory exists, is writable, has free space (estimated from
   duration × bitrate + 20%).
2. Refuse to overwrite unless the user confirms.
3. Render to a job-private temporary path on the same filesystem.
4. Run `ffprobe -v error -print_format json -show_format -show_streams`.
5. Require: `format_name` contains `mp4`; video codec `h264`; audio codec `aac`;
   configured resolution; frame rate 30 ±0.1; duration within 0.25 s; audio
   stream present; nonzero size.
6. Move to the final destination only after validation succeeds.
7. On failure or confirmed cancellation, delete the job's temporary files.
   User source assets are never deleted.

## Process safety

- All external commands use `GSubprocess` with an argument vector. No shell.
- The FFmpeg executable path comes from settings and is checked as executable.
- stderr is drained with a 256 KiB cap; the tail is retained for diagnostics.
- Progress is parsed from `-progress pipe:1` (`out_time_ms`), not timers.
- A `GCancellable` force-terminates the process on cancel and the job is marked
  cancelled, never completed.
