/* assets.c — scene imagery via the configured image route or free search. */
#include "as_generation.h"
#include "as_core.h"

#include <string.h>

#include <glib/gstdio.h>

static gchar *image_extension_from_url(const gchar *url)
{
    if (!url)
        return g_strdup("jpg");
    gchar *clean = g_strdup(url);
    gchar *query = strchr(clean, '?');
    if (query)
        *query = '\0';
    const gchar *dot = strrchr(clean, '.');
    gchar *ext = NULL;
    if (dot) {
        const gchar *candidate = dot + 1;
        if (g_ascii_strcasecmp(candidate, "jpg") == 0 ||
            g_ascii_strcasecmp(candidate, "jpeg") == 0)
            ext = g_strdup("jpg");
        else if (g_ascii_strcasecmp(candidate, "png") == 0)
            ext = g_strdup("png");
        else if (g_ascii_strcasecmp(candidate, "webp") == 0)
            ext = g_strdup("webp");
    }
    g_free(clean);
    return ext ? ext : g_strdup("jpg");
}

static gchar *build_image_prompt(const AsReel *reel, const AsScene *scene)
{
    GString *prompt = g_string_new(NULL);
    if (scene->visual_prompt)
        g_string_append(prompt, scene->visual_prompt);
    const AsVisualBible *vb = &reel->plan.visual_bible;
    const gchar *parts[] = { vb->visual_style, vb->lighting, vb->environment,
                             vb->camera_style, vb->overall_atmosphere, NULL };
    for (gint i = 0; parts[i]; i++) {
        if (!parts[i] || !*parts[i])
            continue;
        if (prompt->len)
            g_string_append(prompt, ", ");
        g_string_append(prompt, parts[i]);
    }
    if (reel->request.visual_style) {
        if (prompt->len)
            g_string_append(prompt, ", ");
        g_string_append_printf(prompt, "%s style", reel->request.visual_style);
    }
    g_string_append(prompt, ". Cinematic composition, vertical short-form "
                            "framing, no text, no captions, no watermark.");
    return g_string_free(prompt, FALSE);
}

static const gchar *image_size_for(AsAspectRatio ratio)
{
    switch (ratio) {
    case AS_ASPECT_16_9: return "1792x1024";
    case AS_ASPECT_1_1: return "1024x1024";
    case AS_ASPECT_9_16:
    default: return "1024x1792";
    }
}

static void record_asset(AsGenerationContext *ctx, const AsReel *reel,
                         const AsScene *scene)
{
    if (!ctx->database || !reel->id)
        return;
    AsAssetRecord record;
    memset(&record, 0, sizeof(record));
    record.id = as_uuid_random();
    record.project_id = (gchar *) reel->id;
    record.scene_id = scene->scene_id;
    record.kind = (gchar *) "image";
    record.local_path = scene->image_path;
    record.source_url = scene->image_source_url;
    record.author = scene->image_author;
    record.license_name = scene->image_license;
    record.license_url = scene->image_license_url;
    record.attribution = scene->image_attribution;
    record.source = scene->image_source_url ? (gchar *) "search"
                                            : (gchar *) "generated";
    GError *local = NULL;
    if (!as_asset_store_insert(ctx->database, &record, &local)) {
        AS_WARN("assets", "Could not record asset provenance: %s",
                local ? local->message : "unknown error");
        g_clear_error(&local);
    }
    g_free(record.id);
}

static gboolean generate_scene_image(AsGenerationContext *ctx, AsReel *reel,
                                     AsScene *scene, const gchar *scene_dir,
                                     AsProviderConfig *cfg, const gchar *model,
                                     GCancellable *cancellable, GError **error)
{
    gchar *api_key = as_credential_manager_get(ctx->credentials, cfg->id, error);
    if (!api_key)
        return FALSE;
    if (*api_key)
        as_redact_register_secret(api_key);
    gchar *prompt = build_image_prompt(reel, scene);
    AsImageResult image;
    memset(&image, 0, sizeof(image));
    gboolean ok = as_provider_generate_image(cfg, api_key, model, prompt,
                                             image_size_for(
                                                 reel->request.aspect_ratio),
                                             &image, cancellable, error);
    as_secure_clear(api_key);
    g_free(prompt);
    if (!ok)
        return FALSE;
    const gchar *ext = (image.extension && *image.extension) ? image.extension
                                                             : "png";
    gchar *path = g_strdup_printf("%s%cscene-%02d.%s", scene_dir,
                                  G_DIR_SEPARATOR, scene->scene_id, ext);
    gsize length = 0;
    const gchar *data = g_bytes_get_data(image.data, &length);
    ok = g_file_set_contents(path, data, (gssize) length, error);
    as_image_result_clear(&image);
    if (!ok) {
        g_free(path);
        return FALSE;
    }
    g_chmod(path, 0644);
    AsMediaInfo info;
    memset(&info, 0, sizeof(info));
    if (!as_media_probe(ctx->ffmpeg, path, &info, error) || !info.has_video) {
        as_media_info_clear(&info);
        g_unlink(path);
        g_free(path);
        if (error && !*error)
            g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                        "The image provider returned an unreadable image.");
        return FALSE;
    }
    as_media_info_clear(&info);

    g_free(scene->image_path);
    scene->image_path = path;
    g_free(scene->image_source_url);
    scene->image_source_url = NULL;
    g_free(scene->image_author);
    scene->image_author = g_strdup(cfg->name);
    g_free(scene->image_license);
    scene->image_license = g_strdup("AI-generated image");
    g_free(scene->image_license_url);
    scene->image_license_url = NULL;
    g_free(scene->image_attribution);
    scene->image_attribution = g_strdup_printf(
        "AI-generated by %s using model %s for AI Reel Studio.", cfg->name,
        model);
    record_asset(ctx, reel, scene);
    return TRUE;
}

static gboolean search_scene_image(AsGenerationContext *ctx, AsReel *reel,
                                   AsScene *scene, const gchar *scene_dir,
                                   GCancellable *cancellable, GError **error)
{
    gchar *query = as_asset_build_query(scene->visual_prompt, scene->keywords,
                                        reel->plan.topic);
    GPtrArray *results = as_asset_search(query, ctx->config->unsplash_access_key,
                                         8, cancellable, error);
    g_free(query);
    if (!results)
        return FALSE;

    gboolean ok = FALSE;
    for (guint i = 0; i < results->len && !ok; i++) {
        AsAssetResult *result = g_ptr_array_index(results, i);
        gchar *ext = image_extension_from_url(result->image_url);
        gchar *dest = g_strdup_printf("%s%cscene-%02d-%u.%s", scene_dir,
                                      G_DIR_SEPARATOR, scene->scene_id, i + 1,
                                      ext);
        g_free(ext);
        GError *local = NULL;
        if (as_asset_download(result, dest, cancellable, &local)) {
            g_free(scene->image_path);
            scene->image_path = dest;
            g_free(scene->image_source_url);
            scene->image_source_url = g_strdup(result->image_url);
            g_free(scene->image_author);
            scene->image_author = g_strdup(result->author);
            g_free(scene->image_license);
            scene->image_license = g_strdup(result->license_name);
            g_free(scene->image_license_url);
            scene->image_license_url = g_strdup(result->license_url);
            g_free(scene->image_attribution);
            scene->image_attribution = g_strdup(result->attribution);
            record_asset(ctx, reel, scene);
            ok = TRUE;
            g_clear_error(&local);
        } else {
            g_unlink(dest);
            g_free(dest);
            g_clear_error(&local);
        }
    }
    g_ptr_array_unref(results);
    if (!ok) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "No image could be downloaded for scene %d.",
                    scene->scene_id);
        return FALSE;
    }
    return TRUE;
}

static gboolean assign_one(AsGenerationContext *ctx, AsReel *reel,
                           AsScene *scene, const gchar *scene_dir,
                           AsProviderConfig *image_cfg, const gchar *image_model,
                           GCancellable *cancellable, GError **error)
{
    if (scene->image_path &&
        g_file_test(scene->image_path, G_FILE_TEST_EXISTS))
        return TRUE;
    if (image_cfg)
        return generate_scene_image(ctx, reel, scene, scene_dir, image_cfg,
                                    image_model, cancellable, error);
    return search_scene_image(ctx, reel, scene, scene_dir, cancellable, error);
}

gboolean as_generation_assign_scene_assets(AsGenerationContext *ctx, AsReel *reel,
                                           const gchar *project_dir,
                                           AsGenerationProgressFunc progress,
                                           gpointer user_data,
                                           GCancellable *cancellable,
                                           GError **error)
{
    g_return_val_if_fail(ctx && reel && project_dir, FALSE);
    gchar *scene_dir = g_build_filename(project_dir, "scenes", NULL);
    if (!as_ensure_dir(scene_dir, error)) {
        g_free(scene_dir);
        return FALSE;
    }
    const gchar *image_model = NULL;
    AsProviderConfig *image_cfg = as_provider_registry_resolve(
        ctx->providers, AS_ROUTE_IMAGE, &image_model, NULL);

    gboolean ok = TRUE;
    for (guint i = 0; i < reel->scenes->len && ok; i++) {
        if (g_cancellable_set_error_if_cancelled(cancellable, error)) {
            ok = FALSE;
            break;
        }
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        if (progress)
            progress(image_cfg ? "Generating scene imagery"
                               : "Finding scene imagery",
                      (gdouble) i / reel->scenes->len, user_data);
        ok = assign_one(ctx, reel, scene, scene_dir, image_cfg, image_model,
                        cancellable, error);
        if (ok && progress)
            progress(image_cfg ? "Generating scene imagery"
                               : "Finding scene imagery",
                      (gdouble) (i + 1) / reel->scenes->len, user_data);
    }
    g_free(scene_dir);
    return ok;
}

gboolean as_generation_regenerate_scene_asset(AsGenerationContext *ctx,
                                              AsReel *reel,
                                              const gchar *project_dir,
                                              gint scene_id,
                                              GCancellable *cancellable,
                                              GError **error)
{
    g_return_val_if_fail(ctx && reel && project_dir, FALSE);
    AsScene *scene = NULL;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *candidate = g_ptr_array_index(reel->scenes, i);
        if (candidate->scene_id == scene_id) {
            scene = candidate;
            break;
        }
    }
    if (!scene) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND,
                    "Scene %d does not exist.", scene_id);
        return FALSE;
    }
    gchar *scene_dir = g_build_filename(project_dir, "scenes", NULL);
    if (!as_ensure_dir(scene_dir, error)) {
        g_free(scene_dir);
        return FALSE;
    }
    if (scene->image_path) {
        gchar *old = g_strdup(scene->image_path);
        g_free(scene->image_path);
        scene->image_path = NULL;
        g_unlink(old);
        g_free(old);
    }
    const gchar *model = NULL;
    AsProviderConfig *cfg = as_provider_registry_resolve(
        ctx->providers, AS_ROUTE_IMAGE, &model, NULL);
    gboolean ok = assign_one(ctx, reel, scene, scene_dir, cfg, model,
                             cancellable, error);
    g_free(scene_dir);
    return ok;
}
