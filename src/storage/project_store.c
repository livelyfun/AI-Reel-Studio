/* project_store.c — project metadata and reel document persistence. */
#include "storage_internal.h"
#include "reel_json.h"
#include "as_core.h"

#include <string.h>

static gboolean step_done_locked(AsDatabase *db, sqlite3_stmt *stmt, GError **error)
{
    return as_sql_step_done(db, stmt, error);
}

void as_project_summary_free(AsProjectSummary *summary)
{
    if (!summary)
        return;
    g_free(summary->id);
    g_free(summary->name);
    g_free(summary->status);
    g_free(summary->updated_at);
    g_free(summary->thumbnail_path);
    g_free(summary);
}

void as_project_free(AsProject *project)
{
    if (!project)
        return;
    g_free(project->id);
    g_free(project->name);
    g_free(project->status);
    g_free(project->prompt);
    g_free(project->voice_style);
    g_free(project->visual_style);
    g_free(project->thumbnail_path);
    g_free(project->export_path);
    g_free(project->created_at);
    g_free(project->updated_at);
    if (project->reel)
        as_reel_free(project->reel);
    g_free(project);
}

gboolean as_project_create(AsDatabase *db, const gchar *id, const gchar *name,
                           const AsReelRequest *request, GError **error)
{
    g_return_val_if_fail(db != NULL, FALSE);
    g_return_val_if_fail(id != NULL && *id, FALSE);
    g_return_val_if_fail(request != NULL, FALSE);
    if (!as_request_validate(request, error))
        return FALSE;

    const gchar *sql =
        "INSERT INTO projects(id,name,status,prompt,voice_style,visual_style,"
        "duration,aspect_ratio,created_at,updated_at) "
        "VALUES(?,?,'draft',?,?,?,?,?,?,?);";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, name && *name ? name : "Untitled reel", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, request->prompt ? request->prompt : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, request->voice_style, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, request->visual_style, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 6, request->duration);
    sqlite3_bind_text(stmt, 7, as_aspect_to_string(request->aspect_ratio), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, now, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

gboolean as_project_save_reel(AsDatabase *db, const gchar *id, const AsReel *reel,
                              const gchar *status, GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL && reel != NULL, FALSE);
    gchar *json = as_reel_to_json(reel);
    if (!json) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not serialize the project.");
        return FALSE;
    }
    const gchar *sql =
        "UPDATE projects SET reel_json=?, name=COALESCE(?,name), status=?, "
        "prompt=?, voice_style=?, visual_style=?, duration=?, aspect_ratio=?, "
        "total_duration=?, thumbnail_path=COALESCE(?,thumbnail_path), "
        "error_message=NULL, updated_at=? WHERE id=?;";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    const gchar *name = (reel->metadata.reel_name && *reel->metadata.reel_name)
                            ? reel->metadata.reel_name
                            : NULL;
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 1, json, -1, SQLITE_TRANSIENT);
    if (name)
        sqlite3_bind_text(stmt, 2, name, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, status ? status : "draft", -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, reel->request.prompt ? reel->request.prompt : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, reel->request.voice_style, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, reel->request.visual_style, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 7, reel->request.duration);
    sqlite3_bind_text(stmt, 8, as_aspect_to_string(reel->request.aspect_ratio), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 9, reel->total_duration);
    if (reel->thumbnail_path)
        sqlite3_bind_text(stmt, 10, reel->thumbnail_path, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 10);
    sqlite3_bind_text(stmt, 11, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 12, id, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    if (ok && sqlite3_changes(as_database_sqlite(db)) == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND, "Project not found.");
        ok = FALSE;
    }
    g_free(now);
out:
    as_database_unlock(db);
    g_free(json);
    return ok;
}

gboolean as_project_rename(AsDatabase *db, const gchar *id, const gchar *name,
                           GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, FALSE);
    if (!name || !*name) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The project name cannot be empty.");
        return FALSE;
    }
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, "UPDATE projects SET name=?, updated_at=? WHERE id=?;",
                        &stmt, error))
        goto out;
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, id, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    if (ok && sqlite3_changes(as_database_sqlite(db)) == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND, "Project not found.");
        ok = FALSE;
    }
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

gboolean as_project_delete(AsDatabase *db, const gchar *id, GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, FALSE);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, "DELETE FROM projects WHERE id=?;", &stmt, error))
        goto out;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    if (ok && sqlite3_changes(as_database_sqlite(db)) == 0)
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND, "Project not found.");
out:
    as_database_unlock(db);
    return ok;
}

gchar *as_project_duplicate(AsDatabase *db, const gchar *id, GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, NULL);
    gchar *new_id = as_uuid_random();
    const gchar *sql =
        "INSERT INTO projects(id,name,status,prompt,voice_style,visual_style,"
        "duration,aspect_ratio,reel_json,thumbnail_path,export_path,error_message,"
        "total_duration,created_at,updated_at) "
        "SELECT ?, name || ' (copy)', 'draft', prompt, voice_style, visual_style,"
        " duration, aspect_ratio, reel_json, thumbnail_path, NULL, NULL,"
        " total_duration, ?, ? FROM projects WHERE id=?;";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 1, new_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, id, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    if (ok && sqlite3_changes(as_database_sqlite(db)) == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND, "Project not found.");
        ok = FALSE;
    }
    g_free(now);
out:
    as_database_unlock(db);
    if (!ok) {
        g_free(new_id);
        return NULL;
    }
    return new_id;
}

/* Column order used by every SELECT below. */
#define PROJECT_COLUMNS                                                     \
    "id,name,status,prompt,voice_style,visual_style,duration,aspect_ratio,"  \
    "thumbnail_path,export_path,total_duration,created_at,updated_at,reel_json"

static AsProject *read_project(sqlite3_stmt *stmt, gboolean with_reel,
                               GError **error)
{
    AsProject *p = g_new0(AsProject, 1);
    p->id = as_sql_column_text(stmt, 0);
    p->name = as_sql_column_text(stmt, 1);
    p->status = as_sql_column_text(stmt, 2);
    p->prompt = as_sql_column_text(stmt, 3);
    p->voice_style = as_sql_column_text(stmt, 4);
    p->visual_style = as_sql_column_text(stmt, 5);
    p->duration = as_sql_column_int(stmt, 6);
    gchar *aspect = as_sql_column_text(stmt, 7);
    p->aspect_ratio = as_aspect_from_string(aspect);
    g_free(aspect);
    p->thumbnail_path = as_sql_column_text(stmt, 8);
    p->export_path = as_sql_column_text(stmt, 9);
    p->total_duration = as_sql_column_double(stmt, 10);
    p->created_at = as_sql_column_text(stmt, 11);
    p->updated_at = as_sql_column_text(stmt, 12);
    if (with_reel) {
        gchar *json = as_sql_column_text(stmt, 13);
        if (json) {
            p->reel = as_reel_from_json(json, error);
            g_free(json);
            if (!p->reel) {
                as_project_free(p);
                return NULL;
            }
        }
    }
    return p;
}

AsProject *as_project_load(AsDatabase *db, const gchar *id, gboolean with_reel,
                           GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, NULL);
    gchar *sql = g_strdup_printf("SELECT %s FROM projects WHERE id=?;",
                                 PROJECT_COLUMNS);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    AsProject *project = NULL;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        project = read_project(stmt, with_reel, error);
    else if (rc != SQLITE_DONE)
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not read project: %s",
                    sqlite3_errmsg(as_database_sqlite(db)));
    else
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND, "Project not found.");
    sqlite3_finalize(stmt);
out:
    as_database_unlock(db);
    g_free(sql);
    return project;
}

GPtrArray *as_project_list(AsDatabase *db, const gchar *search, GError **error)
{
    g_return_val_if_fail(db != NULL, NULL);
    GPtrArray *list = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_project_summary_free);
    gboolean filtered = search && *search;
    const gchar *sql_filtered =
        "SELECT id,name,status,updated_at,total_duration,thumbnail_path,"
        "reel_json IS NOT NULL AND reel_json <> '' FROM projects "
        "WHERE name LIKE ? OR prompt LIKE ? ORDER BY updated_at DESC;";
    const gchar *sql_all =
        "SELECT id,name,status,updated_at,total_duration,thumbnail_path,"
        "reel_json IS NOT NULL AND reel_json <> '' FROM projects "
        "ORDER BY updated_at DESC;";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    if (!as_sql_prepare(db, filtered ? sql_filtered : sql_all, &stmt, error)) {
        as_database_unlock(db);
        g_ptr_array_unref(list);
        return NULL;
    }
    if (filtered) {
        gchar *pattern = g_strdup_printf("%%%s%%", search);
        sqlite3_bind_text(stmt, 1, pattern, -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, pattern, -1, SQLITE_TRANSIENT);
        g_free(pattern);
    }
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        AsProjectSummary *s = g_new0(AsProjectSummary, 1);
        s->id = as_sql_column_text(stmt, 0);
        s->name = as_sql_column_text(stmt, 1);
        s->status = as_sql_column_text(stmt, 2);
        s->updated_at = as_sql_column_text(stmt, 3);
        s->total_duration = as_sql_column_double(stmt, 4);
        s->thumbnail_path = as_sql_column_text(stmt, 5);
        s->has_reel = as_sql_column_int(stmt, 6) != 0;
        g_ptr_array_add(list, s);
    }
    if (rc != SQLITE_DONE) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not list projects: %s",
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

gboolean as_project_set_export(AsDatabase *db, const gchar *id, const gchar *path,
                               GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, FALSE);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, "UPDATE projects SET export_path=?, "
                            "status=CASE WHEN status='draft' THEN 'completed' "
                            "ELSE status END, updated_at=? WHERE id=?;",
                        &stmt, error))
        goto out;
    gchar *now = as_now_iso8601();
    if (path)
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 1);
    sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, id, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

gboolean as_project_set_thumbnail(AsDatabase *db, const gchar *id,
                                  const gchar *path, GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, FALSE);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, "UPDATE projects SET thumbnail_path=?, updated_at=? "
                            "WHERE id=?;", &stmt, error))
        goto out;
    gchar *now = as_now_iso8601();
    if (path)
        sqlite3_bind_text(stmt, 1, path, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 1);
    sqlite3_bind_text(stmt, 2, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, id, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

gboolean as_project_set_status(AsDatabase *db, const gchar *id, const gchar *status,
                               const gchar *error_message, GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL && status != NULL, FALSE);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, "UPDATE projects SET status=?, error_message=?, "
                            "updated_at=? WHERE id=?;", &stmt, error))
        goto out;
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 1, status, -1, SQLITE_TRANSIENT);
    if (error_message)
        sqlite3_bind_text(stmt, 2, error_message, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 2);
    sqlite3_bind_text(stmt, 3, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, id, -1, SQLITE_TRANSIENT);
    ok = step_done_locked(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

guint as_project_count(AsDatabase *db, const gchar *status)
{
    if (!db)
        return 0;
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    guint count = 0;
    const gchar *sql = status ? "SELECT COUNT(*) FROM projects WHERE status=?;"
                              : "SELECT COUNT(*) FROM projects;";
    if (sqlite3_prepare_v2(as_database_sqlite(db), sql, -1, &stmt, NULL) ==
        SQLITE_OK) {
        if (status)
            sqlite3_bind_text(stmt, 1, status, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = (guint) sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    as_database_unlock(db);
    return count;
}

gboolean as_project_has_reel(AsDatabase *db, const gchar *id)
{
    if (!db || !id)
        return FALSE;
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean has = FALSE;
    if (sqlite3_prepare_v2(as_database_sqlite(db),
                           "SELECT reel_json IS NOT NULL AND reel_json <> '' "
                           "FROM projects WHERE id=?;",
                           -1, &stmt, NULL) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            has = sqlite3_column_int(stmt, 0) != 0;
        sqlite3_finalize(stmt);
    }
    as_database_unlock(db);
    return has;
}
