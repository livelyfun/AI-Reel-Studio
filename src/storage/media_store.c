/* media_store.c — asset provenance and verified export records. */
#include "storage_internal.h"
#include "as_core.h"

void as_asset_record_free(AsAssetRecord *record)
{
    if (!record)
        return;
    g_free(record->id);
    g_free(record->project_id);
    g_free(record->kind);
    g_free(record->local_path);
    g_free(record->source);
    g_free(record->source_url);
    g_free(record->author);
    g_free(record->license_name);
    g_free(record->license_url);
    g_free(record->attribution);
    g_free(record->retrieved_at);
    g_free(record->sha256);
    g_free(record);
}

gboolean as_asset_store_insert(AsDatabase *db, const AsAssetRecord *record,
                               GError **error)
{
    g_return_val_if_fail(db != NULL && record != NULL && record->id != NULL, FALSE);
    const gchar *sql =
        "INSERT INTO assets(id,project_id,scene_id,kind,local_path,source,"
        "source_url,author,license_name,license_url,attribution,retrieved_at,"
        "sha256) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?);";
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
    sqlite3_bind_int(stmt, 3, record->scene_id);
    sqlite3_bind_text(stmt, 4, record->kind ? record->kind : "image", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, record->local_path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, record->source, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, record->source_url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, record->author, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, record->license_name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, record->license_url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, record->attribution, -1, SQLITE_TRANSIENT);
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 12, record->retrieved_at ? record->retrieved_at : now,
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 13, record->sha256, -1, SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

GPtrArray *as_asset_store_for_project(AsDatabase *db, const gchar *project_id,
                                      GError **error)
{
    g_return_val_if_fail(db != NULL && project_id != NULL, NULL);
    GPtrArray *list = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_asset_record_free);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    if (!as_sql_prepare(db,
                        "SELECT id,project_id,scene_id,kind,local_path,source,"
                        "source_url,author,license_name,license_url,attribution,"
                        "retrieved_at,sha256 FROM assets WHERE project_id=? "
                        "ORDER BY scene_id,kind;",
                        &stmt, error)) {
        as_database_unlock(db);
        g_ptr_array_unref(list);
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, project_id, -1, SQLITE_TRANSIENT);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        AsAssetRecord *r = g_new0(AsAssetRecord, 1);
        r->id = as_sql_column_text(stmt, 0);
        r->project_id = as_sql_column_text(stmt, 1);
        r->scene_id = as_sql_column_int(stmt, 2);
        r->kind = as_sql_column_text(stmt, 3);
        r->local_path = as_sql_column_text(stmt, 4);
        r->source = as_sql_column_text(stmt, 5);
        r->source_url = as_sql_column_text(stmt, 6);
        r->author = as_sql_column_text(stmt, 7);
        r->license_name = as_sql_column_text(stmt, 8);
        r->license_url = as_sql_column_text(stmt, 9);
        r->attribution = as_sql_column_text(stmt, 10);
        r->retrieved_at = as_sql_column_text(stmt, 11);
        r->sha256 = as_sql_column_text(stmt, 12);
        g_ptr_array_add(list, r);
    }
    if (rc != SQLITE_DONE) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not read assets: %s",
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

void as_export_record_free(AsExportRecord *record)
{
    if (!record)
        return;
    g_free(record->id);
    g_free(record->project_id);
    g_free(record->path);
    g_free(record->format);
    g_free(record->video_codec);
    g_free(record->audio_codec);
    g_free(record->created_at);
    g_free(record);
}

gboolean as_export_store_insert(AsDatabase *db, const AsExportRecord *record,
                                GError **error)
{
    g_return_val_if_fail(db != NULL && record != NULL && record->id != NULL, FALSE);
    const gchar *sql =
        "INSERT INTO exports(id,project_id,path,format,width,height,duration,"
        "video_codec,audio_codec,file_size,verified,created_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?);";
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
    sqlite3_bind_text(stmt, 3, record->path, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, record->format ? record->format : "mp4", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, record->width);
    sqlite3_bind_int(stmt, 6, record->height);
    sqlite3_bind_double(stmt, 7, record->duration);
    sqlite3_bind_text(stmt, 8, record->video_codec, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, record->audio_codec, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 10, record->file_size);
    sqlite3_bind_int(stmt, 11, record->verified ? 1 : 0);
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 12, record->created_at ? record->created_at : now, -1,
                      SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

GPtrArray *as_export_store_for_project(AsDatabase *db, const gchar *project_id,
                                       GError **error)
{
    g_return_val_if_fail(db != NULL && project_id != NULL, NULL);
    GPtrArray *list = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_export_record_free);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    if (!as_sql_prepare(db,
                        "SELECT id,project_id,path,format,width,height,duration,"
                        "video_codec,audio_codec,file_size,verified,created_at "
                        "FROM exports WHERE project_id=? ORDER BY created_at DESC;",
                        &stmt, error)) {
        as_database_unlock(db);
        g_ptr_array_unref(list);
        return NULL;
    }
    sqlite3_bind_text(stmt, 1, project_id, -1, SQLITE_TRANSIENT);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        AsExportRecord *r = g_new0(AsExportRecord, 1);
        r->id = as_sql_column_text(stmt, 0);
        r->project_id = as_sql_column_text(stmt, 1);
        r->path = as_sql_column_text(stmt, 2);
        r->format = as_sql_column_text(stmt, 3);
        r->width = as_sql_column_int(stmt, 4);
        r->height = as_sql_column_int(stmt, 5);
        r->duration = as_sql_column_double(stmt, 6);
        r->video_codec = as_sql_column_text(stmt, 7);
        r->audio_codec = as_sql_column_text(stmt, 8);
        r->file_size = sqlite3_column_int64(stmt, 9);
        r->verified = as_sql_column_int(stmt, 10) != 0;
        r->created_at = as_sql_column_text(stmt, 11);
        g_ptr_array_add(list, r);
    }
    if (rc != SQLITE_DONE) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not read exports: %s",
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

gint as_export_count(AsDatabase *db)
{
    if (!db)
        return 0;
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gint count = 0;
    if (sqlite3_prepare_v2(as_database_sqlite(db), "SELECT COUNT(*) FROM exports;",
                           -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    as_database_unlock(db);
    return count;
}
