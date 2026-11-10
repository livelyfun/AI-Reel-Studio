/* as_job.h — background job manager (worker threads, progress, cancel). */
#pragma once

#include <glib-object.h>
#include <gio/gio.h>
#include "as_types.h"

typedef struct _AsAppContext AsAppContext;

typedef enum {
    AS_JOB_PENDING = 0,
    AS_JOB_QUEUED,
    AS_JOB_RUNNING,
    AS_JOB_RETRYING,
    AS_JOB_COMPLETED,
    AS_JOB_FAILED,
    AS_JOB_CANCELLED,
    AS_JOB_INTERRUPTED
} AsJobState;

const gchar *as_job_state_to_string(AsJobState state);
AsJobState as_job_state_from_string(const gchar *s);

typedef struct {
    gchar *id;
    gchar *project_id;
    gchar *type;
    gchar *stage;
    gchar *error;
    gchar *provider_id;
    gchar *model_id;
    gdouble progress;
    AsJobState state;
    gboolean cancellable;
} AsJobInfo;

#define AS_TYPE_JOB_MANAGER (as_job_manager_get_type())
G_DECLARE_FINAL_TYPE(AsJobManager, as_job_manager, AS, JOB_MANAGER, GObject)

/* Signals:
 *  "progress" (const gchar *job_id, const gchar *stage, gdouble fraction)
 *  "state"    (const gchar *job_id, const gchar *state, const gchar *error)
 *  "finished" (const gchar *job_id, gboolean success, const gchar *error)
 */
AsJobManager *as_job_manager_new(AsAppContext *ctx);

gchar *as_job_manager_start_generation(AsAppContext *ctx, const AsReelRequest *request,
                                       const gchar *project_id, gboolean force_narration,
                                       GError **error);
gchar *as_job_manager_start_scene_image(AsAppContext *ctx, const gchar *project_id,
                                        gint scene_id, GError **error);
gchar *as_job_manager_start_narration(AsAppContext *ctx, const gchar *project_id,
                                      gboolean force, GError **error);
gchar *as_job_manager_start_export(AsAppContext *ctx, const gchar *project_id,
                                   const gchar *output_path, gboolean preview,
                                   GError **error);
gboolean as_job_manager_cancel(AsJobManager *manager, const gchar *job_id);
guint as_job_manager_active_count(AsJobManager *manager);
gboolean as_job_manager_any_active(AsJobManager *manager);
AsJobInfo *as_job_manager_get(AsJobManager *manager, const gchar *job_id);
void as_job_info_free(AsJobInfo *info);
/* Blocks until running/pending jobs finish; used by tests and controlled exit. */
void as_job_manager_wait_idle(AsJobManager *manager);
