/* storage_internal.h — shared lock/sqlite access for storage modules. */
#pragma once

#include <sqlite3.h>
#include "as_storage.h"

void as_database_lock(AsDatabase *db);
void as_database_unlock(AsDatabase *db);
sqlite3 *as_database_sqlite(AsDatabase *db);
const gchar *as_database_path(AsDatabase *db);
gboolean as_database_exec(AsDatabase *db, const gchar *sql, GError **error);
gboolean as_database_migrate(AsDatabase *db, GError **error);
gint as_database_user_version(AsDatabase *db);

/* small helpers */
gboolean as_sql_prepare(AsDatabase *db, const gchar *sql, sqlite3_stmt **stmt,
                        GError **error);
gboolean as_sql_step_done(AsDatabase *db, sqlite3_stmt *stmt, GError **error);
gchar *as_sql_column_text(sqlite3_stmt *stmt, gint index);
gint as_sql_column_int(sqlite3_stmt *stmt, gint index);
gdouble as_sql_column_double(sqlite3_stmt *stmt, gint index);
