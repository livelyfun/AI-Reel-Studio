# Migration from the Web Application

## Baseline

- Repository: `livelyfun/AI-Reel-Studio`
- Branch: `main`
- Commit: `6a3e1c6` (`feat: initialize AI Reel Studio project`)
- Working tree at audit time: clean, no untracked files.
- Recovery: the baseline commit is the restore point. No reset, clean, branch
  merge, push or automatic commit is performed by this work.

The legacy Vite/React/Express sources remain in place. New native sources are
added alongside them; Meson only compiles the C tree. This keeps the migration
recoverable and preserves every original file.

## Feature mapping

| Web implementation | Native replacement | Phase |
|---|---|---|
| `ReelCreatorForm.tsx` prompt, 10 voices, 12 styles, durations, ratios | `src/ui/reel_creator.c` | 1, 4 |
| Artificial progress delays in `App.tsx` | removed; `AsJobManager` reports real stages | 4 |
| `generatePlanAndStoryboard` Gemini call | generation planner + capability router | 4 |
| Hard-coded Gemini client | provider registry and adapters | 3 |
| `GEMINI_API_KEY` only | Secret Service + API Manager | 3 |
| In-memory narration cache | content-addressed on-disk audio cache | 5 |
| Evenly distributed word timings | estimated timing with explicit label | 5 |
| Openverse/Wikimedia/undocumented Unsplash | Openverse/Wikimedia/official Unsplash API | 5 |
| Remote image URLs | downloaded, verified, attributed local assets | 5 |
| Web Audio procedural music | native WAV synthesis in C | 6 |
| Web Audio narration/ducking | FFmpeg sidechain mix in render | 6 |
| Canvas preview | rendered preview MP4 in GtkVideo/GStreamer | 6 |
| MediaRecorder WebM labelled MP4 | FFmpeg H.264/AAC MP4 + ffprobe validation | 6 |
| SRT/VTT/metadata browser downloads | native file exporters | 7 |
| No persistence | SQLite projects, assets, jobs, exports | 2 |
| No tests | GLib tests for core modules | all |

## Phase plan and gates

### Phase 0 — audit and documentation (this document set)

Gate: source, dependencies and behaviour understood; architecture documented;
baseline commit recorded; no source deletion.

### Phase 1 — foundation

Meson, C17, GTK4 application, sidebar, dashboard, settings, logging, errors.

Gate: clean build; app starts without Node/browser; UI stays responsive.

### Phase 2 — storage

SQLite schema/migrations, project store, settings, directories, backups.

Gate: project survives restart; migration is idempotent; no secrets in DB.

### Phase 3 — providers

HTTP client, registry, router, credentials, Gemini/OpenRouter/OpenAI-compatible
adapters, model catalog, API Manager UI.

Gate: provider can be added, key stored in Secret Service, connection tested,
models listed or manually entered, capability routes persisted, failures
reported without secret leakage.

### Phase 4 — generation

Planner, storyboard validation, metadata, job manager, progress, cancellation.

Gate: real content plan and storyboard generated; invalid output rejected; jobs
survive UI navigation; cancel stops work.

### Phase 5 — assets and narration

Asset search/download/cache/attribution, TTS caching, voice regeneration.

Gate: local scene images with licence metadata; narration cached and reusable;
regeneration works.

### Phase 6 — media

Music synthesis, preview render, ASS captions, transitions, export, ffprobe.

Gate: valid 1080p H.264/AAC MP4 for all ratios; duration/codec checks pass;
cancel leaves no partial export marked successful.

### Phase 7 — editing and export UX

Storyboard edits, scene image replacement, metadata editing, caption style,
export manager, project restore.

Gate: edits persist; exports use edited state; SRT/VTT/metadata/thumbnail
exports work.

### Phase 8 — packaging and QA

RPM spec, Flatpak manifest, desktop file, icon, docs, tests, security audit.

Gate: install and launch from desktop menu; documented build; tests pass;
no plaintext secrets.

## Compatibility notes

- Existing `.env` values are not read by the native app. The user enters keys in
  API Manager. This is intentional: environment variables are not the primary
  credential mechanism.
- Existing generated reels (browser memory only) were never persisted and
  cannot be imported. The native app starts with an empty library and clearly
  shows that state.
- The web UI's visual language is translated, not copied: GTK4 widgets and CSS
  replace Tailwind classes.
- WebM is not presented as MP4 anywhere in the native product. The exporter
  produces MP4/H.264/AAC and validates it.
