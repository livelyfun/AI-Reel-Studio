/* ffmpeg.c — GSubprocess wrapper with real progress parsing and cancellation. */
#define _GNU_SOURCE
#include "media_internal.h"
#include "as_core.h"
#include "as_json.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib/gstdio.h>
#include <gio/gio.h>

struct _AsFfmpegRunner {
    gchar *ffmpeg_path;
    gchar *ffprobe_path;
    gchar *video_encoder;
    gchar *audio_encoder;
    GMutex lock;
};

static gboolean program_available(const gchar *path)
{
    if (!path || !*path)
        return FALSE;
    if (g_path_is_absolute(path))
        return g_file_test(path, G_FILE_TEST_IS_EXECUTABLE);
    gchar *found = g_find_program_in_path(path);
    gboolean ok = found != NULL;
    g_free(found);
    return ok;
}

AsFfmpegRunner *as_ffmpeg_runner_new(const gchar *ffmpeg_path,
                                     const gchar *ffprobe_path)
{
    AsFfmpegRunner *runner = g_new0(AsFfmpegRunner, 1);
    runner->ffmpeg_path = g_strdup((ffmpeg_path && *ffmpeg_path) ? ffmpeg_path
                                                                 : "ffmpeg");
    runner->ffprobe_path = g_strdup((ffprobe_path && *ffprobe_path)
                                        ? ffprobe_path
                                        : "ffprobe");
    g_mutex_init(&runner->lock);
    return runner;
}

void as_ffmpeg_runner_free(AsFfmpegRunner *runner)
{
    if (!runner)
        return;
    g_free(runner->ffmpeg_path);
    g_free(runner->ffprobe_path);
    g_free(runner->video_encoder);
    g_free(runner->audio_encoder);
    g_mutex_clear(&runner->lock);
    g_free(runner);
}

gboolean as_ffmpeg_runner_available(AsFfmpegRunner *runner)
{
    return runner && program_available(runner->ffmpeg_path) &&
           program_available(runner->ffprobe_path);
}

const gchar *as_ffmpeg_runner_ffmpeg_path(AsFfmpegRunner *runner)
{
    return runner ? runner->ffmpeg_path : NULL;
}

const gchar *as_ffmpeg_runner_ffprobe_path(AsFfmpegRunner *runner)
{
    return runner ? runner->ffprobe_path : NULL;
}

static gboolean run_capture(AsFfmpegRunner *runner, const gchar *const *argv,
                            gchar **stdout_out, gchar **stderr_out,
                            GError **error)
{
    if (!runner) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "The FFmpeg runner is unavailable.");
        return FALSE;
    }
    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE |
                                  G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_setenv(launcher, "LC_ALL", "C", TRUE);
    GError *local = NULL;
    GSubprocess *process =
        g_subprocess_launcher_spawnv(launcher, argv, &local);
    g_object_unref(launcher);
    if (!process) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "Could not start '%s': %s", argv[0],
                    local ? local->message : "spawn failed");
        g_clear_error(&local);
        return FALSE;
    }
    gchar *out = NULL;
    gchar *err = NULL;
    gboolean ok = g_subprocess_communicate_utf8(process, NULL, NULL, &out, &err,
                                                &local);
    if (!ok) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA, "Process failed: %s",
                    local ? local->message : "communication error");
        g_clear_error(&local);
        g_object_unref(process);
        g_free(out);
        g_free(err);
        return FALSE;
    }
    gboolean exited = g_subprocess_get_successful(process);
    if (!exited) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA, "%s failed: %s", argv[0],
                    err && *err ? err : "non-zero exit status");
        g_object_unref(process);
        g_free(out);
        g_free(err);
        return FALSE;
    }
    g_object_unref(process);
    if (stdout_out)
        *stdout_out = out;
    else
        g_free(out);
    if (stderr_out)
        *stderr_out = err;
    else
        g_free(err);
    return TRUE;
}

static void detect_encoders(AsFfmpegRunner *runner)
{
    g_mutex_lock(&runner->lock);
    if (runner->video_encoder) {
        g_mutex_unlock(&runner->lock);
        return;
    }
    const gchar *argv[] = { runner->ffmpeg_path, "-hide_banner", "-encoders",
                            NULL };
    gchar *out = NULL;
    gboolean ok = run_capture(runner, argv, &out, NULL, NULL);
    if (ok && out) {
        if (strstr(out, "libx264"))
            runner->video_encoder = g_strdup("libx264");
        else if (strstr(out, "libopenh264"))
            runner->video_encoder = g_strdup("libopenh264");
        else if (strstr(out, "h264"))
            runner->video_encoder = g_strdup("h264");
        else
            runner->video_encoder = g_strdup("mpeg4");
        if (strstr(out, " aac ") || strstr(out, " aac"))
            runner->audio_encoder = g_strdup("aac");
        else
            runner->audio_encoder = g_strdup("aac");
    } else {
        runner->video_encoder = g_strdup("libx264");
        runner->audio_encoder = g_strdup("aac");
    }
    g_free(out);
    g_mutex_unlock(&runner->lock);
}

const gchar *as_ffmpeg_video_encoder(AsFfmpegRunner *runner)
{
    if (!runner)
        return "libx264";
    detect_encoders(runner);
    return runner->video_encoder;
}

const gchar *as_ffmpeg_audio_encoder(AsFfmpegRunner *runner)
{
    if (!runner)
        return "aac";
    detect_encoders(runner);
    return runner->audio_encoder;
}

void as_ffmpeg_add_video_quality(AsFfmpegRunner *runner, GPtrArray *args,
                                 gint crf, const gchar *preset)
{
    const gchar *encoder = as_ffmpeg_video_encoder(runner);
    g_ptr_array_add(args, g_strdup("-c:v"));
    g_ptr_array_add(args, g_strdup(encoder));
    if (g_strcmp0(encoder, "libx264") == 0) {
        g_ptr_array_add(args, g_strdup("-preset"));
        g_ptr_array_add(args, g_strdup(preset ? preset : "medium"));
        g_ptr_array_add(args, g_strdup("-crf"));
        g_ptr_array_add(args, g_strdup_printf("%d", crf));
    } else if (g_strcmp0(encoder, "libopenh264") == 0) {
        g_ptr_array_add(args, g_strdup("-b:v"));
        g_ptr_array_add(args, g_strdup_printf("%dM", crf <= 20 ? 8 : 5));
        g_ptr_array_add(args, g_strdup("-g"));
        g_ptr_array_add(args, g_strdup("48"));
    } else {
        g_ptr_array_add(args, g_strdup("-q:v"));
        g_ptr_array_add(args, g_strdup("4"));
    }
}

typedef struct {
    gdouble expected;
    AsRenderProgressFunc progress;
    gpointer user_data;
    gchar *last_stderr;
} ProgressState;

static gdouble parse_time_seconds(const gchar *line)
{
    const gchar *pos = strstr(line, "time=");
    if (!pos)
        return -1;
    pos += 5;
    gint h = 0, m = 0;
    gdouble s = 0;
    if (sscanf(pos, "%d:%d:%lf", &h, &m, &s) == 3)
        return h * 3600.0 + m * 60.0 + s;
    return -1;
}

static void on_cancel(GObject *object, gpointer user_data)
{
    (void) object;
    GSubprocess *process = user_data;
    if (process)
        g_subprocess_force_exit(process);
}

gboolean as_ffmpeg_run(AsFfmpegRunner *runner, const gchar *const *argv,
                       gdouble expected_seconds, AsRenderProgressFunc progress,
                       gpointer user_data, GCancellable *cancellable,
                       GError **error)
{
    g_return_val_if_fail(runner != NULL && argv != NULL, FALSE);
    if (g_cancellable_set_error_if_cancelled(cancellable, error))
        return FALSE;

    GSubprocessLauncher *launcher =
        g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_SILENCE |
                                  G_SUBPROCESS_FLAGS_STDERR_PIPE);
    g_subprocess_launcher_setenv(launcher, "LC_ALL", "C", TRUE);
    GError *local = NULL;
    GSubprocess *process = g_subprocess_launcher_spawnv(launcher, argv, &local);
    g_object_unref(launcher);
    if (!process) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "Could not start FFmpeg: %s",
                    local ? local->message : "spawn failed");
        g_clear_error(&local);
        return FALSE;
    }

    gulong cancel_id = 0;
    if (cancellable)
        cancel_id = g_cancellable_connect(cancellable, G_CALLBACK(on_cancel),
                                          g_object_ref(process),
                                          g_object_unref);

    GInputStream *raw = g_subprocess_get_stderr_pipe(process);
    GDataInputStream *stream = g_data_input_stream_new(raw);
    ProgressState state = { expected_seconds, progress, user_data, NULL };
    gchar *line = NULL;
    while ((line = g_data_input_stream_read_line(stream, NULL, NULL, NULL))) {
        if (strstr(line, "time=")) {
            gdouble seconds = parse_time_seconds(line);
            if (seconds >= 0 && expected_seconds > 0 && progress) {
                gdouble fraction = seconds / expected_seconds;
                if (fraction > 0.99)
                    fraction = 0.99;
                progress(fraction, "Rendering", user_data);
            }
        }
        if (*line) {
            g_free(state.last_stderr);
            state.last_stderr = g_strdup(line);
        }
        g_free(line);
        if (cancellable && g_cancellable_is_cancelled(cancellable))
            break;
    }
    g_object_unref(stream);

    gboolean waited = g_subprocess_wait(process, NULL, &local);
    gboolean success = waited && g_subprocess_get_successful(process);
    if (cancel_id)
        g_cancellable_disconnect(cancellable, cancel_id);
    g_object_unref(process);

    if (cancellable && g_cancellable_is_cancelled(cancellable)) {
        g_set_error(error, AS_ERROR, AS_ERROR_CANCELLED,
                    "The media operation was cancelled.");
        g_free(state.last_stderr);
        g_clear_error(&local);
        return FALSE;
    }
    if (!success) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA, "FFmpeg failed: %s",
                    state.last_stderr ? state.last_stderr
                                      : (local ? local->message : "unknown error"));
        g_free(state.last_stderr);
        g_clear_error(&local);
        return FALSE;
    }
    g_free(state.last_stderr);
    g_clear_error(&local);
    if (progress)
        progress(1.0, "Rendering", user_data);
    return TRUE;
}

/* ---------------- probe ---------------- */

static gdouble parse_fraction(const gchar *value)
{
    if (!value || !*value)
        return 0;
    gchar **parts = g_strsplit(value, "/", 2);
    gdouble result = 0;
    if (parts[0] && parts[1]) {
        gdouble num = g_ascii_strtod(parts[0], NULL);
        gdouble den = g_ascii_strtod(parts[1], NULL);
        if (den > 0)
            result = num / den;
    } else if (parts[0]) {
        result = g_ascii_strtod(parts[0], NULL);
    }
    g_strfreev(parts);
    return result;
}

gboolean as_media_probe(AsFfmpegRunner *runner, const gchar *path,
                        AsMediaInfo *info, GError **error)
{
    g_return_val_if_fail(info != NULL, FALSE);
    memset(info, 0, sizeof(*info));
    if (!runner || !path) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA, "Media probe needs a file.");
        return FALSE;
    }
    if (!g_file_test(path, G_FILE_TEST_IS_REGULAR)) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND,
                    "Media file not found: %s", path);
        return FALSE;
    }
    const gchar *argv[] = { runner->ffprobe_path, "-v", "error",
                            "-print_format", "json", "-show_format",
                            "-show_streams", path, NULL };
    gchar *out = NULL;
    if (!run_capture(runner, argv, &out, NULL, error))
        return FALSE;
    JsonNode *node = as_json_parse(out, -1, error);
    g_free(out);
    if (!node)
        return FALSE;
    JsonObject *root = as_json_root_object(node);
    if (!root) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "ffprobe returned an invalid document.");
        json_node_free(node);
        return FALSE;
    }
    JsonObject *format = as_json_get_object(root, "format");
    if (format) {
        info->format_name = as_json_get_string(format, "format_name");
        info->duration = as_json_get_double(format, "duration", 0);
        info->size = as_json_get_int(format, "size", 0);
    }
    JsonArray *streams = as_json_get_array(root, "streams");
    for (guint i = 0; streams && i < json_array_get_length(streams); i++) {
        JsonNode *stream_node = json_array_get_element(streams, i);
        if (!JSON_NODE_HOLDS_OBJECT(stream_node))
            continue;
        JsonObject *stream = json_node_get_object(stream_node);
        gchar *type = as_json_get_string(stream, "codec_type");
        if (g_strcmp0(type, "video") == 0 && !info->has_video) {
            info->has_video = TRUE;
            info->video_codec = as_json_get_string(stream, "codec_name");
            info->width = (gint) as_json_get_int(stream, "width", 0);
            info->height = (gint) as_json_get_int(stream, "height", 0);
            gchar *rate = as_json_get_string(stream, "avg_frame_rate");
            info->fps = parse_fraction(rate);
            g_free(rate);
            if (info->duration <= 0) {
                gchar *duration = as_json_get_string(stream, "duration");
                if (duration)
                    info->duration = g_ascii_strtod(duration, NULL);
                g_free(duration);
            }
        } else if (g_strcmp0(type, "audio") == 0 && !info->has_audio) {
            info->has_audio = TRUE;
            info->audio_codec = as_json_get_string(stream, "codec_name");
        }
        g_free(type);
    }
    json_node_free(node);
    if (info->size <= 0) {
        GStatBuf st;
        if (g_stat(path, &st) == 0)
            info->size = st.st_size;
    }
    return TRUE;
}

void as_media_info_clear(AsMediaInfo *info)
{
    if (!info)
        return;
    g_free(info->format_name);
    g_free(info->video_codec);
    g_free(info->audio_codec);
    memset(info, 0, sizeof(*info));
}

gdouble as_media_duration(AsFfmpegRunner *runner, const gchar *path,
                          GError **error)
{
    AsMediaInfo info;
    if (!as_media_probe(runner, path, &info, error))
        return 0;
    gdouble duration = info.duration;
    as_media_info_clear(&info);
    return duration;
}

gboolean as_media_validate_export(AsFfmpegRunner *runner, const gchar *path,
                                  AsAspectRatio ratio, gdouble expected_duration,
                                  AsMediaInfo *info, GError **error)
{
    AsMediaInfo local;
    AsMediaInfo *target = info ? info : &local;
    if (!as_media_probe(runner, path, target, error))
        return FALSE;
    if (!target->has_video) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The exported file contains no video stream.");
        goto fail;
    }
    if (!target->has_audio) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The exported file contains no audio stream.");
        goto fail;
    }
    if (!target->format_name || !strstr(target->format_name, "mp4")) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The exported container is not MP4 (got %s).",
                    target->format_name ? target->format_name : "unknown");
        goto fail;
    }
    if (g_strcmp0(target->video_codec, "h264") != 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The video codec is %s, expected H.264.",
                    target->video_codec ? target->video_codec : "unknown");
        goto fail;
    }
    if (g_strcmp0(target->audio_codec, "aac") != 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The audio codec is %s, expected AAC.",
                    target->audio_codec ? target->audio_codec : "unknown");
        goto fail;
    }
    gint expected_w = 0, expected_h = 0;
    as_aspect_dimensions(ratio, &expected_w, &expected_h);
    if (target->width <= 0 || target->height <= 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The exported video has invalid dimensions.");
        goto fail;
    }
    gdouble actual_ratio = (gdouble) target->width / (gdouble) target->height;
    gdouble wanted_ratio = (gdouble) expected_w / (gdouble) expected_h;
    if (fabs(actual_ratio - wanted_ratio) > 0.02) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The exported aspect ratio is %.3f, expected %.3f.",
                    actual_ratio, wanted_ratio);
        goto fail;
    }
    if (expected_duration > 0) {
        gdouble tolerance = MAX(2.0, expected_duration * 0.05);
        if (fabs(target->duration - expected_duration) > tolerance) {
            g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                        "The export duration is %.2fs, expected about %.2fs.",
                        target->duration, expected_duration);
            goto fail;
        }
    }
    if (info)
        return TRUE;
    as_media_info_clear(&local);
    return TRUE;
fail:
    if (!info)
        as_media_info_clear(&local);
    return FALSE;
}
