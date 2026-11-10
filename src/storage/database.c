/* database.c — SQLite lifecycle, migrations, integrity and backup. */
#include "storage_internal.h"
#include "as_core.h"

#include <string.h>
#include <sys/stat.h>

#include <glib/gstdio.h>

#define AS_SCHEMA_VERSION 1

struct _AsDatabase {
    sqlite3 *sqlite;
    GMutex lock;
    gchar *path;
};

void as_database_lock(AsDatabase *db)
{
    g_mutex_lock(&db->lock);
}

void as_database_unlock(AsDatabase *db)
{
    g_mutex_unlock(&db->lock);
}

sqlite3 *as_database_sqlite(AsDatabase *db)
{
    return db->sqlite;
}

const gchar *as_database_path(AsDatabase *db)
{
    return db->path;
}

gboolean as_sql_prepare(AsDatabase *db, const gchar *sql, sqlite3_stmt **stmt,
                        GError **error)
{
    int rc = sqlite3_prepare_v2(db->sqlite, sql, -1, stmt, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "SQL prepare failed: %s (%s)",
                    sqlite3_errmsg(db->sqlite), sql);
        return FALSE;
    }
    return TRUE;
}

gboolean as_sql_step_done(AsDatabase *db, sqlite3_stmt *stmt, GError **error)
{
    int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "SQL execution failed: %s",
                    sqlite3_errmsg(db->sqlite));
        sqlite3_finalize(stmt);
        return FALSE;
    }
    sqlite3_finalize(stmt);
    return TRUE;
}

gchar *as_sql_column_text(sqlite3_stmt *stmt, gint index)
{
    const unsigned char *value = sqlite3_column_text(stmt, index);
    return value ? g_strdup((const gchar *) value) : NULL;
}

gint as_sql_column_int(sqlite3_stmt *stmt, gint index)
{
    return sqlite3_column_int(stmt, index);
}

gdouble as_sql_column_double(sqlite3_stmt *stmt, gint index)
{
    return sqlite3_column_double(stmt, index);
}

gboolean as_database_exec(AsDatabase *db, const gchar *sql, GError **error)
{
    char *message = NULL;
    int rc = sqlite3_exec(db->sqlite, sql, NULL, NULL, &message);
    if (rc != SQLITE_OK) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Database error: %s",
                    message ? message : sqlite3_errmsg(db->sqlite));
        sqlite3_free(message);
        return FALSE;
    }
    return TRUE;
}

gint as_database_user_version(AsDatabase *db)
{
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db->sqlite, "PRAGMA user_version;", -1, &stmt, NULL) !=
        SQLITE_OK)
        return 0;
    gint version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        version = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return version;
}

static gboolean migrate_v1(AsDatabase *db, GError **error)
{
    const gchar *sql =
        "CREATE TABLE IF NOT EXISTS projects ("
        " id TEXT PRIMARY KEY,"
        " name TEXT NOT NULL,"
        " status TEXT NOT NULL DEFAULT 'draft',"
        " prompt TEXT NOT NULL DEFAULT '',"
        " voice_style TEXT NOT NULL DEFAULT 'Cinematic',"
        " visual_style TEXT NOT NULL DEFAULT 'Cinematic',"
        " duration INTEGER NOT NULL DEFAULT 30,"
        " aspect_ratio TEXT NOT NULL DEFAULT '9:16',"
        " reel_json TEXT,"
        " thumbnail_path TEXT,"
        " export_path TEXT,"
        " error_message TEXT,"
        " total_duration REAL NOT NULL DEFAULT 0,"
        " created_at TEXT NOT NULL,"
        " updated_at TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_projects_updated ON projects(updated_at DESC);"
        "CREATE TABLE IF NOT EXISTS providers ("
        " id TEXT PRIMARY KEY,"
        " name TEXT NOT NULL,"
        " type TEXT NOT NULL,"
        " base_url TEXT NOT NULL,"
        " auth_type TEXT NOT NULL DEFAULT 'bearer',"
        " api_version TEXT NOT NULL DEFAULT '',"
        " text_model TEXT NOT NULL DEFAULT '',"
        " speech_model TEXT NOT NULL DEFAULT '',"
        " image_model TEXT NOT NULL DEFAULT '',"
        " speech_voice TEXT NOT NULL DEFAULT '',"
        " capabilities INTEGER NOT NULL DEFAULT 3,"
        " timeout_ms INTEGER NOT NULL DEFAULT 60000,"
        " max_retries INTEGER NOT NULL DEFAULT 2,"
        " enabled INTEGER NOT NULL DEFAULT 1,"
        " insecure_http_allowed INTEGER NOT NULL DEFAULT 0,"
        " chat_path TEXT NOT NULL DEFAULT '',"
        " models_path TEXT NOT NULL DEFAULT '',"
        " speech_path TEXT NOT NULL DEFAULT '',"
        " image_path TEXT NOT NULL DEFAULT '',"
        " extra_json TEXT NOT NULL DEFAULT '{}',"
        " created_at TEXT NOT NULL,"
        " updated_at TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS capability_routes ("
        " capability TEXT PRIMARY KEY,"
        " provider_id TEXT NOT NULL REFERENCES providers(id) ON DELETE CASCADE,"
        " model_id TEXT NOT NULL DEFAULT ''"
        ");"
        "CREATE TABLE IF NOT EXISTS jobs ("
        " id TEXT PRIMARY KEY,"
        " project_id TEXT REFERENCES projects(id) ON DELETE CASCADE,"
        " type TEXT NOT NULL,"
        " state TEXT NOT NULL,"
        " stage TEXT NOT NULL DEFAULT '',"
        " progress REAL NOT NULL DEFAULT 0,"
        " error TEXT,"
        " provider_id TEXT,"
        " model_id TEXT,"
        " usage_json TEXT,"
        " started_at TEXT,"
        " finished_at TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_jobs_project ON jobs(project_id);"
        "CREATE INDEX IF NOT EXISTS idx_jobs_state ON jobs(state);"
        "CREATE TABLE IF NOT EXISTS assets ("
        " id TEXT PRIMARY KEY,"
        " project_id TEXT REFERENCES projects(id) ON DELETE CASCADE,"
        " scene_id INTEGER,"
        " kind TEXT NOT NULL,"
        " local_path TEXT NOT NULL,"
        " source TEXT,"
        " source_url TEXT,"
        " author TEXT,"
        " license_name TEXT,"
        " license_url TEXT,"
        " attribution TEXT,"
        " retrieved_at TEXT,"
        " sha256 TEXT"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_assets_project ON assets(project_id);"
        "CREATE TABLE IF NOT EXISTS exports ("
        " id TEXT PRIMARY KEY,"
        " project_id TEXT REFERENCES projects(id) ON DELETE CASCADE,"
        " path TEXT NOT NULL,"
        " format TEXT NOT NULL DEFAULT 'mp4',"
        " width INTEGER,"
        " height INTEGER,"
        " duration REAL,"
        " video_codec TEXT,"
        " audio_codec TEXT,"
        " file_size INTEGER,"
        " verified INTEGER NOT NULL DEFAULT 0,"
        " created_at TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_exports_project ON exports(project_id);"
        "CREATE TABLE IF NOT EXISTS settings ("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL"
        ");";

    if (!as_database_exec(db, "BEGIN IMMEDIATE;", error))
        return FALSE;
    if (!as_database_exec(db, sql, error)) {
        as_database_exec(db, "ROLLBACK;", NULL);
        return FALSE;
    }
    if (!as_database_exec(db, "CREATE TABLE IF NOT EXISTS schema_migrations ("
                              " version INTEGER PRIMARY KEY,"
                              " applied_at TEXT NOT NULL);", error)) {
        as_database_exec(db, "ROLLBACK;", NULL);
        return FALSE;
    }
    gchar *insert = g_strdup_printf(
        "INSERT OR IGNORE INTO schema_migrations(version, applied_at) "
        "VALUES(%d, '%s'); PRAGMA user_version=%d;",
        AS_SCHEMA_VERSION, as_now_iso8601(), AS_SCHEMA_VERSION);
    gboolean ok = as_database_exec(db, insert, error);
    g_free(insert);
    if (!ok) {
        as_database_exec(db, "ROLLBACK;", NULL);
        return FALSE;
    }
    return as_database_exec(db, "COMMIT;", error);
}

gboolean as_database_migrate(AsDatabase *db, GError **error)
{
    gint version = as_database_user_version(db);
    if (version > AS_SCHEMA_VERSION) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB,
                    "The database schema is newer than this application "
                    "(database %d, application %d). Please update AI Reel Studio.",
                    version, AS_SCHEMA_VERSION);
        return FALSE;
    }
    if (version < 1) {
        if (!migrate_v1(db, error))
            return FALSE;
    }
    return TRUE;
}

AsDatabase *as_database_open(const gchar *path, GError **error)
{
    g_return_val_if_fail(path != NULL, NULL);
    gboolean existed = g_file_test(path, G_FILE_TEST_EXISTS);

    sqlite3 *sqlite = NULL;
    int rc = sqlite3_open_v2(path, &sqlite,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                 SQLITE_OPEN_FULLMUTEX,
                             NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not open database: %s",
                    sqlite ? sqlite3_errmsg(sqlite) : "out of memory");
        if (sqlite)
            sqlite3_close(sqlite);
        return NULL;
    }
    sqlite3_busy_timeout(sqlite, 5000);
    sqlite3_exec(sqlite, "PRAGMA foreign_keys=ON;", NULL, NULL, NULL);
    sqlite3_exec(sqlite, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(sqlite, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);

    AsDatabase *db = g_new0(AsDatabase, 1);
    db->sqlite = sqlite;
    db->path = g_strdup(path);
    g_mutex_init(&db->lock);

    if (existed && as_database_user_version(db) < AS_SCHEMA_VERSION) {
        gchar *stamp = g_date_time_format(g_date_time_new_now_local(), "%Y%m%d-%H%M%S");
        gchar *backup = g_strdup_printf("%s.pre-v%d-%s.bak", path,
                                        AS_SCHEMA_VERSION, stamp);
        if (!as_database_backup(db, backup, error)) {
            g_free(stamp);
            g_free(backup);
            as_database_close(db);
            return NULL;
        }
        g_free(stamp);
        g_free(backup);
    }

    as_database_lock(db);
    gboolean ok = as_database_migrate(db, error);
    as_database_unlock(db);
    if (!ok) {
        as_database_close(db);
        return NULL;
    }
    g_chmod(path, 0600);
    return db;
}

void as_database_close(AsDatabase *db)
{
    if (!db)
        return;
    sqlite3_close(db->sqlite);
    g_mutex_clear(&db->lock);
    g_free(db->path);
    g_free(db);
}

gpointer as_database_handle(AsDatabase *db)
{
    return db ? db->sqlite : NULL;
}

gboolean as_database_backup(AsDatabase *db, const gchar *backup_path, GError **error)
{
    if (!db || !backup_path)
        return FALSE;
    sqlite3 *dest = NULL;
    int rc = sqlite3_open_v2(backup_path, &dest,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Could not create backup: %s",
                    dest ? sqlite3_errmsg(dest) : "open failed");
        if (dest)
            sqlite3_close(dest);
        return FALSE;
    }
    as_database_lock(db);
    sqlite3_backup *backup = sqlite3_backup_init(dest, "main", db->sqlite, "main");
    gboolean ok = FALSE;
    if (backup) {
        sqlite3_backup_step(backup, -1);
        rc = sqlite3_backup_finish(backup);
        ok = rc == SQLITE_OK;
    }
    as_database_unlock(db);
    if (!ok)
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Database backup failed: %s",
                    sqlite3_errmsg(dest));
    sqlite3_close(dest);
    if (ok)
        g_chmod(backup_path, 0600);
    return ok;
}

gboolean as_database_integrity_check(AsDatabase *db, GError **error)
{
    as_database_lock(db);
    sqlite3_stmt *stmt = NULL;
    gboolean ok = FALSE;
    if (sqlite3_prepare_v2(db->sqlite, "PRAGMA integrity_check;", -1, &stmt, NULL) ==
        SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const unsigned char *value = sqlite3_column_text(stmt, 0);
            ok = value && g_strcmp0((const gchar *) value, "ok") == 0;
            if (!ok)
                g_set_error(error, AS_ERROR, AS_ERROR_DB,
                            "Database integrity check failed: %s",
                            value ? (const gchar *) value : "unknown");
        }
    } else {
        g_set_error(error, AS_ERROR, AS_ERROR_DB, "Integrity check failed: %s",
                    sqlite3_errmsg(db->sqlite));
    }
    if (stmt)
        sqlite3_finalize(stmt);
    as_database_unlock(db);
    return ok;
}
