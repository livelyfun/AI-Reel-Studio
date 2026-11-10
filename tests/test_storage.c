/* test_storage.c — unit tests for SQLite database, projects, jobs and providers. */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include "as_storage.h"
#include "as_types.h"
#include "as_core.h"

static void test_database_lifecycle(void)
{
    gchar *tmp_path = g_build_filename(g_get_tmp_dir(), "test_studio.db", NULL);
    g_unlink(tmp_path);

    GError *error = NULL;
    AsDatabase *db = as_database_open(tmp_path, &error);
    g_assert_no_error(error);
    g_assert_nonnull(db);

    gboolean check = as_database_integrity_check(db, &error);
    g_assert_no_error(error);
    g_assert_true(check);

    as_database_close(db);
    g_unlink(tmp_path);
    g_free(tmp_path);
}

static void test_project_crud(void)
{
    gchar *tmp_path = g_build_filename(g_get_tmp_dir(), "test_proj.db", NULL);
    g_unlink(tmp_path);

    GError *error = NULL;
    AsDatabase *db = as_database_open(tmp_path, &error);
    g_assert_no_error(error);

    AsReelRequest req;
    memset(&req, 0, sizeof(req));
    req.prompt = g_strdup("Explore deep ocean mysteries");
    req.voice_style = g_strdup("Cinematic");
    req.visual_style = g_strdup("Documentary");
    req.duration = 30;
    req.aspect_ratio = AS_ASPECT_9_16;

    gboolean ok = as_project_create(db, "proj-123", "Deep Ocean", &req, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    AsProject *loaded = as_project_load(db, "proj-123", FALSE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(loaded);
    g_assert_cmpstr(loaded->id, ==, "proj-123");
    g_assert_cmpstr(loaded->name, ==, "Deep Ocean");
    g_assert_cmpint(loaded->duration, ==, 30);
    as_project_free(loaded);

    /* Duplicate project */
    gchar *dup_id = as_project_duplicate(db, "proj-123", &error);
    g_assert_no_error(error);
    g_assert_nonnull(dup_id);

    AsProject *dup = as_project_load(db, dup_id, FALSE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(dup);
    g_assert_cmpstr(dup->id, ==, dup_id);
    as_project_free(dup);
    g_free(dup_id);

    /* Delete project */
    ok = as_project_delete(db, "proj-123", &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    as_request_clear(&req);
    as_database_close(db);
    g_unlink(tmp_path);
    g_free(tmp_path);
}

static void test_provider_store(void)
{
    gchar *tmp_path = g_build_filename(g_get_tmp_dir(), "test_prov.db", NULL);
    g_unlink(tmp_path);

    GError *error = NULL;
    AsDatabase *db = as_database_open(tmp_path, &error);
    g_assert_no_error(error);

    AsProviderConfig cfg;
    as_provider_config_init(&cfg);
    cfg.id = g_strdup("gemini-test");
    cfg.name = g_strdup("Google Gemini Pro");
    cfg.type = AS_PROVIDER_GEMINI;
    cfg.base_url = g_strdup("https://generativelanguage.googleapis.com");
    cfg.auth_type = AS_AUTH_QUERY_KEY;
    cfg.text_model = g_strdup("gemini-2.5-flash");
    cfg.timeout_ms = 45000;
    cfg.enabled = TRUE;
    cfg.capabilities = AS_CAP_TEXT | AS_CAP_STRUCTURED | AS_CAP_SPEECH;

    gboolean ok = as_provider_store_save(db, &cfg, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    GPtrArray *list = g_ptr_array_new_with_free_func((GDestroyNotify)as_provider_config_free);
    ok = as_provider_store_list(db, list, &error);
    g_assert_no_error(error);
    g_assert_true(ok);
    g_assert_cmpuint(list->len, ==, 1);

    AsProviderConfig *first = g_ptr_array_index(list, 0);
    g_assert_cmpstr(first->id, ==, "gemini-test");
    g_assert_cmpstr(first->name, ==, "Google Gemini Pro");
    g_assert_cmpint(first->type, ==, AS_PROVIDER_GEMINI);

    g_ptr_array_unref(list);
    as_provider_config_clear(&cfg);
    as_database_close(db);
    g_unlink(tmp_path);
    g_free(tmp_path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/storage/database_lifecycle", test_database_lifecycle);
    g_test_add_func("/storage/project_crud", test_project_crud);
    g_test_add_func("/storage/provider_store", test_provider_store);
    return g_test_run();
}
