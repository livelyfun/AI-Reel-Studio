/* provider_store.c — provider configuration and capability routes. */
#include "storage_internal.h"
#include "as_core.h"

#define PROVIDER_COLUMNS                                                       \
    "id,name,type,base_url,auth_type,api_version,text_model,speech_model,"      \
    "image_model,speech_voice,capabilities,timeout_ms,max_retries,enabled,"     \
    "insecure_http_allowed,chat_path,models_path,speech_path,image_path,"       \
    "extra_json"

static AsProviderConfig *provider_from_row(sqlite3_stmt *stmt)
{
    AsProviderConfig *cfg = as_provider_config_new();
    cfg->id = as_sql_column_text(stmt, 0);
    cfg->name = as_sql_column_text(stmt, 1);
    gchar *type = as_sql_column_text(stmt, 2);
    cfg->type = as_provider_type_from_string(type);
    g_free(type);
    cfg->base_url = as_sql_column_text(stmt, 3);
    gchar *auth = as_sql_column_text(stmt, 4);
    cfg->auth_type = as_auth_type_from_string(auth);
    g_free(auth);
    cfg->api_version = as_sql_column_text(stmt, 5);
    cfg->text_model = as_sql_column_text(stmt, 6);
    cfg->speech_model = as_sql_column_text(stmt, 7);
    cfg->image_model = as_sql_column_text(stmt, 8);
    cfg->speech_voice = as_sql_column_text(stmt, 9);
    cfg->capabilities = (guint) as_sql_column_int(stmt, 10);
    cfg->timeout_ms = as_sql_column_int(stmt, 11);
    cfg->max_retries = as_sql_column_int(stmt, 12);
    cfg->enabled = as_sql_column_int(stmt, 13) != 0;
    cfg->insecure_http_allowed = as_sql_column_int(stmt, 14) != 0;
    cfg->chat_path = as_sql_column_text(stmt, 15);
    cfg->models_path = as_sql_column_text(stmt, 16);
    cfg->speech_path = as_sql_column_text(stmt, 17);
    cfg->image_path = as_sql_column_text(stmt, 18);
    cfg->extra_json = as_sql_column_text(stmt, 19);
    return cfg;
}

gboolean as_provider_store_list(AsDatabase *db, GPtrArray *out, GError **error)
{
    g_return_val_if_fail(db != NULL && out != NULL, FALSE);
    gchar *sql = g_strdup_printf("SELECT %s FROM providers ORDER BY name;",
                                 PROVIDER_COLUMNS);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    if (!as_sql_prepare(db, sql, &stmt, error)) {
        as_database_unlock(db);
        g_free(sql);
        return FALSE;
    }
    g_free(sql);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW)
        g_ptr_array_add(out, provider_from_row(stmt));
    gboolean ok = TRUE;
    if (rc != SQLITE_DONE) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not list providers: %s",
                    sqlite3_errmsg(as_database_sqlite(db)));
        ok = FALSE;
    }
    sqlite3_finalize(stmt);
    as_database_unlock(db);
    return ok;
}

gboolean as_provider_store_save(AsDatabase *db, const AsProviderConfig *cfg,
                                GError **error)
{
    g_return_val_if_fail(db != NULL && cfg != NULL, FALSE);
    if (!as_provider_config_validate(cfg, error))
        return FALSE;
    const gchar *sql =
        "INSERT INTO providers(" PROVIDER_COLUMNS ",created_at,updated_at) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON CONFLICT(id) DO UPDATE SET name=excluded.name,type=excluded.type,"
        "base_url=excluded.base_url,auth_type=excluded.auth_type,"
        "api_version=excluded.api_version,text_model=excluded.text_model,"
        "speech_model=excluded.speech_model,image_model=excluded.image_model,"
        "speech_voice=excluded.speech_voice,capabilities=excluded.capabilities,"
        "timeout_ms=excluded.timeout_ms,max_retries=excluded.max_retries,"
        "enabled=excluded.enabled,"
        "insecure_http_allowed=excluded.insecure_http_allowed,"
        "chat_path=excluded.chat_path,models_path=excluded.models_path,"
        "speech_path=excluded.speech_path,image_path=excluded.image_path,"
        "extra_json=excluded.extra_json,updated_at=excluded.updated_at;";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    gchar *now = as_now_iso8601();
    sqlite3_bind_text(stmt, 1, cfg->id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, cfg->name, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, as_provider_type_to_string(cfg->type), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, cfg->base_url, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, as_auth_type_to_string(cfg->auth_type), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, cfg->api_version ? cfg->api_version : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, cfg->text_model ? cfg->text_model : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, cfg->speech_model ? cfg->speech_model : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, cfg->image_model ? cfg->image_model : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, cfg->speech_voice ? cfg->speech_voice : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 11, (gint) cfg->capabilities);
    sqlite3_bind_int(stmt, 12, cfg->timeout_ms);
    sqlite3_bind_int(stmt, 13, cfg->max_retries);
    sqlite3_bind_int(stmt, 14, cfg->enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 15, cfg->insecure_http_allowed ? 1 : 0);
    sqlite3_bind_text(stmt, 16, cfg->chat_path ? cfg->chat_path : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 17, cfg->models_path ? cfg->models_path : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 18, cfg->speech_path ? cfg->speech_path : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 19, cfg->image_path ? cfg->image_path : "", -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 20, cfg->extra_json && *cfg->extra_json
                                   ? cfg->extra_json
                                   : "{}",
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 21, now, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 22, now, -1, SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
    g_free(now);
out:
    as_database_unlock(db);
    return ok;
}

gboolean as_provider_store_delete(AsDatabase *db, const gchar *id, GError **error)
{
    g_return_val_if_fail(db != NULL && id != NULL, FALSE);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, "DELETE FROM providers WHERE id=?;", &stmt, error))
        goto out;
    sqlite3_bind_text(stmt, 1, id, -1, SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
out:
    as_database_unlock(db);
    return ok;
}

gboolean as_provider_store_load_routes(AsDatabase *db, GHashTable *out,
                                       GError **error)
{
    g_return_val_if_fail(db != NULL && out != NULL, FALSE);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    if (!as_sql_prepare(db, "SELECT capability,provider_id,model_id "
                            "FROM capability_routes;", &stmt, error)) {
        as_database_unlock(db);
        return FALSE;
    }
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        gchar *capability = as_sql_column_text(stmt, 0);
        AsCapabilityRoute *route = g_new0(AsCapabilityRoute, 1);
        route->provider_id = as_sql_column_text(stmt, 1);
        route->model_id = as_sql_column_text(stmt, 2);
        g_hash_table_replace(out, capability, route);
    }
    gboolean ok = TRUE;
    if (rc != SQLITE_DONE) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not load routes: %s",
                    sqlite3_errmsg(as_database_sqlite(db)));
        ok = FALSE;
    }
    sqlite3_finalize(stmt);
    as_database_unlock(db);
    return ok;
}

gboolean as_provider_store_save_route(AsDatabase *db, AsRouteCapability cap,
                                      const gchar *provider_id,
                                      const gchar *model_id, GError **error)
{
    g_return_val_if_fail(db != NULL && provider_id != NULL, FALSE);
    const gchar *sql =
        "INSERT INTO capability_routes(capability,provider_id,model_id) "
        "VALUES(?,?,?) ON CONFLICT(capability) DO UPDATE SET "
        "provider_id=excluded.provider_id,model_id=excluded.model_id;";
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, sql, &stmt, error))
        goto out;
    sqlite3_bind_text(stmt, 1, as_route_to_string(cap), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, provider_id, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, model_id ? model_id : "", -1, SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
out:
    as_database_unlock(db);
    return ok;
}

gboolean as_provider_store_clear_route(AsDatabase *db, AsRouteCapability cap,
                                       GError **error)
{
    g_return_val_if_fail(db != NULL, FALSE);
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (!as_sql_prepare(db, "DELETE FROM capability_routes WHERE capability=?;",
                        &stmt, error))
        goto out;
    sqlite3_bind_text(stmt, 1, as_route_to_string(cap), -1, SQLITE_TRANSIENT);
    ok = as_sql_step_done(db, stmt, error);
out:
    as_database_unlock(db);
    return ok;
}

gint as_provider_count(AsDatabase *db, gboolean enabled_only)
{
    if (!db)
        return 0;
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gint count = 0;
    const gchar *sql = enabled_only
                           ? "SELECT COUNT(*) FROM providers WHERE enabled=1;"
                           : "SELECT COUNT(*) FROM providers;";
    if (sqlite3_prepare_v2(as_database_sqlite(db), sql, -1, &stmt, NULL) ==
        SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    as_database_unlock(db);
    return count;
}
