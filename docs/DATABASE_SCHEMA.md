# Database Schema

Database: `$XDG_DATA_HOME/ai-reel-studio/database/studio.db`
Mode: WAL, foreign keys ON, `synchronous=NORMAL`, busy timeout 5000 ms.
Migrations are numbered in `PRAGMA user_version`; each migration runs in a
transaction and is idempotent. A backup copy is made before a version upgrade.

## Tables

```sql
CREATE TABLE projects (
    id              TEXT PRIMARY KEY,
    name            TEXT NOT NULL,
    status          TEXT NOT NULL DEFAULT 'draft',
    prompt          TEXT NOT NULL DEFAULT '',
    voice_style     TEXT NOT NULL DEFAULT 'Cinematic',
    visual_style    TEXT NOT NULL DEFAULT 'Cinematic',
    duration        INTEGER NOT NULL DEFAULT 30,
    aspect_ratio    TEXT NOT NULL DEFAULT '9:16',
    reel_json       TEXT,           -- validated GeneratedReel document
    thumbnail_path  TEXT,
    export_path     TEXT,
    total_duration  REAL NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL
);
CREATE INDEX idx_projects_updated ON projects(updated_at DESC);

CREATE TABLE providers (
    id              TEXT PRIMARY KEY,
    name            TEXT NOT NULL,
    type            TEXT NOT NULL,       -- gemini|openrouter|openai_compatible
    base_url        TEXT NOT NULL,
    auth_type       TEXT NOT NULL DEFAULT 'bearer',
    api_version     TEXT NOT NULL DEFAULT '',
    text_model      TEXT NOT NULL DEFAULT '',
    speech_model    TEXT NOT NULL DEFAULT '',
    image_model     TEXT NOT NULL DEFAULT '',
    capabilities    INTEGER NOT NULL DEFAULT 3,
    timeout_ms      INTEGER NOT NULL DEFAULT 60000,
    max_retries     INTEGER NOT NULL DEFAULT 2,
    enabled         INTEGER NOT NULL DEFAULT 1,
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL
);

CREATE TABLE capability_routes (
    capability      TEXT PRIMARY KEY,   -- planning|storyboard|metadata|speech|image|vision
    provider_id     TEXT NOT NULL REFERENCES providers(id) ON DELETE CASCADE,
    model_id        TEXT NOT NULL DEFAULT ''
);

CREATE TABLE jobs (
    id              TEXT PRIMARY KEY,
    project_id      TEXT REFERENCES projects(id) ON DELETE CASCADE,
    type            TEXT NOT NULL,
    state           TEXT NOT NULL,
    stage           TEXT NOT NULL DEFAULT '',
    progress        REAL NOT NULL DEFAULT 0,
    error           TEXT,
    provider_id     TEXT,
    model_id        TEXT,
    usage_json      TEXT,
    started_at      TEXT,
    finished_at     TEXT
);
CREATE INDEX idx_jobs_project ON jobs(project_id);
CREATE INDEX idx_jobs_state ON jobs(state);

CREATE TABLE assets (
    id              TEXT PRIMARY KEY,
    project_id      TEXT REFERENCES projects(id) ON DELETE CASCADE,
    scene_id        INTEGER,
    kind            TEXT NOT NULL,       -- image|audio|thumbnail
    local_path      TEXT NOT NULL,
    source          TEXT,
    source_url      TEXT,
    author          TEXT,
    license_name    TEXT,
    license_url     TEXT,
    attribution     TEXT,
    retrieved_at    TEXT,
    sha256          TEXT
);
CREATE INDEX idx_assets_project ON assets(project_id);

CREATE TABLE exports (
    id              TEXT PRIMARY KEY,
    project_id      TEXT REFERENCES projects(id) ON DELETE CASCADE,
    path            TEXT NOT NULL,
    format          TEXT NOT NULL DEFAULT 'mp4',
    width           INTEGER,
    height          INTEGER,
    duration        REAL,
    video_codec     TEXT,
    audio_codec     TEXT,
    file_size       INTEGER,
    verified        INTEGER NOT NULL DEFAULT 0,
    created_at      TEXT NOT NULL
);
CREATE INDEX idx_exports_project ON exports(project_id);

CREATE TABLE settings (
    key             TEXT PRIMARY KEY,
    value           TEXT NOT NULL
);

CREATE TABLE schema_migrations (
    version         INTEGER PRIMARY KEY,
    applied_at      TEXT NOT NULL
);
```

## Migration policy

- v0 → v1 creates the schema above.
- Future versions add tables/columns only; destructive rewrites require an
  explicit backup and a documented migration.
- Startup checks `PRAGMA integrity_check`; a failed check offers a backup path
  instead of deleting the database.
- Before applying a migration, the file is copied to
  `database/backups/studio-<version>-<timestamp>.db` with mode 0600.

## JSON documents

`projects.reel_json` stores the validated `GeneratedReel`-equivalent document
(see `as_types.h`), including request, plan, Visual Bible, storyboard, assets,
narration cache keys, metadata, caption style and timing source. It never
contains credentials, absolute home paths outside the project directory, or
environment values.

## Backup and recovery

- `as_database_backup()` uses SQLite's online backup API.
- Interrupted jobs are marked `INTERRUPTED` at startup.
- Deleting a project deletes its rows and its project directory only; shared
  cache entries are left to the cache manager.
