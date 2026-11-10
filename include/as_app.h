/* as_app.h — process-wide application context. */
#pragma once

#include <glib.h>
#include "as_core.h"
#include "as_storage.h"
#include "as_security.h"
#include "as_provider.h"
#include "as_job.h"
#include "as_media.h"

struct _AsAppContext {
    AsConfig *config;
    AsPaths *paths;
    AsDatabase *db;
    AsCredentialManager *credentials;
    AsProviderRegistry *providers;
    AsJobManager *jobs;
    AsFfmpegRunner *ffmpeg;
};

typedef struct _AsGenerationContext AsGenerationContext;

AsAppContext *as_app_context_new(GError **error);
void as_app_context_free(AsAppContext *ctx);
gboolean as_app_context_reload_providers(AsAppContext *ctx, GError **error);
AsGenerationContext *as_app_context_generation(AsAppContext *ctx);
void as_generation_context_free(AsGenerationContext *gen);
gboolean as_app_context_ensure_project_dir(AsAppContext *ctx, const gchar *project_id,
                                           gchar **path_out, GError **error);
gchar *as_app_context_project_dir(AsAppContext *ctx, const gchar *project_id);
