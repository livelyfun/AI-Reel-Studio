/* as_core.h — errors, logging, configuration and XDG paths. */
#pragma once

#include <glib.h>

typedef enum {
    AS_ERROR_UNKNOWN = 0,
    AS_ERROR_INVALID_ARGUMENT,
    AS_ERROR_NOT_FOUND,
    AS_ERROR_IO,
    AS_ERROR_HTTP_STATUS,
    AS_ERROR_NETWORK,
    AS_ERROR_TLS,
    AS_ERROR_TIMEOUT,
    AS_ERROR_TOO_LARGE,
    AS_ERROR_INVALID_JSON,
    AS_ERROR_AUTH,
    AS_ERROR_RATE_LIMIT,
    AS_ERROR_QUOTA_DAILY,
    AS_ERROR_UNAVAILABLE,
    AS_ERROR_INVALID_RESPONSE,
    AS_ERROR_UNSUPPORTED,
    AS_ERROR_CANCELLED,
    AS_ERROR_CONFIG,
    AS_ERROR_SECRET,
    AS_ERROR_DB,
    AS_ERROR_MEDIA,
    AS_ERROR_VALIDATION,
    AS_ERROR_BUSY
} AsErrorCode;

GQuark as_error_quark(void);
#define AS_ERROR (as_error_quark())

GError *as_error_new(AsErrorCode code, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);
AsErrorCode as_error_code(const GError *error);
gboolean as_error_is_retryable(const GError *error);

/* Redaction is applied to every log line and to user-facing provider errors. */
gchar *as_redact(const gchar *text);
void as_redact_register_secret(const gchar *secret);
void as_redact_clear_secrets(void);

typedef enum {
    AS_LOG_DEBUG = 0,
    AS_LOG_INFO,
    AS_LOG_WARNING,
    AS_LOG_ERROR
} AsLogLevel;

gboolean as_log_init(const gchar *log_dir, GError **error);
void as_log(AsLogLevel level, const gchar *domain, const gchar *fmt, ...)
    G_GNUC_PRINTF(3, 4);
#define AS_DEBUG(dom, ...) as_log(AS_LOG_DEBUG, dom, __VA_ARGS__)
#define AS_INFO(dom, ...)  as_log(AS_LOG_INFO, dom, __VA_ARGS__)
#define AS_WARN(dom, ...)  as_log(AS_LOG_WARNING, dom, __VA_ARGS__)
#define AS_ERROR_LOG(dom, ...) as_log(AS_LOG_ERROR, dom, __VA_ARGS__)

/* ---- paths ---- */
typedef struct {
    gchar *data_dir;
    gchar *config_dir;
    gchar *cache_dir;
    gchar *projects_dir;
    gchar *logs_dir;
    gchar *db_path;
    gchar *db_backup_dir;
    gchar *settings_path;
    gchar *tmp_dir;
} AsPaths;

AsPaths *as_paths_new(GError **error);
void as_paths_free(AsPaths *paths);
gchar *as_paths_project_dir(const AsPaths *paths, const gchar *project_id);
gboolean as_ensure_dir(const gchar *path, GError **error);

/* ---- configuration (non-secret) ---- */
typedef struct {
    gchar *theme;                  /* "dark", "light", "system" */
    gint window_width;
    gint window_height;
    gchar *export_dir;
    gchar *default_aspect;
    gint default_duration;
    gchar *default_voice;
    gchar *default_visual;
    gchar *ffmpeg_path;
    gchar *ffprobe_path;
    gchar *unsplash_access_key;
    gint cache_limit_mb;
    gint max_concurrent_jobs;
    gboolean confirm_paid_providers;
    gboolean auto_fallback;
    gdouble music_gain;
    gdouble narration_gain;
} AsConfig;

AsConfig *as_config_new(void);
void as_config_free(AsConfig *config);
gboolean as_config_load(AsConfig *config, const gchar *path, GError **error);
gboolean as_config_save(const AsConfig *config, const gchar *path, GError **error);
void as_config_reset_defaults(AsConfig *config);

/* ---- misc ---- */
gchar *as_now_iso8601(void);
gchar *as_uuid_random(void);
gboolean as_str_is_loopback_url(const gchar *url);
gboolean as_url_validate(const gchar *url, gboolean allow_insecure, GError **error);
gchar *as_sanitize_filename(const gchar *name);
void as_secure_clear(gchar *secret);
gsize as_dir_size(const gchar *path);
