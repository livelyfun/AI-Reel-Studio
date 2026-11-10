/* pipeline.c — resumable end-to-end reel generation. */
#include "as_generation.h"
#include "as_core.h"

#include <string.h>

struct ProgressMap {
    AsGenerationProgressFunc progress;
    gpointer user_data;
    gdouble base;
    gdouble span;
    const gchar *stage;
};

static void render_mapped(gdouble fraction, const gchar *stage, gpointer data)
{
    struct ProgressMap *map = data;
    if (map->progress)
        map->progress(map->stage ? map->stage : stage,
                      map->base + CLAMP(fraction, 0.0, 1.0) * map->span,
                      map->user_data);
}

static void generation_mapped(const gchar *stage, gdouble fraction,
                              gpointer data)
{
    struct ProgressMap *map = data;
    if (map->progress)
        map->progress(map->stage ? map->stage : stage,
                      map->base + CLAMP(fraction, 0.0, 1.0) * map->span,
                      map->user_data);
}

static void report(struct ProgressMap *map, const gchar *stage, gdouble fraction)
{
    if (map->progress)
        map->progress(stage, CLAMP(fraction, 0.0, 1.0), map->user_data);
}

static gchar *project_name_from(const AsReelRequest *request)
{
    if (!request || !request->prompt || !*request->prompt)
        return g_strdup("Untitled reel");
    gchar *copy = g_strndup(request->prompt, 70);
    gchar *name = as_sanitize_filename(copy);
    g_free(copy);
    return name;
}

static gboolean ensure_project(AsGenerationContext *ctx, const gchar *project_id,
                               const AsReelRequest *request, GError **error)
{
    AsProject *existing = as_project_load(ctx->database, project_id, FALSE, NULL);
    if (existing) {
        as_project_free(existing);
        return TRUE;
    }
    gchar *name = project_name_from(request);
    gboolean ok = as_project_create(ctx->database, project_id, name, request,
                                    error);
    g_free(name);
    return ok;
}

gboolean as_generation_update_thumbnail(AsGenerationContext *ctx, AsReel *reel,
                                        const gchar *project_dir, GError **error)
{
    g_return_val_if_fail(ctx && reel && project_dir, FALSE);
    AsScene *first = NULL;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        if (scene->image_path &&
            g_file_test(scene->image_path, G_FILE_TEST_EXISTS)) {
            first = scene;
            break;
        }
    }
    if (!first) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "A scene image is needed before a thumbnail can be made.");
        return FALSE;
    }
    gchar *dest = g_build_filename(project_dir, "thumbnail.jpg", NULL);
    gchar *result = as_media_thumbnail_from_image(first->image_path, dest,
                                                  ctx->ffmpeg, error);
    if (!result) {
        g_free(dest);
        return FALSE;
    }
    g_free(reel->thumbnail_path);
    reel->thumbnail_path = g_strdup(dest);
    if (ctx->database)
        as_project_set_thumbnail(ctx->database, reel->id, dest, NULL);
    g_free(result);
    g_free(dest);
    return TRUE;
}

gboolean as_generation_run(AsGenerationContext *ctx, AsReelRequest *request,
                           const gchar *project_id,
                           AsGenerationProgressFunc progress, gpointer user_data,
                           GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(ctx && request && project_id, FALSE);
    if (!as_request_validate(request, error))
        return FALSE;
    gchar *project_dir = as_paths_project_dir(ctx->paths, project_id);
    if (!as_ensure_dir(project_dir, error)) {
        g_free(project_dir);
        return FALSE;
    }
    if (!ensure_project(ctx, project_id, request, error)) {
        g_free(project_dir);
        return FALSE;
    }
    struct ProgressMap map = { progress, user_data, 0, 1, NULL };

    AsProject *project = as_project_load(ctx->database, project_id, TRUE, NULL);
    AsReel *reel = project ? project->reel : NULL;
    if (reel) {
        g_free(reel->id);
        reel->id = g_strdup(project_id);
        as_generation_finalize_captions(reel);
        report(&map, "Resuming saved project", 0.05);
    } else {
        report(&map, "Planning story", 0.0);
        if (!as_generation_plan(ctx, request, &reel, cancellable, error)) {
            as_project_free(project);
            g_free(project_dir);
            return FALSE;
        }
        g_free(reel->id);
        reel->id = g_strdup(project_id);
        report(&map, "Planning story", 0.10);
    }
    if (project)
        as_project_free(project);
    if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
        as_reel_free(reel);
        g_free(project_dir);
        return FALSE;
    }

    report(&map, "Writing metadata", 0.11);
    as_generation_attach_metadata(ctx, reel, cancellable, NULL);
    as_generation_finalize_captions(reel);
    if (!as_generation_save(ctx, project_id, reel, "planned", error)) {
        as_reel_free(reel);
        g_free(project_dir);
        return FALSE;
    }

    struct ProgressMap asset_map = { progress, user_data, 0.14, 0.40,
                                    "Collecting scene imagery" };
    if (!as_generation_assign_scene_assets(ctx, reel, project_dir, generation_mapped,
                                           &asset_map, cancellable, error)) {
        as_generation_save(ctx, project_id, reel, "failed", NULL);
        as_reel_free(reel);
        g_free(project_dir);
        return FALSE;
    }
    as_generation_save(ctx, project_id, reel, "assets-ready", NULL);

    report(&map, "Synthesizing narration", 0.55);
    if (!as_generation_synthesize_narration(ctx, reel, project_dir, FALSE,
                                            cancellable, error)) {
        as_generation_save(ctx, project_id, reel, "failed", NULL);
        as_reel_free(reel);
        g_free(project_dir);
        return FALSE;
    }
    as_generation_save(ctx, project_id, reel, "narration-ready", NULL);

    report(&map, "Synthesizing music", 0.66);
    if (!as_generation_synthesize_music(ctx, reel, project_dir, error)) {
        as_generation_save(ctx, project_id, reel, "failed", NULL);
        as_reel_free(reel);
        g_free(project_dir);
        return FALSE;
    }

    report(&map, "Creating thumbnail", 0.70);
    if (!as_generation_update_thumbnail(ctx, reel, project_dir, NULL)) {
        AS_WARN("pipeline", "Thumbnail generation failed; continuing without one.");
    }
    as_generation_save(ctx, project_id, reel, "media-ready", NULL);

    report(&map, "Rendering preview", 0.74);
    AsRenderOptions options;
    as_render_options_for_ratio(reel->request.aspect_ratio, TRUE, &options);
    options.music_gain = ctx->config->music_gain;
    options.narration_gain = ctx->config->narration_gain;
    gchar *preview = g_build_filename(project_dir, "preview.mp4", NULL);
    struct ProgressMap render_map = { progress, user_data, 0.74, 0.25,
                                      "Rendering preview" };
    AsRenderResult result;
    memset(&result, 0, sizeof(result));
    gboolean ok = as_video_render(reel, project_dir, ctx->ffmpeg, &options,
                                  preview, render_mapped, &render_map, cancellable,
                                  &result, error);
    if (ok) {
        as_render_result_clear(&result);
        if (!as_generation_save(ctx, project_id, reel, "ready", error))
            ok = FALSE;
    } else {
        as_generation_save(ctx, project_id, reel, "failed", NULL);
    }
    g_free(preview);
    as_reel_free(reel);
    g_free(project_dir);
    if (ok && progress)
        progress("Ready", 1.0, user_data);
    return ok;
}
