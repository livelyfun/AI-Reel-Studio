/* context.c — AsAppContext initialization and lifecycle. */
#define _GNU_SOURCE
#include "as_app.h"
#include "as_core.h"
#include "as_storage.h"
#include "as_security.h"
#include "as_provider.h"
#include "as_job.h"
#include "as_media.h"
#include "as_generation.h"

#include <glib/gstdio.h>

AsAppContext *as_app_context_new(GError **error)
{
    AsAppContext *ctx = g_new0(AsAppContext, 1);

    ctx->paths = as_paths_new(error);
    if (!ctx->paths) {
        as_app_context_free(ctx);
        return NULL;
    }

    /* Initialize logger in log dir */
    as_log_init(ctx->paths->logs_dir, NULL);

    ctx->config = as_config_new();
    if (g_file_test(ctx->paths->settings_path, G_FILE_TEST_EXISTS)) {
        as_config_load(ctx->config, ctx->paths->settings_path, NULL);
    }

    ctx->db = as_database_open(ctx->paths->db_path, error);
    if (!ctx->db) {
        as_app_context_free(ctx);
        return NULL;
    }

    ctx->credentials = as_credential_manager_new();

    ctx->providers = as_provider_registry_new();
    if (!as_provider_registry_reload(ctx->providers, ctx->db, error)) {
        as_app_context_free(ctx);
        return NULL;
    }

    ctx->ffmpeg = as_ffmpeg_runner_new(ctx->config->ffmpeg_path,
                                       ctx->config->ffprobe_path);

    ctx->jobs = as_job_manager_new(ctx);

    return ctx;
}

void as_app_context_free(AsAppContext *ctx)
{
    if (!ctx)
        return;

    if (ctx->jobs)
        g_object_unref(ctx->jobs);

    if (ctx->ffmpeg)
        as_ffmpeg_runner_free(ctx->ffmpeg);

    if (ctx->providers)
        as_provider_registry_free(ctx->providers);

    if (ctx->credentials)
        as_credential_manager_free(ctx->credentials);

    if (ctx->db)
        as_database_close(ctx->db);

    if (ctx->config) {
        /* Save settings on exit */
        if (ctx->paths && ctx->paths->settings_path)
            as_config_save(ctx->config, ctx->paths->settings_path, NULL);
        as_config_free(ctx->config);
    }

    if (ctx->paths)
        as_paths_free(ctx->paths);

    as_redact_clear_secrets();
    g_free(ctx);
}

gboolean as_app_context_reload_providers(AsAppContext *ctx, GError **error)
{
    g_return_val_if_fail(ctx && ctx->providers && ctx->db, FALSE);
    return as_provider_registry_reload(ctx->providers, ctx->db, error);
}

AsGenerationContext *as_app_context_generation(AsAppContext *ctx)
{
    g_return_val_if_fail(ctx != NULL, NULL);
    AsGenerationContext *gen = g_new0(AsGenerationContext, 1);
    gen->providers = ctx->providers;
    gen->credentials = ctx->credentials;
    gen->database = ctx->db;
    gen->config = ctx->config;
    gen->paths = ctx->paths;
    gen->ffmpeg = ctx->ffmpeg;
    return gen;
}

void as_generation_context_free(AsGenerationContext *gen)
{
    g_free(gen);
}

gboolean as_app_context_ensure_project_dir(AsAppContext *ctx, const gchar *project_id,
                                            gchar **path_out, GError **error)
{
    g_return_val_if_fail(ctx && ctx->paths && project_id, FALSE);
    gchar *dir = as_paths_project_dir(ctx->paths, project_id);
    if (!as_ensure_dir(dir, error)) {
        g_free(dir);
        return FALSE;
    }
    if (path_out)
        *path_out = dir;
    else
        g_free(dir);
    return TRUE;
}

gchar *as_app_context_project_dir(AsAppContext *ctx, const gchar *project_id)
{
    g_return_val_if_fail(ctx && ctx->paths && project_id, NULL);
    return as_paths_project_dir(ctx->paths, project_id);
}

