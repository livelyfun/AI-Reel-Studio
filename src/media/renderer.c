/* renderer.c — scene rendering, xfade composition, audio mix and validation.
 *
 * Every stage is a real FFmpeg process. The output is written to a temporary
 * file inside the project and only moved into place after ffprobe verifies
 * MP4/H.264/AAC, dimensions, duration and stream presence.
 */
#define _GNU_SOURCE
#include "as_media.h"
#include "as_core.h"
#include "media_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <glib/gstdio.h>

typedef struct {
    AsRenderProgressFunc progress;
    gpointer user_data;
    gdouble base;
    gdouble span;
    const gchar *stage;
} StageProgress;

static void stage_progress(gdouble fraction, const gchar *stage, gpointer data)
{
    StageProgress *state = data;
    if (state->progress)
        state->progress(state->base + fraction * state->span,
                        state->stage ? state->stage : stage, state->user_data);
}

static GPtrArray *args_new(void)
{
    return g_ptr_array_new_with_free_func(g_free);
}

static void args_add(GPtrArray *args, const gchar *value)
{
    g_ptr_array_add(args, g_strdup(value ? value : ""));
}

static void args_add_printf(GPtrArray *args, const gchar *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    g_ptr_array_add(args, g_strdup_vprintf(fmt, ap));
    va_end(ap);
}

static const gchar *const *args_finish(GPtrArray *args)
{
    g_ptr_array_add(args, NULL);
    return (const gchar *const *) args->pdata;
}

static void remove_tree(const gchar *path)
{
    if (!path)
        return;
    GDir *dir = g_dir_open(path, 0, NULL);
    if (!dir) {
        g_unlink(path);
        return;
    }
    const gchar *name;
    while ((name = g_dir_read_name(dir)) != NULL) {
        gchar *child = g_build_filename(path, name, NULL);
        if (g_file_test(child, G_FILE_TEST_IS_DIR))
            remove_tree(child);
        else
            g_unlink(child);
        g_free(child);
    }
    g_dir_close(dir);
    g_rmdir(path);
}

static gchar *filter_escape(const gchar *path)
{
    GString *out = g_string_new(NULL);
    for (const gchar *p = path; *p; p++) {
        if (*p == '\\' || *p == '\'' || *p == ':' || *p == ',')
            g_string_append_c(out, '\\');
        g_string_append_c(out, *p);
    }
    return g_string_free(out, FALSE);
}

void as_render_options_for_ratio(AsAspectRatio ratio, gboolean preview,
                                 AsRenderOptions *options)
{
    if (!options)
        return;
    memset(options, 0, sizeof(*options));
    options->preview = preview;
    options->fps = 30;
    options->music_gain = 0.35;
    options->narration_gain = 1.0;
    gint w = 0, h = 0;
    as_aspect_dimensions(ratio, &w, &h);
    if (preview) {
        options->width = w / 2;
        options->height = h / 2;
        if (options->width % 2)
            options->width++;
        if (options->height % 2)
            options->height++;
    } else {
        options->width = w;
        options->height = h;
    }
}

static gchar *make_temp_dir(const gchar *project_dir, GError **error)
{
    gchar *template = g_build_filename(project_dir, ".render-XXXXXX", NULL);
    if (!g_mkdtemp(template)) {
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "Could not create a render directory in %s.", project_dir);
        g_free(template);
        return NULL;
    }
    return template;
}

static gboolean render_scene(AsFfmpegRunner *runner, const AsReel *reel,
                             const AsScene *scene, const AsRenderOptions *options,
                             const gchar *temp_dir, gint index, gint scene_count,
                             gdouble clip_duration, StageProgress *stage,
                             GCancellable *cancellable, GError **error)
{
    gchar *ass_path = g_strdup_printf("%s%csene-%02d.ass", temp_dir,
                                      G_DIR_SEPARATOR, index);
    gchar *clip_path = g_strdup_printf("%s%csene-%02d.mp4", temp_dir,
                                       G_DIR_SEPARATOR, index);
    gboolean ok = FALSE;
    if (!as_captions_write_ass(reel, scene, options->width, options->height,
                               ass_path, error))
        goto out;
    if (!scene->image_path || !g_file_test(scene->image_path, G_FILE_TEST_EXISTS)) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "Scene %d has no local image to render.", scene->scene_id);
        goto out;
    }

    gint frames = (gint) llround(clip_duration * options->fps);
    if (frames < options->fps)
        frames = options->fps;
    gint sw = options->width * 2;
    gint sh = options->height * 2;
    gchar *escaped = filter_escape(ass_path);
    gchar *filter;
    if (index % 2 == 0) {
        filter = g_strdup_printf(
            "scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,"
            "zoompan=z='1+0.12*on/%d':x='iw/2-(iw/zoom/2)':"
            "y='ih/2-(ih/zoom/2)':d=%d:s=%dx%d:fps=%d,"
            "subtitles=filename='%s'",
            sw, sh, sw, sh, frames, frames, options->width, options->height,
            options->fps, escaped);
    } else {
        filter = g_strdup_printf(
            "scale=%d:%d:force_original_aspect_ratio=increase,crop=%d:%d,"
            "zoompan=z='1.12':x='(iw-iw/zoom)*on/%d':"
            "y='ih/2-(ih/zoom/2)':d=%d:s=%dx%d:fps=%d,"
            "subtitles=filename='%s'",
            sw, sh, sw, sh, frames, frames, options->width, options->height,
            options->fps, escaped);
    }
    g_free(escaped);

    GPtrArray *args = args_new();
    args_add(args, as_ffmpeg_runner_ffmpeg_path(runner));
    args_add(args, "-hide_banner");
    args_add(args, "-y");
    args_add(args, "-i");
    args_add(args, scene->image_path);
    args_add(args, "-vf");
    args_add(args, filter);
    args_add(args, "-t");
    args_add_printf(args, "%.3f", clip_duration);
    args_add(args, "-an");
    as_ffmpeg_add_video_quality(runner, args, 20, "veryfast");
    args_add(args, "-pix_fmt");
    args_add(args, "yuv420p");
    args_add(args, "-r");
    args_add_printf(args, "%d", options->fps);
    args_add(args, "-movflags");
    args_add(args, "+faststart");
    args_add(args, clip_path);
    ok = as_ffmpeg_run(runner, args_finish(args), clip_duration,
                       stage_progress, stage, cancellable, error);
    g_ptr_array_unref(args);
    g_free(filter);
out:
    g_free(ass_path);
    g_free(clip_path);
    (void) scene_count;
    return ok;
}

static gboolean build_composite(AsFfmpegRunner *runner, const AsReel *reel,
                                const AsRenderOptions *options,
                                const gchar *temp_dir, gint scene_count,
                                gdouble transition, const gchar *output,
                                gdouble total, StageProgress *stage,
                                GCancellable *cancellable, GError **error)
{
    GPtrArray *args = args_new();
    args_add(args, as_ffmpeg_runner_ffmpeg_path(runner));
    args_add(args, "-hide_banner");
    args_add(args, "-y");
    for (gint i = 0; i < scene_count; i++) {
        gchar *clip = g_strdup_printf("%s%csene-%02d.mp4", temp_dir,
                                      G_DIR_SEPARATOR, i);
        args_add(args, "-i");
        args_add(args, clip);
        g_free(clip);
    }

    gchar *narration = NULL;
    if (reel->narration_path &&
        g_file_test(reel->narration_path, G_FILE_TEST_EXISTS))
        narration = g_strdup(reel->narration_path);
    gchar *music = g_build_filename(temp_dir, "..", "music.wav", NULL);
    gchar *music_clean = g_canonicalize_filename(music, NULL);
    g_free(music);
    if (!g_file_test(music_clean, G_FILE_TEST_EXISTS)) {
        g_free(music_clean);
        music_clean = g_build_filename(temp_dir, "music.wav", NULL);
        if (!g_file_test(music_clean, G_FILE_TEST_EXISTS)) {
            g_free(music_clean);
            music_clean = NULL;
        }
    }

    gint next_input = scene_count;
    gint narration_index = -1;
    gint music_index = -1;
    if (narration) {
        narration_index = next_input++;
        args_add(args, "-i");
        args_add(args, narration);
    }
    if (music_clean) {
        music_index = next_input++;
        args_add(args, "-stream_loop");
        args_add(args, "-1");
        args_add(args, "-i");
        args_add(args, music_clean);
    }
    gint silence_index = -1;
    if (narration_index < 0 && music_index < 0) {
        silence_index = next_input++;
        args_add(args, "-f");
        args_add(args, "lavfi");
        args_add(args, "-t");
        args_add_printf(args, "%.3f", total);
        args_add(args, "-i");
        args_add(args, "anullsrc=channel_layout=stereo:sample_rate=48000");
    }

    GString *graph = g_string_new(NULL);
    if (scene_count == 1) {
        g_string_append(graph, "[0:v]null[vout]");
    } else {
        gdouble offset = 0;
        for (gint i = 1; i < scene_count; i++) {
            AsScene *previous = g_ptr_array_index(reel->scenes, i - 1);
            AsScene *boundary = g_ptr_array_index(reel->scenes, i);
            offset += previous->duration;
            const gchar *transition_name = as_ffmpeg_xfade(
                as_transition_from_string(boundary->transition
                                              ? boundary->transition
                                              : previous->transition));
            g_string_append_printf(
                graph, "[%d:v][%d:v]xfade=transition=%s:duration=%.3f:"
                       "offset=%.3f[v%d];",
                i == 1 ? 0 : i - 1, i, transition_name, transition, offset,
                i);
        }
        g_string_append_printf(graph, "[v%d]null[vout]", scene_count - 1);
    }

    if (narration_index >= 0 || music_index >= 0) {
        g_string_append_c(graph, ';');
        if (narration_index >= 0)
            g_string_append_printf(
                graph,
                "[%d:a]aformat=sample_fmts=fltp:sample_rates=48000:"
                "channel_layouts=stereo,volume=%.3f[narr]",
                narration_index, options->narration_gain);
        if (music_index >= 0) {
            if (narration_index >= 0)
                g_string_append_c(graph, ';');
            g_string_append_printf(
                graph,
                "[%d:a]aformat=sample_fmts=fltp:sample_rates=48000:"
                "channel_layouts=stereo,volume=%.3f,"
                "afade=t=in:st=0:d=1,afade=t=out:st=%.3f:d=1[music]",
                music_index, options->music_gain, MAX(0.0, total - 1.2));
        }
        if (narration_index >= 0 && music_index >= 0) {
            g_string_append(
                graph,
                ";[music][narr]sidechaincompress=threshold=0.05:ratio=8:"
                "attack=15:release=320[duck];"
                "[narr][duck]amix=inputs=2:duration=longest:"
                "dropout_transition=0,alimiter=limit=0.95[aout]");
        } else if (narration_index >= 0) {
            g_string_append(graph, ";[narr]alimiter=limit=0.95[aout]");
        } else {
            g_string_append(graph, ";[music]alimiter=limit=0.95[aout]");
        }
    } else {
        g_string_append_printf(graph, ";[%d:a]anull[aout]", silence_index);
    }

    args_add(args, "-filter_complex");
    args_add(args, graph->str);
    args_add(args, "-map");
    args_add(args, "[vout]");
    args_add(args, "-map");
    args_add(args, "[aout]");
    as_ffmpeg_add_video_quality(runner, args, 20, "veryfast");
    args_add(args, "-pix_fmt");
    args_add(args, "yuv420p");
    args_add(args, "-c:a");
    args_add(args, as_ffmpeg_audio_encoder(runner));
    args_add(args, "-b:a");
    args_add(args, "192k");
    args_add(args, "-t");
    args_add_printf(args, "%.3f", total);
    args_add(args, "-movflags");
    args_add(args, "+faststart");
    args_add(args, output);

    gboolean ok = as_ffmpeg_run(runner, args_finish(args), total, stage_progress,
                                stage, cancellable, error);
    g_ptr_array_unref(args);
    g_string_free(graph, TRUE);
    g_free(narration);
    g_free(music_clean);
    return ok;
}

gboolean as_video_render(const AsReel *reel, const gchar *project_dir,
                         AsFfmpegRunner *runner, const AsRenderOptions *options,
                         const gchar *output_path, AsRenderProgressFunc progress,
                         gpointer user_data, GCancellable *cancellable,
                         AsRenderResult *result, GError **error)
{
    g_return_val_if_fail(reel && project_dir && runner && options && output_path,
                         FALSE);
    if (result)
        memset(result, 0, sizeof(*result));
    if (!as_ffmpeg_runner_available(runner)) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "FFmpeg and ffprobe are required to render video.");
        return FALSE;
    }
    if (!reel->scenes || reel->scenes->len == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "There are no scenes to render.");
        return FALSE;
    }
    if (!as_reel_validate(reel, error))
        return FALSE;

    gdouble total = 0;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        total += scene->duration;
    }
    if (progress)
        progress(0.0, "Preparing render", user_data);

    gchar *temp_dir = make_temp_dir(project_dir, error);
    if (!temp_dir)
        return FALSE;

    gdouble min_duration = 10.0;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        min_duration = MIN(min_duration, scene->duration);
    }
    gdouble transition = MIN(0.6, min_duration * 0.25);
    if (transition < 0.2)
        transition = 0.2;

    gboolean ok = TRUE;
    for (guint i = 0; i < reel->scenes->len && ok; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        gdouble clip_duration = scene->duration;
        if (i + 1 < reel->scenes->len)
            clip_duration += transition;
        StageProgress stage = { progress, user_data,
                                (gdouble) i / reel->scenes->len * 0.72,
                                0.72 / reel->scenes->len, "Rendering scenes" };
        ok = render_scene(runner, reel, scene, options, temp_dir, (gint) i,
                          (gint) reel->scenes->len, clip_duration, &stage,
                          cancellable, error);
    }

    gchar *temp_output = g_build_filename(temp_dir, "output.mp4", NULL);
    if (ok) {
        StageProgress stage = { progress, user_data, 0.72, 0.24,
                                "Compositing" };
        ok = build_composite(runner, reel, options, temp_dir,
                             (gint) reel->scenes->len, transition, temp_output,
                             total, &stage, cancellable, error);
    }

    AsMediaInfo info;
    memset(&info, 0, sizeof(info));
    if (ok) {
        if (progress)
            progress(0.96, "Validating export", user_data);
        ok = as_media_validate_export(runner, temp_output,
                                      reel->request.aspect_ratio, total, &info,
                                      error);
    }

    if (ok) {
        gchar *parent = g_path_get_dirname(output_path);
        if (!as_ensure_dir(parent, error))
            ok = FALSE;
        g_free(parent);
    }
    if (ok) {
        g_unlink(output_path);
        if (g_rename(temp_output, output_path) != 0) {
            gchar *contents = NULL;
            gsize length = 0;
            GError *local = NULL;
            if (!g_file_get_contents(temp_output, &contents, &length, &local)) {
                g_set_error(error, AS_ERROR, AS_ERROR_IO,
                            "Could not move the export into place: %s",
                            local->message);
                g_clear_error(&local);
                ok = FALSE;
            } else if (!g_file_set_contents(output_path, contents,
                                            (gssize) length, &local)) {
                g_set_error(error, AS_ERROR, AS_ERROR_IO,
                            "Could not write the export: %s", local->message);
                g_clear_error(&local);
                ok = FALSE;
            }
            g_free(contents);
        } else {
            g_chmod(output_path, 0644);
        }
    }

    if (ok && result) {
        result->output_path = g_strdup(output_path);
        result->width = info.width;
        result->height = info.height;
        result->duration = info.duration;
        result->file_size = info.size;
        result->video_codec = g_strdup(info.video_codec);
        result->audio_codec = g_strdup(info.audio_codec);
        result->verified = TRUE;
    }
    as_media_info_clear(&info);
    g_free(temp_output);
    remove_tree(temp_dir);
    g_free(temp_dir);
    if (ok && progress)
        progress(1.0, "Export complete", user_data);
    return ok;
}

void as_render_result_clear(AsRenderResult *result)
{
    if (!result)
        return;
    g_free(result->output_path);
    g_free(result->video_codec);
    g_free(result->audio_codec);
    memset(result, 0, sizeof(*result));
}

gchar *as_media_thumbnail_from_image(const gchar *image_path,
                                     const gchar *dest_path,
                                     AsFfmpegRunner *runner, GError **error)
{
    if (!image_path || !dest_path || !runner) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "Thumbnail generation needs an image, destination and FFmpeg.");
        return NULL;
    }
    if (!g_file_test(image_path, G_FILE_TEST_EXISTS)) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND,
                    "Thumbnail source image not found.");
        return NULL;
    }
    GPtrArray *args = args_new();
    args_add(args, as_ffmpeg_runner_ffmpeg_path(runner));
    args_add(args, "-hide_banner");
    args_add(args, "-y");
    args_add(args, "-i");
    args_add(args, image_path);
    args_add(args, "-vf");
    args_add(args, "scale=640:-2:force_original_aspect_ratio=decrease");
    args_add(args, "-frames:v");
    args_add(args, "1");
    args_add(args, "-q:v");
    args_add(args, "3");
    args_add(args, dest_path);
    gboolean ok = as_ffmpeg_run(runner, args_finish(args), 1.0, NULL, NULL, NULL,
                                error);
    g_ptr_array_unref(args);
    if (!ok)
        return NULL;
    return g_strdup(dest_path);
}
