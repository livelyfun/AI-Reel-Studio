/* narration.c — provider speech synthesis with a content-addressed cache. */
#include "as_generation.h"
#include "as_core.h"

#include <string.h>

#include <glib/gstdio.h>

static gchar *sha256_hex(const gchar *text)
{
    GChecksum *checksum = g_checksum_new(G_CHECKSUM_SHA256);
    g_checksum_update(checksum, (const guchar *) text, strlen(text));
    gchar *digest = g_strdup(g_checksum_get_string(checksum));
    g_checksum_free(checksum);
    return digest;
}

static gchar *find_cached(const gchar *dir, const gchar *hash)
{
    GDir *handle = g_dir_open(dir, 0, NULL);
    if (!handle)
        return NULL;
    gchar *found = NULL;
    const gchar *name;
    gsize prefix = strlen(hash);
    while ((name = g_dir_read_name(handle)) != NULL) {
        if (g_str_has_prefix(name, hash) && name[prefix] == '.') {
            found = g_build_filename(dir, name, NULL);
            break;
        }
    }
    g_dir_close(handle);
    return found;
}

static gboolean copy_file_atomic(const gchar *source, const gchar *dest,
                                 GError **error)
{
    gchar *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(source, &contents, &length, error))
        return FALSE;
    gboolean ok = g_file_set_contents(dest, contents, (gssize) length, error);
    g_free(contents);
    if (ok)
        g_chmod(dest, 0644);
    return ok;
}

gboolean as_generation_synthesize_narration(AsGenerationContext *ctx, AsReel *reel,
                                            const gchar *project_dir,
                                            gboolean force,
                                            GCancellable *cancellable,
                                            GError **error)
{
    g_return_val_if_fail(ctx && reel && project_dir, FALSE);
    const gchar *text = reel->narration_text;
    gchar *joined = NULL;
    if (!text || !*text) {
        GString *all = g_string_new(NULL);
        for (guint i = 0; i < reel->scenes->len; i++) {
            AsScene *scene = g_ptr_array_index(reel->scenes, i);
            if (!scene->narration)
                continue;
            if (all->len)
                g_string_append_c(all, ' ');
            g_string_append(all, scene->narration);
        }
        joined = g_string_free(all, FALSE);
        text = joined;
        g_free(reel->narration_text);
        reel->narration_text = g_strdup(joined);
    }
    if (!text || !*text) {
        g_free(joined);
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "There is no narration text to synthesize.");
        return FALSE;
    }

    const gchar *model = NULL;
    AsProviderConfig *cfg = as_provider_registry_resolve(
        ctx->providers, AS_ROUTE_SPEECH, &model, error);
    if (!cfg) {
        g_free(joined);
        return FALSE;
    }
    const gchar *voice = (cfg->speech_voice && *cfg->speech_voice)
                             ? cfg->speech_voice
                             : "";
    gchar *key_material = g_strdup_printf(
        "v1|%s|%s|%s|%s|%d|%d", cfg->id, model, voice, text,
        (gint) reel->request.duration, (gint) reel->request.aspect_ratio);
    gchar *hash = sha256_hex(key_material);
    g_free(key_material);

    gchar *cache_dir = g_build_filename(ctx->paths->cache_dir, "narration", NULL);
    if (!as_ensure_dir(cache_dir, error)) {
        g_free(cache_dir);
        g_free(hash);
        g_free(joined);
        return FALSE;
    }
    gchar *cached = find_cached(cache_dir, hash);
    gchar *final_name = NULL;
    if (!force && cached) {
        const gchar *ext = strrchr(cached, '.');
        final_name = g_strdup_printf("narration%s", ext ? ext : ".wav");
        gchar *dest = g_build_filename(project_dir, final_name, NULL);
        if (copy_file_atomic(cached, dest, error)) {
            g_free(reel->narration_path);
            reel->narration_path = dest;
            g_free(final_name);
            g_free(cached);
            g_free(cache_dir);
            g_free(hash);
            g_free(joined);
            return TRUE;
        }
        g_free(dest);
        g_free(final_name);
        g_free(cached);
        g_free(cache_dir);
        g_free(hash);
        g_free(joined);
        return FALSE;
    }
    g_free(cached);

    if (force && reel->narration_path) {
        gchar *old = g_strdup(reel->narration_path);
        g_free(reel->narration_path);
        reel->narration_path = NULL;
        g_unlink(old);
        g_free(old);
    }

    gchar *api_key = as_credential_manager_get(ctx->credentials, cfg->id, error);
    if (!api_key) {
        g_free(cache_dir);
        g_free(hash);
        g_free(joined);
        return FALSE;
    }
    if (*api_key)
        as_redact_register_secret(api_key);

    AsSpeechResult speech;
    memset(&speech, 0, sizeof(speech));
    gboolean ok = as_provider_generate_speech(cfg, api_key, model, voice, text,
                                              &speech, cancellable, error);
    as_secure_clear(api_key);
    g_free(joined);
    if (!ok) {
        g_free(cache_dir);
        g_free(hash);
        return FALSE;
    }
    if (!speech.data || g_bytes_get_size(speech.data) < 512) {
        as_speech_result_clear(&speech);
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The speech provider returned no usable audio.");
        g_free(cache_dir);
        g_free(hash);
        return FALSE;
    }
    const gchar *ext = (speech.extension && *speech.extension)
                          ? speech.extension
                          : "wav";
    gchar *cache_name = g_strdup_printf("%s.%s", hash, ext);
    gchar *cache_path = g_build_filename(cache_dir, cache_name, NULL);
    gchar *part_path = g_strdup_printf("%s.part", cache_path);
    gsize length = 0;
    const gchar *data = g_bytes_get_data(speech.data, &length);
    ok = g_file_set_contents(part_path, data, (gssize) length, error);
    if (ok) {
        g_chmod(part_path, 0644);
        if (g_rename(part_path, cache_path) != 0) {
            g_set_error(error, AS_ERROR, AS_ERROR_IO,
                        "Could not store the narration cache file.");
            ok = FALSE;
        }
    }
    g_free(part_path);
    if (!ok) {
        as_speech_result_clear(&speech);
        g_free(cache_name);
        g_free(cache_path);
        g_free(cache_dir);
        g_free(hash);
        return FALSE;
    }

    final_name = g_strdup_printf("narration.%s", ext);
    gchar *dest = g_build_filename(project_dir, final_name, NULL);
    ok = copy_file_atomic(cache_path, dest, error);
    if (ok) {
        gdouble duration = as_media_duration(ctx->ffmpeg, dest, error);
        if (duration <= 0) {
            g_unlink(dest);
            ok = FALSE;
        }
    }
    if (ok) {
        g_free(reel->narration_path);
        reel->narration_path = dest;
    } else {
        g_free(dest);
    }
    g_free(final_name);
    g_free(cache_name);
    g_free(cache_path);
    g_free(cache_dir);
    g_free(hash);
    as_speech_result_clear(&speech);
    return ok;
}

gboolean as_generation_synthesize_music(AsGenerationContext *ctx, AsReel *reel,
                                        const gchar *project_dir, GError **error)
{
    g_return_val_if_fail(ctx && reel && project_dir, FALSE);
    const gchar *mood = reel->music_mood ? reel->music_mood
                                         : as_music_mood_for_styles(
                                               reel->request.voice_style,
                                               reel->request.visual_style);
    gdouble seconds = MAX(1.0, reel->total_duration + 0.5);
    gchar *path = g_build_filename(project_dir, "music.wav", NULL);
    if (!as_music_synthesize(mood, seconds, path, error)) {
        g_free(path);
        return FALSE;
    }
    g_free(reel->music_mood);
    reel->music_mood = g_strdup(mood);
    g_free(path);
    return TRUE;
}

gboolean as_generation_regenerate_narration(AsGenerationContext *ctx,
                                            AsReel *reel,
                                            const gchar *project_dir,
                                            GCancellable *cancellable,
                                            GError **error)
{
    return as_generation_synthesize_narration(ctx, reel, project_dir, TRUE,
                                              cancellable, error);
}
