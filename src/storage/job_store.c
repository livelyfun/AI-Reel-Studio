/* job_store.c — durable job history and interrupted-job recovery. */
#include "storage_internal.h"
#include "as_core.h"

void as_job_record_free(AsJobRecord *record)
{
    if (!record)
        return;
    g_free(record->id);
    g_free(record->project_id);
    g_free(record->type);
    g_free(record->state);
    g_free(record->stage);
    g_free(record->error);
    g_free(record->provider_id);
    g_free(record->model_id);
    g_free(record->started_at);
    g_free(record->finished_at);
    g_free(record);
}

gboolean as_job_store_insert(AsDatabase *db, const AsJobRecord *record, GError **error)
{
    g_return_val_if_fail(db != NULL && record != NULL && record->id != NULL, FALSE);
    const gchar *sql =
        "INSERT INTO jobs(id,project_id,type,state,stage,progress,error,"
        "provider_id,model_id,started_at,finished_at) VALUES(?,?,?,?,?,?,?,?,?,?,?);";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    sqlite3_bind_text(stmt, 1, record->id, -1, SQLITE_TRANSIENT);
    if (record->project_id)
        sqlite3_bind_text(stmt, 2, record->project_id, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, record->type ? record->type : "generation", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record->state ? record->state : "queued", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, record->stage ? record->stage : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 6, record->progress);
    if (record->error)
        sqlite3_bind_text(stmt, 7, record->error, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 7);
    if (record->provider_id)
        sqlite3_bind_text(stmt, 8, record->provider_id, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 8);
    if (record->model_id)
        sqlite3_bind_text(stmt, 9, record->model_id, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 9);
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 10, record->started_at ? record->started_at : now, -1,
                      SQLITE_TRANSIENT);
    if (record->finished_at)
        sqlite3_bind_text(stmt, 11, record->finished_at, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 11);
    ok = as_sql_step_done(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

static gboolean state_is_terminal(const gchar *state)
{
    return g_strcmp0(state, "completed") == 0 || g_strcmp0(state, "failed") == 0 ||
           g_strcmp0(state, "cancelled") == 0 ||
           g_strcmp0(state, "interrupted") == 0;
}

gboolean as_job_store_update(AsDatabase *db, const gchar *id, const gchar *state,
                             const gchar *stage, gdouble progress,
                             const gchar *error_message, GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, FALSE);
    gboolean terminal = state && state_is_terminal(state);
    const gchar *sql = terminal
        ? "UPDATE jobs SET state=?,stage=?,progress=?,error=?,finished_at=? "
          "WHERE id=?;"
        : "UPDATE jobs SET state=?,stage=?,progress=?,error=? WHERE id=?;";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    sqlite3_bind_text(stmt, 1, state ? state : "running", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, stage ? stage : "", -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 3, progress);
    if (error_message)
        sqlite3_bind_text(stmt, 4, error_message, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 4);
    gchar *now = terminal ? as_now_iso8601() : NULL;
    if (terminal)
        sqlite3_bind_text(stmt, 5, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, terminal ? 6 : 5, id, -1, SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

GPtrArray *as_job_store_recent(AsDatabase *db, gint limit, GError **error)
{
    g_return_val_if_fail(db != NULL, NULL);
    if (limit <= 0 || limit > 500)
        limit = 50;
    GPtrArray *list = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_job_record_free);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    const gchar *sql =
        "SELECT id,project_id,type,state,stage,progress,error,provider_id,"
        "model_id,started_at,finished_at FROM jobs "
        "ORDER BY COALESCE(started_at,'') DESC LIMIT ?;";
    if (!as_sql_prepare(db, sql, &stmt, error)) {
        as_database_unlock(db);
        g_ptr_array_unref(list);
        return NULL;
    }
    sqlite3_bind_int(stmt, 1, limit);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        AsJobRecord *r = g_new0(AsJobRecord, 1);
        r->id = as_sql_column_text(stmt, 0);
        r->project_id = as_sql_column_text(stmt, 1);
        r->type = as_sql_column_text(stmt, 2);
        r->state = as_sql_column_text(stmt, 3);
        r->stage = as_sql_column_text(stmt, 4);
        r->progress = as_sql_column_double(stmt, 5);
        r->error = as_sql_column_text(stmt, 6);
        r->provider_id = as_sql_column_text(stmt, 7);
        r->model_id = as_sql_column_text(stmt, 8);
        r->started_at = as_sql_column_text(stmt, 9);
        r->finished_at = as_sql_column_text(stmt, 10);
        g_ptr_array_add(list, r);
    }
    if (rc != SQLITE_DONE) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not read jobs: %s",
                    sqlite3_errmsg(as_database_sqlite(db)));
        sqlite3_finalize(stmt);
        as_database_unlock(db);
        g_ptr_array_unref(list);
        return NULL;
    }
    sqlite3_finalize(stmt);
    as_database_unlock(db);
    return list;
}

gint as_job_store_mark_interrupted(AsDatabase *db, GError **error)
{
    g_return_val_if_fail(db != NULL, 0);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db,
                        "UPDATE jobs SET state='interrupted',"
                        "error='The application stopped before this job finished.',"
                        "finished_at=? WHERE state IN "
                        "('pending','queued','running','retrying');",
                        &stmt, error)) {
        as_database_unlock(db);
        return 0;
    }
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 1, now, -1, SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
    gint changed = ok ? sqlite3_changes(as_database_sqlite(db)) : 0;
    g_free(now);
    as_database_unlock(db);
    return changed;
}
