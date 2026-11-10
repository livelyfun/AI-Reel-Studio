/* core.c — errors, redaction, logging, XDG paths, configuration, helpers. */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "as_core.h"
#include "as_types.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>

/* ---------------- errors ---------------- */

GQuark as_error_quark(void)
{
    return g_quark_from_static_string("as-error");
}

GError *as_error_new(AsErrorCode code, const gchar *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    gchar *message = g_strdup_vprintf(fmt, args);
    va_end(args);
    GError *error = g_error_new_literal(AS_ERROR, code, message);
    g_free(message);
    return error;
}

AsErrorCode as_error_code(const GError *error)
{
    if (!error || error->domain != AS_ERROR)
        return AS_ERROR_UNKNOWN;
    return (AsErrorCode) error->code;
}

gboolean as_error_is_retryable(const GError *error)
{
    if (!error)
        return FALSE;
    switch (as_error_code(error)) {
    case AS_ERROR_RATE_LIMIT:
    case AS_ERROR_UNAVAILABLE:
    case AS_ERROR_NETWORK:
    case AS_ERROR_TIMEOUT:
        return TRUE;
    default:
        return FALSE;
    }
}

/* ---------------- redaction ---------------- */

static GMutex redaction_lock;
static GHashTable *registered_secrets; /* gchar* -> gchar* */
static GRegex *rx_auth;
static GRegex *rx_xkey;
static GRegex *rx_query_key;
static GRegex *rx_json_key;
static GRegex *rx_sk;

static void redaction_init(void)
{
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        registered_secrets = g_hash_table_new_full(g_str_hash, g_str_equal,
                                                   g_free, NULL);
        rx_auth = g_regex_new("(?i)(authorization\\s*:\\s*)([^\\r\\n]+)",
                              G_REGEX_OPTIMIZE, 0, NULL);
        rx_xkey = g_regex_new("(?i)(x-api-key\\s*:\\s*)([^\\r\\n]+)",
                              G_REGEX_OPTIMIZE, 0, NULL);
        rx_query_key = g_regex_new("([?&]key=)([^&\\s\"'])",
                                   G_REGEX_OPTIMIZE, 0, NULL);
        rx_json_key = g_regex_new("(?i)(\"api[_-]?key\"\\s*:\\s*\")([^\"]+)",
                                  G_REGEX_OPTIMIZE, 0, NULL);
        rx_sk = g_regex_new("\\bsk-[A-Za-z0-9_-]{12,}\\b",
                            G_REGEX_OPTIMIZE, 0, NULL);
        g_once_init_leave(&once, 1);
    }
}

void as_redact_register_secret(const gchar *secret)
{
    if (!secret || strlen(secret) < 4)
        return;
    redaction_init();
    g_mutex_lock(&redaction_lock);
    g_hash_table_add(registered_secrets, g_strdup(secret));
    g_mutex_unlock(&redaction_lock);
}

void as_redact_clear_secrets(void)
{
    if (!registered_secrets)
        return;
    g_mutex_lock(&redaction_lock);
    g_hash_table_remove_all(registered_secrets);
    g_mutex_unlock(&redaction_lock);
}

static gchar *apply_pattern(GRegex *rx, const gchar *input, const gchar *replacement)
{
    if (!rx || !input)
        return g_strdup(input);
    return g_regex_replace(rx, input, -1, 0, replacement, 0, NULL);
}

gchar *as_redact(const gchar *text)
{
    if (!text)
        return g_strdup("");
    redaction_init();
    gchar *result = g_strdup(text);

    g_mutex_lock(&redaction_lock);
    if (registered_secrets) {
        GHashTableIter iter;
        gpointer key;
        g_hash_table_iter_init(&iter, registered_secrets);
        while (g_hash_table_iter_next(&iter, &key, NULL)) {
            const gchar *secret = key;
            if (!*secret)
                continue;
            gchar **parts = g_strsplit(result, secret, -1);
            g_free(result);
            result = g_strjoinv("[REDACTED]", parts);
            g_strfreev(parts);
        }
    }
    g_mutex_unlock(&redaction_lock);

    gchar *step = apply_pattern(rx_auth, result, "\\1[REDACTED]");
    g_free(result);
    result = step;
    step = apply_pattern(rx_xkey, result, "\\1[REDACTED]");
    g_free(result);
    result = step;
    step = apply_pattern(rx_query_key, result, "\\1[REDACTED]");
    g_free(result);
    result = step;
    step = apply_pattern(rx_json_key, result, "\\1[REDACTED]");
    g_free(result);
    result = step;
    step = apply_pattern(rx_sk, result, "[REDACTED]");
    g_free(result);
    return step;
}

/* ---------------- logging ---------------- */

static FILE *log_file;
static GMutex log_lock;
static gchar *log_path;
static goffset log_size;

gboolean as_log_init(const gchar *log_dir, GError **error)
{
    if (!as_ensure_dir(log_dir, error))
        return FALSE;
    g_mutex_lock(&log_lock);
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }
    g_free(log_path);
    log_path = g_build_filename(log_dir, "studio.log", NULL);
    log_file = g_fopen(log_path, "a");
    if (!log_file) {
        int saved = errno;
        g_mutex_unlock(&log_lock);
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "Could not open log file '%s': %s", log_path,
                    g_strerror(saved));
        return FALSE;
    }
    g_chmod(log_path, 0600);
    log_size = 0;
    fseeko(log_file, 0, SEEK_END);
    log_size = ftello(log_file);
    g_mutex_unlock(&log_lock);
    return TRUE;
}

static void log_rotate_locked(void)
{
    if (log_size < 5 * 1024 * 1024 || !log_path)
        return;
    fclose(log_file);
    gchar *old = g_strconcat(log_path, ".1", NULL);
    g_remove(old);
    g_rename(log_path, old);
    g_free(old);
    log_file = g_fopen(log_path, "w");
    if (log_file)
        g_chmod(log_path, 0600);
    log_size = 0;
}

void as_log(AsLogLevel level, const gchar *domain, const gchar *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    gchar *message = g_strdup_vprintf(fmt, args);
    va_end(args);

    const gchar *labels[] = { "DEBUG", "INFO", "WARN", "ERROR" };
    gchar *safe = as_redact(message);
    gchar *timestamp = as_now_iso8601();

    g_mutex_lock(&log_lock);
    if (log_file) {
        log_rotate_locked();
        if (log_file) {
            int written = fprintf(log_file, "%s [%s] %s: %s\n", timestamp,
                                  labels[level], domain ? domain : "app", safe);
            fflush(log_file);
            if (written > 0)
                log_size += written;
        }
    }
    g_mutex_unlock(&log_lock);

    if (level >= AS_LOG_WARNING)
        g_printerr("%s: %s\n", labels[level], safe);

    g_free(safe);
    g_free(timestamp);
    g_free(message);
}

/* ---------------- paths ---------------- */

static gchar *make_dir(const gchar *base, const gchar *leaf, GError **error)
{
    gchar *path = leaf ? g_build_filename(base, leaf, NULL) : g_strdup(base);
    if (!as_ensure_dir(path, error)) {
        g_free(path);
        return NULL;
    }
    return path;
}

gboolean as_ensure_dir(const gchar *path, GError **error)
{
    if (!path || !*path) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "No directory path was provided.");
        return FALSE;
    }
    if (g_mkdir_with_parents(path, 0700) != 0 && errno != EEXIST) {
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "Could not create directory '%s': %s", path,
                    g_strerror(errno));
        return FALSE;
    }
    g_chmod(path, 0700);
    struct stat st;
    if (g_stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "'%s' is not a directory.", path);
        return FALSE;
    }
    return TRUE;
}

AsPaths *as_paths_new(GError **error)
{
    AsPaths *paths = g_new0(AsPaths, 1);
    const gchar *data = g_get_user_data_dir();
    const gchar *config = g_get_user_config_dir();
    const gchar *cache = g_get_user_cache_dir();

    paths->data_dir = make_dir(data, "ai-reel-studio", error);
    if (!paths->data_dir)
        goto fail;
    paths->config_dir = make_dir(config, "ai-reel-studio", error);
    if (!paths->config_dir)
        goto fail;
    paths->cache_dir = make_dir(cache, "ai-reel-studio", error);
    if (!paths->cache_dir)
        goto fail;
    paths->projects_dir = make_dir(paths->data_dir, "projects", error);
    if (!paths->projects_dir)
        goto fail;
    paths->logs_dir = make_dir(paths->data_dir, "logs", error);
    if (!paths->logs_dir)
        goto fail;

    gchar *db_dir = make_dir(paths->data_dir, "database", error);
    if (!db_dir)
        goto fail;
    paths->db_path = g_build_filename(db_dir, "studio.db", NULL);
    paths->db_backup_dir = make_dir(db_dir, "backups", error);
    g_free(db_dir);
    if (!paths->db_backup_dir)
        goto fail;

    gchar *tmp_base = make_dir(paths->cache_dir, "temporary", error);
    if (!tmp_base)
        goto fail;
    paths->tmp_dir = g_build_filename(tmp_base, "jobs", NULL);
    g_free(tmp_base);
    if (!as_ensure_dir(paths->tmp_dir, error))
        goto fail;

    paths->settings_path = g_build_filename(paths->config_dir, "settings.ini", NULL);
    return paths;

fail:
    as_paths_free(paths);
    return NULL;
}

void as_paths_free(AsPaths *paths)
{
    if (!paths)
        return;
    g_free(paths->data_dir);
    g_free(paths->config_dir);
    g_free(paths->cache_dir);
    g_free(paths->projects_dir);
    g_free(paths->logs_dir);
    g_free(paths->db_path);
    g_free(paths->db_backup_dir);
    g_free(paths->settings_path);
    g_free(paths->tmp_dir);
    g_free(paths);
}

gchar *as_paths_project_dir(const AsPaths *paths, const gchar *project_id)
{
    if (!paths || !project_id)
        return NULL;
    return g_build_filename(paths->projects_dir, project_id, NULL);
}

/* ---------------- configuration ---------------- */

static void replace_string(gchar **dst, const gchar *src)
{
    g_free(*dst);
    *dst = g_strdup(src);
}

void as_config_reset_defaults(AsConfig *config)
{
    memset(config, 0, sizeof(*config));
    config->theme = g_strdup("dark");
    config->window_width = 1360;
    config->window_height = 860;
    const gchar *videos = g_get_user_special_dir(G_USER_DIRECTORY_VIDEOS);
    if (!videos || !*videos)
        videos = g_get_home_dir();
    config->export_dir = g_strdup(videos);
    config->default_aspect = g_strdup("9:16");
    config->default_duration = 30;
    config->default_voice = g_strdup("Cinematic");
    config->default_visual = g_strdup("Cinematic");
    config->ffmpeg_path = g_strdup("ffmpeg");
    config->ffprobe_path = g_strdup("ffprobe");
    config->unsplash_access_key = g_strdup("");
    config->cache_limit_mb = 2048;
    config->max_concurrent_jobs = 1;
    config->confirm_paid_providers = TRUE;
    config->auto_fallback = FALSE;
    config->music_gain = 0.35;
    config->narration_gain = 1.0;
}

AsConfig *as_config_new(void)
{
    AsConfig *config = g_new0(AsConfig, 1);
    as_config_reset_defaults(config);
    return config;
}

void as_config_free(AsConfig *config)
{
    if (!config)
        return;
    g_free(config->theme);
    g_free(config->export_dir);
    g_free(config->default_aspect);
    g_free(config->default_voice);
    g_free(config->default_visual);
    g_free(config->ffmpeg_path);
    g_free(config->ffprobe_path);
    g_free(config->unsplash_access_key);
    g_free(config);
}

static gchar *keyfile_string(GKeyFile *kf, const gchar *group, const gchar *key,
                             const gchar *fallback)
{
    gchar *value = g_key_file_get_string(kf, group, key, NULL);
    if (!value || !*value) {
        g_free(value);
        return g_strdup(fallback);
    }
    return value;
}

gboolean as_config_load(AsConfig *config, const gchar *path, GError **error)
{
    if (!g_file_test(path, G_FILE_TEST_EXISTS))
        return TRUE;
    GKeyFile *kf = g_key_file_new();
    GError *local = NULL;
    if (!g_key_file_load_from_file(kf, path, G_KEY_FILE_NONE, &local)) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "Could not load settings: %s", local->message);
        g_clear_error(&local);
        g_key_file_free(kf);
        return FALSE;
    }

    replace_string(&config->theme, keyfile_string(kf, "general", "theme", "dark"));
    config->window_width = g_key_file_get_integer(kf, "general", "window-width", NULL);
    config->window_height = g_key_file_get_integer(kf, "general", "window-height", NULL);
    replace_string(&config->export_dir,
                   keyfile_string(kf, "general", "export-dir", config->export_dir));
    replace_string(&config->default_aspect,
                   keyfile_string(kf, "defaults", "aspect", "9:16"));
    config->default_duration = g_key_file_get_integer(kf, "defaults", "duration", NULL);
    replace_string(&config->default_voice,
                   keyfile_string(kf, "defaults", "voice", "Cinematic"));
    replace_string(&config->default_visual,
                   keyfile_string(kf, "defaults", "visual", "Cinematic"));
    replace_string(&config->ffmpeg_path,
                   keyfile_string(kf, "media", "ffmpeg", "ffmpeg"));
    replace_string(&config->ffprobe_path,
                   keyfile_string(kf, "media", "ffprobe", "ffprobe"));
    replace_string(&config->unsplash_access_key,
                   keyfile_string(kf, "media", "unsplash-access-key", ""));
    config->cache_limit_mb = g_key_file_get_integer(kf, "media", "cache-limit-mb", NULL);
    config->max_concurrent_jobs =
        g_key_file_get_integer(kf, "media", "max-concurrent-jobs", NULL);
    config->confirm_paid_providers =
        g_key_file_get_boolean(kf, "providers", "confirm-paid", NULL);
    config->auto_fallback =
        g_key_file_get_boolean(kf, "providers", "auto-fallback", NULL);
    config->music_gain = g_key_file_get_double(kf, "audio", "music-gain", NULL);
    config->narration_gain = g_key_file_get_double(kf, "audio", "narration-gain", NULL);

    g_key_file_free(kf);

    if (!config->theme)
        config->theme = g_strdup("dark");
    if (config->window_width < 900 || config->window_width > 8000)
        config->window_width = 1360;
    if (config->window_height < 600 || config->window_height > 6000)
        config->window_height = 860;
    if (!as_duration_valid(config->default_duration))
        config->default_duration = 30;
    if (!as_voice_style_valid(config->default_voice))
        replace_string(&config->default_voice, "Cinematic");
    if (!as_visual_style_valid(config->default_visual))
        replace_string(&config->default_visual, "Cinematic");
    if (!config->default_aspect || !*config->default_aspect)
        replace_string(&config->default_aspect, "9:16");
    if (config->cache_limit_mb < 128)
        config->cache_limit_mb = 128;
    if (config->max_concurrent_jobs < 1 || config->max_concurrent_jobs > 4)
        config->max_concurrent_jobs = 1;
    if (config->music_gain < 0.0 || config->music_gain > 1.0)
        config->music_gain = 0.35;
    if (config->narration_gain < 0.1 || config->narration_gain > 2.0)
        config->narration_gain = 1.0;
    return TRUE;
}

gboolean as_config_save(const AsConfig *config, const gchar *path, GError **error)
{
    GKeyFile *kf = g_key_file_new();
    g_key_file_set_string(kf, "general", "theme", config->theme);
    g_key_file_set_integer(kf, "general", "window-width", config->window_width);
    g_key_file_set_integer(kf, "general", "window-height", config->window_height);
    g_key_file_set_string(kf, "general", "export-dir", config->export_dir);
    g_key_file_set_string(kf, "defaults", "aspect", config->default_aspect);
    g_key_file_set_integer(kf, "defaults", "duration", config->default_duration);
    g_key_file_set_string(kf, "defaults", "voice", config->default_voice);
    g_key_file_set_string(kf, "defaults", "visual", config->default_visual);
    g_key_file_set_string(kf, "media", "ffmpeg", config->ffmpeg_path);
    g_key_file_set_string(kf, "media", "ffprobe", config->ffprobe_path);
    g_key_file_set_string(kf, "media", "unsplash-access-key",
                          config->unsplash_access_key);
    g_key_file_set_integer(kf, "media", "cache-limit-mb", config->cache_limit_mb);
    g_key_file_set_integer(kf, "media", "max-concurrent-jobs",
                           config->max_concurrent_jobs);
    g_key_file_set_boolean(kf, "providers", "confirm-paid",
                           config->confirm_paid_providers);
    g_key_file_set_boolean(kf, "providers", "auto-fallback", config->auto_fallback);
    g_key_file_set_double(kf, "audio", "music-gain", config->music_gain);
    g_key_file_set_double(kf, "audio", "narration-gain", config->narration_gain);

    gsize length = 0;
    gchar *data = g_key_file_to_data(kf, &length, NULL);
    g_key_file_free(kf);

    GError *local = NULL;
    if (!g_file_set_contents(path, data, (gssize) length, &local)) {
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "Could not save settings: %s", local->message);
        g_clear_error(&local);
        g_free(data);
        return FALSE;
    }
    g_chmod(path, 0600);
    g_free(data);
    return TRUE;
}

/* ---------------- misc ---------------- */

gchar *as_now_iso8601(void)
{
    GDateTime *now = g_date_time_new_now_local();
    gchar *value = g_date_time_format_iso8601(now);
    g_date_time_unref(now);
    return value;
}

gchar *as_uuid_random(void)
{
    return g_uuid_string_random();
}

gboolean as_str_is_loopback_url(const gchar *url)
{
    if (!url)
        return FALSE;
    GError *error = NULL;
    GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, &error);
    if (!uri) {
        g_clear_error(&error);
        return FALSE;
    }
    const gchar *host = g_uri_get_host(uri);
    gboolean loopback = FALSE;
    if (host) {
        if (g_ascii_strcasecmp(host, "localhost") == 0 ||
            g_strcmp0(host, "::1") == 0)
            loopback = TRUE;
        else if (g_str_has_prefix(host, "127."))
            loopback = TRUE;
    }
    g_uri_unref(uri);
    return loopback;
}

gboolean as_url_validate(const gchar *url, gboolean allow_insecure, GError **error)
{
    if (!url || !*url) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG, "The base URL is empty.");
        return FALSE;
    }
    GError *local = NULL;
    GUri *uri = g_uri_parse(url, G_URI_FLAGS_NONE, &local);
    if (!uri) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG, "Invalid URL: %s",
                    local ? local->message : "parse failure");
        g_clear_error(&local);
        return FALSE;
    }
    const gchar *scheme = g_uri_get_scheme(uri);
    const gchar *host = g_uri_get_host(uri);
    const gchar *userinfo = g_uri_get_userinfo(uri);
    gboolean ok = TRUE;
    if (!scheme || (g_ascii_strcasecmp(scheme, "https") != 0 &&
                    g_ascii_strcasecmp(scheme, "http") != 0)) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "Only http:// and https:// URLs are supported.");
        ok = FALSE;
    } else if (!host || !*host) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "The URL has no host name.");
        ok = FALSE;
    } else if (userinfo && *userinfo) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "Credentials embedded in URLs are not allowed.");
        ok = FALSE;
    } else if (g_ascii_strcasecmp(scheme, "http") == 0 &&
               !as_str_is_loopback_url(url) && !allow_insecure) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "Plain HTTP is only allowed for localhost. Use HTTPS or "
                    "explicitly allow an insecure endpoint.");
        ok = FALSE;
    }
    g_uri_unref(uri);
    return ok;
}

gchar *as_sanitize_filename(const gchar *name)
{
    if (!name || !*name)
        return g_strdup("ai_reel");
    GString *out = g_string_new(NULL);
    for (const gchar *p = name; *p && out->len < 120; p++) {
        gunichar c = g_utf8_get_char(p);
        if (c == 0)
            break;
        if (g_unichar_isalnum(c) || c == ' ' || c == '-' || c == '_' || c == '.')
            g_string_append_unichar(out, c);
        else
            g_string_append_c(out, '_');
        p = g_utf8_next_char(p) - 1;
    }
    g_strstrip(out->str);
    while (out->len && (out->str[0] == '.' || out->str[0] == '-'))
        g_string_erase(out, 0, 1);
    if (!out->len)
        g_string_append(out, "ai_reel");
    return g_string_free(out, FALSE);
}

void as_secure_clear(gchar *secret)
{
    if (!secret)
        return;
    gsize len = strlen(secret);
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 25))
    explicit_bzero(secret, len);
#else
    volatile gchar *p = secret;
    while (len--)
        *p++ = 0;
#endif
    g_free(secret);
}

gsize as_dir_size(const gchar *path)
{
    if (!path)
        return 0;
    GDir *dir = g_dir_open(path, 0, NULL);
    if (!dir)
        return 0;
    gsize total = 0;
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *child = g_build_filename(path, name, NULL);
        GStatBuf st;
        if (g_lstat(child, &st) == 0) {
            if (S_ISREG(st.st_mode))
                total += (gsize) st.st_size;
            else if (S_ISDIR(st.st_mode))
                total += as_dir_size(child);
        }
        g_free(child);
    }
    g_dir_close(dir);
    return total;
}
