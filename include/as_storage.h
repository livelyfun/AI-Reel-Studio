/* as_storage.h — SQLite persistence (projects, providers, routes, jobs). */
#pragma once

#include <glib.h>
#include "as_types.h"
#include "as_provider.h"

typedef struct _AsDatabase AsDatabase;

typedef struct {
    gchar *id;
    gchar *name;
    gchar *status;
    gchar *updated_at;
    gdouble total_duration;
    gchar *thumbnail_path;
    gboolean has_reel;
} AsProjectSummary;

typedef struct {
    gchar *id;
    gchar *name;
    gchar *status;
    gchar *prompt;
    gchar *voice_style;
    gchar *visual_style;
    gint duration;
    AsAspectRatio aspect_ratio;
    gchar *thumbnail_path;
    gchar *export_path;
    gdouble total_duration;
    gchar *created_at;
    gchar *updated_at;
    AsReel *reel;               /* NULL unless loaded */
} AsProject;

typedef struct {
    gchar *id;
    gchar *project_id;
    gchar *type;
    gchar *state;
    gchar *stage;
    gdouble progress;
    gchar *error;
    gchar *provider_id;
    gchar *model_id;
    gchar *started_at;
    gchar *finished_at;
} AsJobRecord;

typedef struct {
    gchar *id;
    gchar *project_id;
    gint scene_id;
    gchar *kind;
    gchar *local_path;
    gchar *source;
    gchar *source_url;
    gchar *author;
    gchar *license_name;
    gchar *license_url;
    gchar *attribution;
    gchar *retrieved_at;
    gchar *sha256;
} AsAssetRecord;

typedef struct {
    gchar *id;
    gchar *project_id;
    gchar *path;
    gchar *format;
    gint width;
    gint height;
    gdouble duration;
    gchar *video_codec;
    gchar *audio_codec;
    gint64 file_size;
    gboolean verified;
    gchar *created_at;
} AsExportRecord;

AsDatabase *as_database_open(const gchar *path, GError **error);
void as_database_close(AsDatabase *db);
gpointer as_database_handle(AsDatabase *db);
gboolean as_database_backup(AsDatabase *db, const gchar *backup_path, GError **error);
gboolean as_database_integrity_check(AsDatabase *db, GError **error);

/* projects */
gboolean as_project_create(AsDatabase *db, const gchar *id, const gchar *name,
                           const AsReelRequest *request, GError **error);
gboolean as_project_save_reel(AsDatabase *db, const gchar *id, const AsReel *reel,
                              const gchar *status, GError **error);
gboolean as_project_rename(AsDatabase *db, const gchar *id, const gchar *name,
                           GError **error);
gboolean as_project_delete(AsDatabase *db, const gchar *id, GError **error);
gchar *as_project_duplicate(AsDatabase *db, const gchar *id, GError **error);
AsProject *as_project_load(AsDatabase *db, const gchar *id, gboolean with_reel,
                           GError **error);
GPtrArray *as_project_list(AsDatabase *db, const gchar *search,
                           GError **error); /* AsProjectSummary* */
gboolean as_project_set_export(AsDatabase *db, const gchar *id, const gchar *path,
                               GError **error);
gboolean as_project_set_thumbnail(AsDatabase *db, const gchar *id,
                                  const gchar *path, GError **error);
gboolean as_project_set_status(AsDatabase *db, const gchar *id, const gchar *status,
                               const gchar *error_message, GError **error);
guint as_project_count(AsDatabase *db, const gchar *status);
gboolean as_project_has_reel(AsDatabase *db, const gchar *id);

void as_project_summary_free(AsProjectSummary *summary);
void as_project_free(AsProject *project);

/* providers and routes */
gboolean as_provider_store_list(AsDatabase *db, GPtrArray *out /* AsProviderConfig* */,
                                GError **error);
gboolean as_provider_store_save(AsDatabase *db, const AsProviderConfig *cfg,
                                GError **error);
gboolean as_provider_store_delete(AsDatabase *db, const gchar *id, GError **error);
gboolean as_provider_store_load_routes(AsDatabase *db, GHashTable *out, GError **error);
gboolean as_provider_store_save_route(AsDatabase *db, AsRouteCapability cap,
                                      const gchar *provider_id,
                                      const gchar *model_id, GError **error);
gboolean as_provider_store_clear_route(AsDatabase *db, AsRouteCapability cap,
                                       GError **error);
gint as_provider_count(AsDatabase *db, gboolean enabled_only);

/* jobs */
gboolean as_job_store_insert(AsDatabase *db, const AsJobRecord *record, GError **error);
gboolean as_job_store_update(AsDatabase *db, const gchar *id, const gchar *state,
                             const gchar *stage, gdouble progress,
                             const gchar *error_message, GError **error);
GPtrArray *as_job_store_recent(AsDatabase *db, gint limit, GError **error);
gint as_job_store_mark_interrupted(AsDatabase *db, GError **error);
void as_job_record_free(AsJobRecord *record);

/* assets and exports */
gboolean as_asset_store_insert(AsDatabase *db, const AsAssetRecord *record,
                               GError **error);
GPtrArray *as_asset_store_for_project(AsDatabase *db, const gchar *project_id,
                                      GError **error);
void as_asset_record_free(AsAssetRecord *record);

gboolean as_export_store_insert(AsDatabase *db, const AsExportRecord *record,
                                GError **error);
GPtrArray *as_export_store_for_project(AsDatabase *db, const gchar *project_id,
                                       GError **error);
void as_export_record_free(AsExportRecord *record);
gint as_export_count(AsDatabase *db);
