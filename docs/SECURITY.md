# Security Model

## Assets protected

- AI provider API keys.
- Generated project content (prompts, storyboards, narration, metadata).
- Local file paths and exported media.
- Integrity of external processes and downloads.

## Credential storage

- Linux Secret Service via libsecret.
- Schema: `org.aireelstudio.Provider`, attribute `provider_id`.
- Label: `AI Reel Studio — <provider name> (<provider id>)`.
- SQLite has no secret column; project JSON never includes keys.
- The UI never re-displays a saved key. It shows `••••••••` and a "Replace"
  action. Closing the dialog clears entry buffers.
- Deleting a provider deletes its secret item. If deletion fails, the provider is
  still removed from the app and the orphan is reported.
- If the Secret Service is unavailable:
  - persistent save is disabled;
  - the dialog explains why;
  - the user may choose "Use for this session only", held in memory and erased
    on exit.
- Environment override for development/tests:
  `AI_REEL_STUDIO_ENV_KEYS=1` plus `AS_KEY_<PROVIDER_ID_UPPER>`. This is
  documented as a development feature, not offered as the normal UI flow.

## Secret lifecycle

1. Entered in a `GtkPasswordEntry` (masked, no clipboard by default).
2. Sent directly to libsecret; the `GBytes`/buffer is zeroed with
   `memset_s`-style explicit clearing before release.
3. Retrieved only immediately before an HTTP request; held in a `GBytes` for the
   request lifetime and cleared afterwards.
4. Redacted from all logs, error details and exported diagnostics.
5. Never included in crash-style dumps or job records.

## Logging and diagnostics

- `as_log_redact()` replaces values matching:
  - `Authorization: ...`
  - `x-api-key: ...`
  - `key=...` query parameters
  - known configured secret values (exact match)
- Provider error bodies are logged only after redaction.
- The logger writes to `$XDG_DATA_HOME/ai-reel-studio/logs/studio.log` with mode
  0600 and rotates at 5 MiB (one `.1` backup).
- Debug logging of full HTTP bodies is disabled by default and requires an
  explicit environment flag in a developer build.

## Networking

- TLS certificate and hostname verification are always enabled.
- Minimum TLS 1.2.
- Plain HTTP is allowed only for loopback (`localhost`, `127.0.0.0/8`, `[::1]`)
  or when the user explicitly confirms an insecure non-loopback URL. The
  provider list shows a warning badge in that case.
- URLs must be absolute, http/https, and have a host. Credentials embedded in
  URLs (`user:pass@host`) are rejected.
- Redirects: maximum 5, only http/https, no downgrade from HTTPS to HTTP.
- Response size limits prevent memory exhaustion.
- No proxy environment trust by default; libcurl uses its standard environment
  handling only when `AS_USE_ENV_PROXY=1`.

## Subprocess safety

- FFmpeg/ffprobe are executed with `GSubprocess` argv arrays. No `system()`,
  `popen()`, `sh -c` or string concatenation into a command line.
- Paths are passed as separate arguments.
- Filter expressions are built from validated numeric fields and fixed mapping
  tables. User text is never interpolated into a filter expression; captions are
  written to an ASS file with escaping.
- FFmpeg path from settings must resolve to an executable regular file.
- Temporary files live under the job directory; cleanup only touches paths
  recorded by that job.

## File system

- App directories: `$XDG_DATA_HOME/ai-reel-studio` and
  `$XDG_CONFIG_HOME/ai-reel-studio`, mode 0700.
- Database mode 0600. Logs mode 0600.
- Project IDs are generated internally; user-provided names are sanitized for
  display only and never concatenated into paths unsanitized.
- Export filenames are sanitized (no `/`, `\`, NUL, leading dot, control chars)
  and resolved inside the chosen directory.
- Symlink checks are applied before writing exports; a symlink at the target is
  refused unless overwrite is explicitly confirmed and the resolved path stays
  in the selected directory.
- Asset downloads are written with `O_CREAT|O_EXCL` to temporary names and
  renamed atomically.
- Downloads enforce HTTPS for remote sources and cap redirects/size.

## Threat notes

- A malicious provider response cannot execute code: it is parsed as JSON or
  written as media bytes and validated.
- A malicious image cannot escape the cache path: names are hashes plus a known
  extension, and content type is verified.
- Prompt injection in generated content can affect text output, but generated
  data is validated and never used as a command.
- A malicious local FFmpeg binary is a user-controlled setting; the UI warns and
  shows the resolved path.

## Verification checklist

- `sqlite3 studio.db .dump | grep -i -E 'api[_-]?key|sk-'` returns nothing.
- Logs contain no configured secret values.
- Exported project JSON contains provider names and model IDs, never keys.
- HTTP error messages include status and provider message, not the credential.
- Failed TLS validation prevents the request.
- Cancelling a job removes its partial export.
- Directory traversal attempts in project names and export names are sanitized.
- FFmpeg invocation rejects shell metacharacters by construction.
