# API Provider System

## Principles

1. No provider is hard-coded into the generation pipeline.
2. A provider exposes only capabilities it can prove it supports.
3. Credentials are held by the Secret Service, never by project files or SQLite.
4. Changing routes affects future jobs, not running jobs.
5. No automatic paid fallback. The user chooses.
6. Provider failures preserve completed work and are shown verbatim (redacted).

## Data model

A provider record (SQLite `providers`) contains no secret:

```text
id              stable UUID-ish text identifier
name            user-visible name
type            gemini | openrouter | openai_compatible
base_url        validated HTTPS URL (HTTP only for loopback)
auth_type       bearer | x-api-key | query | none
api_version     e.g. v1beta (Gemini) or v1 (OpenAI-compatible)
text_model      model ID for chat/completions
speech_model    model ID for speech, optional
image_model     model ID for image generation, optional
capabilities    bit mask: text, structured, speech, image, vision
timeout_ms      request timeout
max_retries     transient retry limit
enabled         0/1
created_at, updated_at
```

Capability routes (SQLite `capability_routes`) select provider + model per
capability:

```text
capability      planning | storyboard | metadata | speech | image | vision
provider_id     FK providers(id)
model_id        model used when the job starts
```

The router resolves a route in this order: explicit capability route → enabled
provider whose default capability matches → error. A provider whose capability
bit is absent is never called; the UI and router both reject it.

## Capabilities

| Capability | Meaning |
|---|---|
| `text` | free-form chat/completion |
| `structured` | JSON object output with validation |
| `speech` | text-to-speech returning audio bytes |
| `image` | text-to-image returning image bytes |
| `vision` | image input accepted for analysis |

Gemini defaults: text + structured + speech + vision (vision declared but not
used by the initial pipeline). OpenRouter defaults: text + structured; speech and
image are not advertised unless the user explicitly enables a compatible
endpoint and model. OpenAI-compatible defaults: text + structured; speech and
image are opt-in checkboxes.

## Adapters

### Gemini native

- Text: `POST {base}/v1beta/models/{model}:generateContent?key=…`
  with `systemInstruction`, `contents`, `generationConfig` (`temperature`,
  `responseMimeType: application/json` for structured stages).
- Models: `GET /v1beta/models?key=…`.
- Speech: `POST /v1beta/models/{speech_model}:generateContent` with
  `generationConfig.responseModalities = ["AUDIO"]` and `speechConfig` voice.
  The returned base64 PCM (24 kHz, mono, 16-bit) is wrapped in a WAV container.
- Errors: HTTP status plus Google error body. `RESOURCE_EXHAUSTED`/429 is quota
  or rate limiting; daily quota is not retried. 503 high-demand is retried with
  bounded backoff. `RetryInfo.retryDelay` is surfaced.
- Text and speech model IDs are independent and never fixed in code.

### OpenRouter

- Text: `POST {base}/v1/chat/completions` with `Authorization: Bearer`.
- Structured: `response_format: {"type":"json_object"}` where the selected model
  supports it; the application still validates JSON.
- Models: `GET {base}/v1/models`.
- Capabilities are derived from configuration, not assumed per model. The model
  catalogue is informational; the app does not claim image or TTS support for a
  model without an explicit user-enabled route.
- Usage fields returned by OpenRouter are stored on the job for display.

### OpenAI-compatible

- Base URL examples: `https://api.openai.com/v1`, `http://localhost:11434/v1`.
- Text: `POST {base}/chat/completions`.
- Models: `GET {base}/models` (optional; failure is non-fatal and the UI allows
  manual model entry).
- Speech: `POST {base}/audio/speech` with `model`, `input`, `voice`,
  `response_format` (`wav` or `mp3`), only when the user enables speech.
- Image: `POST {base}/images/generations` with `model`, `prompt`, `size`,
  `response_format: b64_json`, only when the user enables image.
- Authentication: bearer token, `x-api-key`, query parameter or none. Header
  name and scheme come from `auth_type`; no arbitrary headers or scripts.
- Endpoint paths are fixed by the adapter. A user-supplied path is only a
  validated path suffix, never a URL with a different host or scheme.

## HTTP behaviour

- TLS verification is always on. A non-loopback `http://` URL requires an
  explicit confirmation and is shown as insecure.
- Connect timeout 10 s, request timeout configurable (15–300 s).
- Response body capped at 32 MiB for JSON, 64 MiB for media; exceeding the cap
  fails the request.
- gzip/deflate accepted.
- Redirects limited to 5, protocol restricted to http/https.
- Retry only for 408, 425, 429 (except daily quota), 500, 502, 503, 504 and
  transport errors. Maximum attempts = 1 + configured retries. Exponential
  backoff with jitter, capped at 30 s. `Retry-After` (seconds or HTTP date) is
  respected.
- Non-idempotent generation requests are retried only for failures where the
  server reports that no generation occurred (429/503 with explicit error body)
  or when the configured retry setting explicitly allows it. TTS is not retried
  automatically after a transport interruption unless the response was not
  started; the UI offers manual retry.
- Secrets never enter URLs except for Gemini's `key` query parameter, which is
  redacted in logs and error reports.

## Adding a provider without recompiling

The API Manager form accepts name, type, base URL, API version, auth type, model
IDs, capability checkboxes, timeout, retry count and optional endpoint paths.
Saving writes a provider row and a Secret Service entry. The router reads the
new row immediately; no restart or rebuild is required.

## Model catalogue

- Fetch is provider-specific and optional.
- Results are cached in memory for the session with timestamp.
- The user can always type a model ID manually; a failed catalogue fetch never
  blocks saving or testing.
- Capability filters are advisory and come from provider metadata where
  available. Manual override is allowed with a warning.

## Failure and fallback

Default: no automatic fallback.

When a stage fails:

1. The job enters `RETRYING` only for transient transport/5xx failures within
   the configured retry budget.
2. Otherwise it enters `FAILED` with a redacted, provider-specific message and
   a suggested action (check key, wait for quota, choose another provider).
3. Completed stages are persisted; a retry resumes from the failed stage.
4. The user may switch the capability route and retry that stage.
5. Switching a route while a job runs does not mutate the running job; the job
   stores the provider/model snapshot it started with.

Cost controls: providers flagged as potentially paid require a confirmation
before first use in a session. Free-tier quota errors are never retried into a
paid fallback.
