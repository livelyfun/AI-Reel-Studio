/* as_generation.h — AI generation pipeline independent of any provider. */
#pragma once

#include <glib.h>
#include <gio/gio.h>
#include "as_core.h"
#include "as_types.h"
#include "as_provider.h"
#include "as_storage.h"
#include "as_security.h"
#include "as_media.h"

typedef struct _AsGenerationContext AsGenerationContext;
struct _AsGenerationContext {
    AsProviderRegistry *providers;
    AsCredentialManager *credentials;
    AsDatabase *database;
    AsConfig *config;
    AsPaths *paths;
    AsFfmpegRunner *ffmpeg;
};

typedef void (*AsGenerationProgressFunc)(const gchar *stage, gdouble fraction,
                                         gpointer user_data);

/* Runs the complete pipeline for an existing draft project. Existing completed
 * stages are reused, so this doubles as "resume" after a failure. */
gboolean as_generation_run(AsGenerationContext *ctx, AsReelRequest *request,
                           const gchar *project_id,
                           AsGenerationProgressFunc progress, gpointer user_data,
                           GCancellable *cancellable, GError **error);

gboolean as_generation_plan(AsGenerationContext *ctx,
                            const AsReelRequest *request, AsReel **out,
                            GCancellable *cancellable, GError **error);

gboolean as_generation_assign_scene_assets(AsGenerationContext *ctx, AsReel *reel,
                                           const gchar *project_dir,
                                           AsGenerationProgressFunc progress,
                                           gpointer user_data,
                                           GCancellable *cancellable,
                                           GError **error);

gboolean as_generation_synthesize_narration(AsGenerationContext *ctx, AsReel *reel,
                                            const gchar *project_dir,
                                            gboolean force,
                                            GCancellable *cancellable,
                                            GError **error);

gboolean as_generation_synthesize_music(AsGenerationContext *ctx, AsReel *reel,
                                        const gchar *project_dir,
                                        GError **error);

gboolean as_generation_regenerate_scene_asset(AsGenerationContext *ctx,
                                              AsReel *reel,
                                              const gchar *project_dir,
                                              gint scene_id,
                                              GCancellable *cancellable,
                                              GError **error);

/* Replaces narration audio using the current speech route. */
gboolean as_generation_regenerate_narration(AsGenerationContext *ctx,
                                            AsReel *reel,
                                            const gchar *project_dir,
                                            GCancellable *cancellable,
                                            GError **error);

/* Derives caption metadata and validates/estimates timings. */
void as_generation_finalize_captions(AsReel *reel);

/* Validates and normalizes provider JSON into a reel. Exposed for tests. */
gboolean as_generation_parse_plan_json(const gchar *json,
                                       const AsReelRequest *request,
                                       AsReel *reel, GError **error);

gboolean as_generation_save(AsGenerationContext *ctx, const gchar *project_id,
                            AsReel *reel, const gchar *status, GError **error);

/* Derives metadata through the metadata route, then fills any gaps from the
 * plan. Never fails the pipeline when the route is unconfigured. */
gboolean as_generation_attach_metadata(AsGenerationContext *ctx, AsReel *reel,
                                       GCancellable *cancellable, GError **error);

gboolean as_generation_update_thumbnail(AsGenerationContext *ctx, AsReel *reel,
                                        const gchar *project_dir, GError **error);
