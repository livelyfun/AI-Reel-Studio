# AI Reel Studio — Native Architecture

Status: Phase 0 baseline, revised as implementation proceeds.
Target: Fedora Linux first, other mainstream Linux distributions afterwards.
Language: C17. GUI: GTK4. Media: FFmpeg/ffprobe + GStreamer. Storage: SQLite.
Secrets: libsecret (Secret Service).

## 1. Audit baseline

The repository at commit `6a3e1c6` is a Vite 6 + React 19 + TypeScript
single-page application with an Express server. It is **not** a Next.js
application. The audit found:

- `server.ts` exposes `/api/reel/plan`, `/api/reel/generate-scene-visual`,
  `/api/reel/generate-voiceover`, `/api/reel/regenerate-scene` and
  `/api/reel/full-pipeline`.
- Gemini access is hard-wired through `@google/genai` in
  `server/geminiService.ts`. Text model defaults to `gemini-2.5-flash`; TTS is a
  single hard-coded preview model.
- The API key comes only from `GEMINI_API_KEY`; there is no provider UI and no
  secure credential storage.
- The "visual generation" stage performs no generation. It queries Openverse,
  Wikimedia Commons and an **undocumented** Unsplash web endpoint
  (`unsplash.com/napi/...`), returning a remote URL rather than a stored asset.
- Narration is one TTS request for the full script, cached only in a process
  memory `Map`. Cache keys do not include provider/model/voice.
- Word timings are distributed evenly over each scene. They are estimates but
  are treated as synchronization data by the player and exporter.
- The pipeline contains artificial `setTimeout` delays (`App.tsx` lines
  67, 85, 196, 200, 204, 232) that advance progress without work.
- Preview and export are Canvas + MediaRecorder. The exporter requests WebM
  (VP9/VP8) and the UI labels the result "MP4". No H.264/AAC file is produced.
- Background music is procedural Web Audio synthesis. There is no persistent
  project, no database, no job manager and no native process.
- There is no test suite.

All of the above are replaced. The legacy web sources are retained in the
working tree for reference until the native milestones are verified.

## 2. System boundaries

```
┌──────────────────────────────────────────────────────────────┐
│ GTK4 user interface                                          │
│ dashboard · creator · projects · API manager · settings      │
│ workspace (storyboard · preview · metadata · export)         │
└───────────────┬──────────────────────────────────────────────┘
                │ signals / immutable snapshots
┌───────────────▼──────────────────────────────────────────────┐
│ Application core                                             │
│ config · logging · error reporting · job manager · events    │
└───┬───────────────┬────────────────┬───────────────┬─────────┘
    │               │                │               │
┌───▼────────┐ ┌────▼─────────┐ ┌────▼─────────┐ ┌───▼────────┐
│ Generation │ │ Providers    │ │ Media        │ │ Storage    │
│ planner    │ │ registry     │ │ assets       │ │ SQLite     │
│ storyboard │ │ router       │ │ audio synth  │ │ projects   │
│ narration  │ │ Gemini       │ │ renderer     │ │ providers  │
│ captions   │ │ OpenRouter   │ │ ffprobe      │ │ jobs       │
│ metadata   │ │ OpenAI-compat│ │ GStreamer    │ │ settings   │
└───┬────────┘ └────┬─────────┘ └────┬─────────┘ └───┬────────┘
    │               │                │               │
    └───────────────┴────────┬───────┴───────────────┘
                             │
              ┌──────────────▼──────────────┐
              │ Security                    │
              │ Secret Service credentials  │
              │ redaction · URL validation  │
              └─────────────────────────────┘
```

Dependency direction is one-way: UI → core/generation → providers/network →
storage/security. Provider adapters never depend on UI. Storage never depends on
GTK. The generation engine never contains provider-specific HTTP code.

## 3. Modules

### 3.1 Application core (`src/app`, `include/as_app.h`)

- `AsApplication` owns the GTK application, configuration, database handle,
  provider registry, job manager and main window.
- Configuration is stored in `$XDG_CONFIG_HOME/ai-reel-studio/settings.ini`
  (`GKeyFile`). Values are validated with explicit defaults.
- Logging uses GLib structured log levels and a rotating file in
  `$XDG_DATA_HOME/ai-reel-studio/logs/`. All messages pass through the
  redaction filter.
- Errors are `AsError` records (`domain`, `code`, `message`, `detail`) carried
  across module boundaries and converted to user-facing messages only in UI.

### 3.2 Storage (`src/storage`)

SQLite with WAL, foreign keys and versioned migrations. Tables: `projects`,
`providers`, `capability_routes`, `jobs`, `assets`, `exports`, `settings`.
Structured project payloads are stored as validated JSON documents; binary media
lives in the per-project directory. Secrets are never written to SQLite.

### 3.3 Security (`src/security`)

`AsCredentialManager` stores API keys in the Secret Service under a dedicated
schema keyed by provider ID. SQLite stores only the provider row; there is no
secret column. If no Secret Service is available the manager refuses persistent
storage and offers session-only keys held in memory. Environment overrides exist
for automated tests and development, never as the primary UI path.

### 3.4 Network (`src/network`)

One libcurl-based client provides GET/POST, headers, bounded response bodies,
connect/request timeouts, TLS verification, cancellation, HTTP status mapping
and bounded retry with `Retry-After` support. Provider adapters use this client
exclusively. Authorization headers and keys are redacted before logging.

### 3.5 Providers (`src/providers`)

- `AsProviderRegistry` owns configured providers loaded from SQLite.
- Capability flags: text, structured output, speech, image, vision.
- `AsCapabilityRouter` resolves the enabled provider for a capability:
  explicit route → default provider → capability default → error.
- Adapters: Gemini, OpenRouter, OpenAI-compatible. Each adapter exposes only
  verified capabilities and implements request/response parsing itself.
- `AsModelCatalog` caches provider model lists and supports manual model IDs.

The generation engine asks the router for a provider; it never names Gemini,
OpenRouter or any endpoint.

### 3.6 Generation (`src/generation`)

Pipeline stages run in worker threads under `AsJobManager`:

1. content plan + Visual Bible + storyboard + metadata (structured text)
2. visual assets per scene (free asset engine or an explicitly configured
   image provider)
3. narration (TTS provider, cached by content/provider/model/voice hash)
4. captions and estimated word timings (labelled as estimated)
5. background music synthesis
6. project persistence
7. optional render/export

Every stage validates its inputs and outputs. LLM output is normalized: scene
IDs, ordering, durations, transitions and required strings are checked. Invalid
output fails the stage with a precise message instead of propagating bad data.

Progress is reported only after real work completes. No timers advance it.

### 3.7 Media (`src/media`)

- Free asset search: Openverse and Wikimedia Commons. Unsplash is used only when
  the user supplies an official Unsplash Access Key; the undocumented `napi`
  endpoint is removed.
- Asset cache stores bytes locally with source, author, licence and attribution.
- Native music synthesis writes real 16-bit PCM WAV files in C for the five mood
  presets (cinematic, electronic, dramatic, ambient, motivational).
- FFmpeg rendering: per-scene Ken Burns clips with ASS captions/headlines,
  `xfade` transitions, H.264/AAC MP4, `yuv420p`, 30 fps.
- ffprobe validation verifies container, codecs, resolution, frame rate,
  duration, audio stream and integrity. Export success is reported only after
  validation.
- Preview uses GTK4's media widget backed by GStreamer (`GtkVideo` +
  `GtkMediaFile`). A low-resolution preview MP4 is rendered on demand, so the
  preview shows the same composition as the final export.

### 3.8 UI (`src/ui`)

Sidebar navigation with Dashboard, Create Reel, Projects, API Manager and
Settings. Project workspace contains storyboard editing, preview, metadata,
Visual Bible, captions and export. The UI is dark, keyboard navigable and
responsive; all expensive work is async. Long operations show a cancel action
and an honest stage label.

## 4. Ownership and concurrency rules

- GObject instances use `g_object_ref`/`g_object_unref`; floating references are
  consumed exactly once.
- Structs returned by value use explicit `*_free()` functions.
- Worker threads receive immutable snapshots and never touch GTK widgets.
- UI updates happen on the main context through `g_idle_add` callbacks.
- SQLite connections are not shared across threads; the worker opens its own.
- Cancellation is an atomic flag checked between stages and passed to libcurl
  and GSubprocess as a `GCancellable`.
- No unbounded response or stderr buffers; both are capped and errors are
  surfaced when the cap is reached.

## 5. Deliberate limitations

- Word timings are estimates unless a provider returns timing data. The model
  and ASS exporter mark them as estimated; no fake alignment is claimed.
- OpenRouter TTS is not enabled by default because it is not a uniform,
  verified capability. A user can route speech to an OpenAI-compatible provider.
- Automatic paid fallback is disabled. A provider failure is shown and the user
  chooses another provider.
- The legacy web application cannot be executed as part of the native product;
  it remains reference material only.
