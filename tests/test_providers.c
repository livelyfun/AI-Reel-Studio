/* test_providers.c — tests for provider capability routing and secret redaction. */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include "as_provider.h"
#include "as_core.h"
#include "as_storage.h"

static void test_secret_redaction(void)
{
    const gchar *fake_key = "AIzaSyD-TEST-SECRET-KEY-123456";
    as_redact_register_secret(fake_key);

    gchar *text = g_strdup_printf("Request failed with key %s inside url", fake_key);
    gchar *redacted = as_redact(text);
    g_assert_null(strstr(redacted, fake_key));
    g_assert_nonnull(strstr(redacted, "[REDACTED]"));

    g_free(text);
    g_free(redacted);
    as_redact_clear_secrets();
}

static void test_capability_routing(void)
{
    gchar *tmp_path = g_build_filename(g_get_tmp_dir(), "test_routes.db", NULL);
    g_unlink(tmp_path);

    GError *error = NULL;
    AsDatabase *db = as_database_open(tmp_path, &error);
    g_assert_no_error(error);

    /* Insert test provider */
    AsProviderConfig cfg;
    as_provider_config_init(&cfg);
    cfg.id = g_strdup("test-prov-1");
    cfg.name = g_strdup("Test Gemini");
    cfg.type = AS_PROVIDER_GEMINI;
    cfg.base_url = g_strdup("https://example.com");
    cfg.text_model = g_strdup("gemini-2.5-flash");
    cfg.speech_model = g_strdup("gemini-3.1-flash-tts-preview");
    cfg.enabled = TRUE;
    cfg.capabilities = AS_CAP_TEXT | AS_CAP_STRUCTURED | AS_CAP_SPEECH;
    as_provider_store_save(db, &cfg, &error);
    g_assert_no_error(error);

    AsProviderRegistry *reg = as_provider_registry_new();
    gboolean ok = as_provider_registry_reload(reg, db, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    /* Set route for content plan */
    ok = as_provider_registry_set_route(reg, AS_ROUTE_PLANNING, "test-prov-1", "gemini-2.5-flash", &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    /* Resolve route */
    const gchar *model = NULL;
    AsProviderConfig *resolved = as_provider_registry_resolve(reg, AS_ROUTE_PLANNING, &model, &error);
    g_assert_no_error(error);
    g_assert_nonnull(resolved);
    g_assert_cmpstr(resolved->id, ==, "test-prov-1");
    g_assert_cmpstr(model, ==, "gemini-2.5-flash");

    as_provider_registry_free(reg);
    as_provider_config_clear(&cfg);
    as_database_close(db);
    g_unlink(tmp_path);
    g_free(tmp_path);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/providers/redaction", test_secret_redaction);
    g_test_add_func("/providers/routing", test_capability_routing);
    return g_test_run();
}
