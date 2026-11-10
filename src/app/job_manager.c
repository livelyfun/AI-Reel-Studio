/* job_manager.c — background job lifecycle, concurrency, progress and cancellation. */
#define _GNU_SOURCE
#include "as_job.h"
#include "as_app.h"
#include "as_core.h"
#include "as_storage.h"
#include "as_generation.h"
#include "as_media.h"

#include <string.h>

struct _AsJobManager {
    GObject parent_instance;
    AsAppContext *ctx;
    GMutex lock;
    GHashTable *active_jobs;   /* id -> ActiveJob* */
};

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
    GCancellable *cancellable;
    AsJobManager *manager;
} ActiveJob;

enum {
    SIG_PROGRESS,
    SIG_STATE,
    SIG_FINISHED,
    N_SIGNALS
};

static guint signals[N_SIGNALS] = { 0 };

G_DEFINE_TYPE(AsJobManager, as_job_manager, G_TYPE_OBJECT)

const gchar *as_job_state_to_string(AsJobState state)
{
    switch (state) {
    case AS_JOB_PENDING: return "pending";
    case AS_JOB_QUEUED: return "queued";
    case AS_JOB_RUNNING: return "running";
    case AS_JOB_RETRYING: return "retrying";
    case AS_JOB_COMPLETED: return "completed";
    case AS_JOB_FAILED: return "failed";
    case AS_JOB_CANCELLED: return "cancelled";
    case AS_JOB_INTERRUPTED: return "interrupted";
    default: return "unknown";
    }
}

AsJobState as_job_state_from_string(const gchar *s)
{
    if (!s) return AS_JOB_PENDING;
    if (g_strcmp0(s, "queued") == 0) return AS_JOB_QUEUED;
    if (g_strcmp0(s, "running") == 0) return AS_JOB_RUNNING;
    if (g_strcmp0(s, "retrying") == 0) return AS_JOB_RETRYING;
    if (g_strcmp0(s, "completed") == 0) return AS_JOB_COMPLETED;
    if (g_strcmp0(s, "failed") == 0) return AS_JOB_FAILED;
    if (g_strcmp0(s, "cancelled") == 0) return AS_JOB_CANCELLED;
    if (g_strcmp0(s, "interrupted") == 0) return AS_JOB_INTERRUPTED;
    return AS_JOB_PENDING;
}

static void active_job_free(ActiveJob *job)
{
    if (!job) return;
    g_free(job->id);
    g_free(job->project_id);
    g_free(job->type);
    g_free(job->stage);
    g_free(job->error);
    g_free(job->provider_id);
    g_free(job->model_id);
    if (job->cancellable)
        g_object_unref(job->cancellable);
    g_free(job);
}

static void as_job_manager_dispose(GObject *object)
{
    AsJobManager *self = AS_JOB_MANAGER(object);
    g_mutex_lock(&self->lock);
    if (self->active_jobs) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, self->active_jobs);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            ActiveJob *job = value;
            if (job->cancellable)
                g_cancellable_cancel(job->cancellable);
        }
        g_hash_table_destroy(self->active_jobs);
        self->active_jobs = NULL;
    }
    g_mutex_unlock(&self->lock);
    G_OBJECT_CLASS(as_job_manager_parent_class)->dispose(object);
}

static void as_job_manager_finalize(GObject *object)
{
    AsJobManager *self = AS_JOB_MANAGER(object);
    g_mutex_clear(&self->lock);
    G_OBJECT_CLASS(as_job_manager_parent_class)->finalize(object);
}

static void as_job_manager_class_init(AsJobManagerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = as_job_manager_dispose;
    object_class->finalize = as_job_manager_finalize;

    signals[SIG_PROGRESS] = g_signal_new("progress",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL,
        G_TYPE_NONE, 3,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_DOUBLE);

    signals[SIG_STATE] = g_signal_new("state",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL,
        G_TYPE_NONE, 3,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);

    signals[SIG_FINISHED] = g_signal_new("finished",
        G_TYPE_FROM_CLASS(klass),
        G_SIGNAL_RUN_LAST,
        0, NULL, NULL, NULL,
        G_TYPE_NONE, 3,
        G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);
}

static void as_job_manager_init(AsJobManager *self)
{
    g_mutex_init(&self->lock);
    self->active_jobs = g_hash_table_new_full(g_str_hash, g_str_equal,
                                              g_free, (GDestroyNotify) active_job_free);
}

AsJobManager *as_job_manager_new(AsAppContext *ctx)
{
    AsJobManager *manager = g_object_new(AS_TYPE_JOB_MANAGER, NULL);
    manager->ctx = ctx;
    return manager;
}

typedef struct {
    AsJobManager *manager;
    gchar *job_id;
    gchar *stage;
    gdouble fraction;
} ProgressPayload;

static gboolean on_progress_idle(gpointer data)
{
    ProgressPayload *p = data;
    g_signal_emit(p->manager, signals[SIG_PROGRESS], 0, p->job_id, p->stage, p->fraction);
    g_object_unref(p->manager);
    g_free(p->job_id);
    g_free(p->stage);
    g_free(p);
    return G_SOURCE_REMOVE;
}

static void emit_progress(AsJobManager *mgr, const gchar *job_id, const gchar *stage, gdouble fraction)
{
    ProgressPayload *p = g_new0(ProgressPayload, 1);
    p->manager = g_object_ref(mgr);
    p->job_id = g_strdup(job_id);
    p->stage = g_strdup(stage ? stage : "");
    p->fraction = fraction;
    g_idle_add(on_progress_idle, p);
}

typedef struct {
    AsJobManager *manager;
    gchar *job_id;
    gboolean success;
    gchar *error;
} FinishedPayload;

static gboolean on_finished_idle(gpointer data)
{
    FinishedPayload *p = data;
    g_signal_emit(p->manager, signals[SIG_FINISHED], 0, p->job_id, p->success, p->error);
    g_object_unref(p->manager);
    g_free(p->job_id);
    g_free(p->error);
    g_free(p);
    return G_SOURCE_REMOVE;
}

static void emit_finished(AsJobManager *mgr, const gchar *job_id, gboolean success, const gchar *error)
{
    FinishedPayload *p = g_new0(FinishedPayload, 1);
    p->manager = g_object_ref(mgr);
    p->job_id = g_strdup(job_id);
    p->success = success;
    p->error = g_strdup(error);
    g_idle_add(on_finished_idle, p);
}

static void record_job_start(AsJobManager *mgr, ActiveJob *job)
{
    if (!mgr->ctx || !mgr->ctx->db) return;
    AsJobRecord rec;
    memset(&rec, 0, sizeof(rec));
    rec.id = job->id;
    rec.project_id = job->project_id;
    rec.type = job->type;
    rec.state = (gchar *) as_job_state_to_string(job->state);
    rec.stage = job->stage;
    rec.progress = job->progress;
    rec.provider_id = job->provider_id;
    rec.model_id = job->model_id;
    as_job_store_insert(mgr->ctx->db, &rec, NULL);
}

static void record_job_update(AsJobManager *mgr, ActiveJob *job)
{
    if (!mgr->ctx || !mgr->ctx->db) return;
    as_job_store_update(mgr->ctx->db, job->id,
                        as_job_state_to_string(job->state),
                        job->stage, job->progress, job->error, NULL);
}

/* Worker for Generation Pipeline */
typedef struct {
    AsJobManager *manager;
    gchar *job_id;
    gchar *project_id;
    AsReelRequest request;
    gboolean force_narration;
    GCancellable *cancellable;
} GenWorkerData;

static void gen_progress_cb(const gchar *stage, gdouble fraction, gpointer user_data)
{
    GenWorkerData *wd = user_data;
    g_mutex_lock(&wd->manager->lock);
    ActiveJob *job = g_hash_table_lookup(wd->manager->active_jobs, wd->job_id);
    if (job) {
        g_free(job->stage);
        job->stage = g_strdup(stage);
        job->progress = fraction;
        record_job_update(wd->manager, job);
    }
    g_mutex_unlock(&wd->manager->lock);
    emit_progress(wd->manager, wd->job_id, stage, fraction);
}

static gpointer generation_worker(gpointer data)
{
    GenWorkerData *wd = data;
    AsAppContext *ctx = wd->manager->ctx;
    AsGenerationContext *gen_ctx = as_app_context_generation(ctx);

    GError *error = NULL;
    gboolean ok = as_generation_run(gen_ctx, &wd->request, wd->project_id,
                                    gen_progress_cb, wd,
                                    wd->cancellable, &error);

    g_mutex_lock(&wd->manager->lock);
    ActiveJob *job = g_hash_table_lookup(wd->manager->active_jobs, wd->job_id);
    if (job) {
        if (g_cancellable_is_cancelled(wd->cancellable)) {
            job->state = AS_JOB_CANCELLED;
            g_free(job->error);
            job->error = g_strdup("Job cancelled by user");
        } else if (ok) {
            job->state = AS_JOB_COMPLETED;
            job->progress = 1.0;
        } else {
            job->state = AS_JOB_FAILED;
            g_free(job->error);
            job->error = error ? g_strdup(error->message) : g_strdup("Generation failed");
        }
        record_job_update(wd->manager, job);
    }
    g_mutex_unlock(&wd->manager->lock);

    emit_finished(wd->manager, wd->job_id, ok, error ? error->message : NULL);

    if (error)
        g_error_free(error);

    as_generation_context_free(gen_ctx);
    as_request_clear(&wd->request);
    g_object_unref(wd->cancellable);
    g_object_unref(wd->manager);
    g_free(wd->job_id);
    g_free(wd->project_id);
    g_free(wd);
    return NULL;
}

gchar *as_job_manager_start_generation(AsAppContext *ctx, const AsReelRequest *request,
                                       const gchar *project_id, gboolean force_narration,
                                       GError **error)
{
    g_return_val_if_fail(ctx && ctx->jobs && request && project_id, NULL);
    AsJobManager *mgr = ctx->jobs;

    gchar *job_id = as_uuid_random();
    ActiveJob *job = g_new0(ActiveJob, 1);
    job->id = g_strdup(job_id);
    job->project_id = g_strdup(project_id);
    job->type = g_strdup("generation");
    job->stage = g_strdup("Starting generation");
    job->state = AS_JOB_RUNNING;
    job->progress = 0.0;
    job->cancellable = g_cancellable_new();
    job->manager = mgr;

    g_mutex_lock(&mgr->lock);
    g_hash_table_insert(mgr->active_jobs, g_strdup(job_id), job);
    record_job_start(mgr, job);
    g_mutex_unlock(&mgr->lock);

    GenWorkerData *wd = g_new0(GenWorkerData, 1);
    wd->manager = g_object_ref(mgr);
    wd->job_id = g_strdup(job_id);
    wd->project_id = g_strdup(project_id);
    as_request_copy(&wd->request, request);
    wd->force_narration = force_narration;
    wd->cancellable = g_object_ref(job->cancellable);

    GThread *th = g_thread_try_new("gen-worker", generation_worker, wd, error);
    if (!th) {
        g_mutex_lock(&mgr->lock);
        g_hash_table_remove(mgr->active_jobs, job_id);
        g_mutex_unlock(&mgr->lock);
        g_free(job_id);
        return NULL;
    }
    g_thread_unref(th);

    return job_id;
}

/* Worker for Export */
typedef struct {
    AsJobManager *manager;
    gchar *job_id;
    gchar *project_id;
    gchar *output_path;
    gboolean preview;
    GCancellable *cancellable;
} ExportWorkerData;

static void export_progress_cb(gdouble fraction, const gchar *stage, gpointer user_data)
{
    ExportWorkerData *wd = user_data;
    g_mutex_lock(&wd->manager->lock);
    ActiveJob *job = g_hash_table_lookup(wd->manager->active_jobs, wd->job_id);
    if (job) {
        g_free(job->stage);
        job->stage = g_strdup(stage);
        job->progress = fraction;
        record_job_update(wd->manager, job);
    }
    g_mutex_unlock(&wd->manager->lock);
    emit_progress(wd->manager, wd->job_id, stage, fraction);
}

static gpointer export_worker(gpointer data)
{
    ExportWorkerData *wd = data;
    AsAppContext *ctx = wd->manager->ctx;

    GError *error = NULL;
    AsProject *project = as_project_load(ctx->db, wd->project_id, TRUE, &error);
    gboolean ok = FALSE;

    if (project && project->reel) {
        AsRenderOptions opts;
        as_render_options_for_ratio(project->aspect_ratio, wd->preview, &opts);
        gchar *proj_dir = as_app_context_project_dir(ctx, wd->project_id);

        AsRenderResult result;
        memset(&result, 0, sizeof(result));

        ok = as_video_render(project->reel, proj_dir, ctx->ffmpeg, &opts,
                             wd->output_path, export_progress_cb, wd,
                             wd->cancellable, &result, &error);

        if (ok) {
            AsExportRecord rec;
            memset(&rec, 0, sizeof(rec));
            rec.id = as_uuid_random();
            rec.project_id = wd->project_id;
            rec.path = wd->output_path;
            rec.format = "mp4";
            rec.width = result.width;
            rec.height = result.height;
            rec.duration = result.duration;
            rec.video_codec = result.video_codec;
            rec.audio_codec = result.audio_codec;
            rec.file_size = result.file_size;
            rec.verified = result.verified;
            as_export_store_insert(ctx->db, &rec, NULL);
            g_free(rec.id);
            as_render_result_clear(&result);
        }
        g_free(proj_dir);
    }

    if (project)
        as_project_free(project);

    g_mutex_lock(&wd->manager->lock);
    ActiveJob *job = g_hash_table_lookup(wd->manager->active_jobs, wd->job_id);
    if (job) {
        if (g_cancellable_is_cancelled(wd->cancellable)) {
            job->state = AS_JOB_CANCELLED;
            g_free(job->error);
            job->error = g_strdup("Export cancelled");
        } else if (ok) {
            job->state = AS_JOB_COMPLETED;
            job->progress = 1.0;
        } else {
            job->state = AS_JOB_FAILED;
            g_free(job->error);
            job->error = error ? g_strdup(error->message) : g_strdup("Export failed");
        }
        record_job_update(wd->manager, job);
    }
    g_mutex_unlock(&wd->manager->lock);

    emit_finished(wd->manager, wd->job_id, ok, error ? error->message : NULL);

    if (error)
        g_error_free(error);

    g_object_unref(wd->cancellable);
    g_object_unref(wd->manager);
    g_free(wd->job_id);
    g_free(wd->project_id);
    g_free(wd->output_path);
    g_free(wd);
    return NULL;
}

gchar *as_job_manager_start_export(AsAppContext *ctx, const gchar *project_id,
                                   const gchar *output_path, gboolean preview,
                                   GError **error)
{
    g_return_val_if_fail(ctx && ctx->jobs && project_id && output_path, NULL);
    AsJobManager *mgr = ctx->jobs;

    gchar *job_id = as_uuid_random();
    ActiveJob *job = g_new0(ActiveJob, 1);
    job->id = g_strdup(job_id);
    job->project_id = g_strdup(project_id);
    job->type = g_strdup("export");
    job->stage = g_strdup("Starting export");
    job->state = AS_JOB_RUNNING;
    job->progress = 0.0;
    job->cancellable = g_cancellable_new();
    job->manager = mgr;

    g_mutex_lock(&mgr->lock);
    g_hash_table_insert(mgr->active_jobs, g_strdup(job_id), job);
    record_job_start(mgr, job);
    g_mutex_unlock(&mgr->lock);

    ExportWorkerData *wd = g_new0(ExportWorkerData, 1);
    wd->manager = g_object_ref(mgr);
    wd->job_id = g_strdup(job_id);
    wd->project_id = g_strdup(project_id);
    wd->output_path = g_strdup(output_path);
    wd->preview = preview;
    wd->cancellable = g_object_ref(job->cancellable);

    GThread *th = g_thread_try_new("export-worker", export_worker, wd, error);
    if (!th) {
        g_mutex_lock(&mgr->lock);
        g_hash_table_remove(mgr->active_jobs, job_id);
        g_mutex_unlock(&mgr->lock);
        g_free(job_id);
        return NULL;
    }
    g_thread_unref(th);

    return job_id;
}

gchar *as_job_manager_start_scene_image(AsAppContext *ctx, const gchar *project_id,
                                        gint scene_id, GError **error)
{
    g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED, "Use as_ui_run_async for single scene regeneration");
    return NULL;
}

gchar *as_job_manager_start_narration(AsAppContext *ctx, const gchar *project_id,
                                      gboolean force, GError **error)
{
    g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED, "Use as_ui_run_async for narration regeneration");
    return NULL;
}

gboolean as_job_manager_cancel(AsJobManager *manager, const gchar *job_id)
{
    g_return_val_if_fail(manager && job_id, FALSE);
    gboolean cancelled = FALSE;
    g_mutex_lock(&manager->lock);
    ActiveJob *job = g_hash_table_lookup(manager->active_jobs, job_id);
    if (job && job->cancellable && !g_cancellable_is_cancelled(job->cancellable)) {
        g_cancellable_cancel(job->cancellable);
        job->state = AS_JOB_CANCELLED;
        cancelled = TRUE;
    }
    g_mutex_unlock(&manager->lock);
    return cancelled;
}

guint as_job_manager_active_count(AsJobManager *manager)
{
    g_return_val_if_fail(manager, 0);
    guint count = 0;
    g_mutex_lock(&manager->lock);
    if (manager->active_jobs) {
        GHashTableIter iter;
        gpointer key, value;
        g_hash_table_iter_init(&iter, manager->active_jobs);
        while (g_hash_table_iter_next(&iter, &key, &value)) {
            ActiveJob *job = value;
            if (job->state == AS_JOB_RUNNING || job->state == AS_JOB_QUEUED)
                count++;
        }
    }
    g_mutex_unlock(&manager->lock);
    return count;
}

gboolean as_job_manager_any_active(AsJobManager *manager)
{
    return as_job_manager_active_count(manager) > 0;
}

AsJobInfo *as_job_manager_get(AsJobManager *manager, const gchar *job_id)
{
    g_return_val_if_fail(manager && job_id, NULL);
    AsJobInfo *info = NULL;
    g_mutex_lock(&manager->lock);
    ActiveJob *job = g_hash_table_lookup(manager->active_jobs, job_id);
    if (job) {
        info = g_new0(AsJobInfo, 1);
        info->id = g_strdup(job->id);
        info->project_id = g_strdup(job->project_id);
        info->type = g_strdup(job->type);
        info->stage = g_strdup(job->stage);
        info->error = g_strdup(job->error);
        info->provider_id = g_strdup(job->provider_id);
        info->model_id = g_strdup(job->model_id);
        info->progress = job->progress;
        info->state = job->state;
        info->cancellable = (job->cancellable != NULL);
    }
    g_mutex_unlock(&manager->lock);
    return info;
}
