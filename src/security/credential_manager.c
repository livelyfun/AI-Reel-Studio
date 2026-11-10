/* credential_manager.c — API keys in the Secret Service, session fallback. */
#include "as_security.h"
#include "as_core.h"

#include <string.h>

#include <libsecret/secret.h>

struct _AsCredentialManager {
    GHashTable *session; /* provider_id -> secret (gchar*) */
    gboolean available;
};

static const SecretSchema provider_schema = {
    "org.aireelstudio.Provider",
    SECRET_SCHEMA_NONE,
    {
        { "provider_id", SECRET_SCHEMA_ATTRIBUTE_STRING },
        { NULL, 0 }
    }
};

static gchar *env_var_for_provider(const gchar *provider_id)
{
    GString *name = g_string_new("AS_KEY_");
    for (const gchar *p = provider_id; *p; p++) {
        gchar c = g_ascii_toupper(*p);
        g_string_append_c(name, g_ascii_isalnum(c) ? c : '_');
    }
    return g_string_free(name, FALSE);
}

AsCredentialManager *as_credential_manager_new(void)
{
    AsCredentialManager *manager = g_new0(AsCredentialManager, 1);
    manager->session = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                             (GDestroyNotify) as_secure_clear);

    GError *error = NULL;
    SecretService *service = secret_service_get_sync(SECRET_SERVICE_NONE, NULL,
                                                      &error);
    manager->available = service != NULL;
    if (service)
        g_object_unref(service);
    if (error) {
        AS_DEBUG("security", "Secret Service unavailable: %s", error->message);
        g_clear_error(&error);
    }
    return manager;
}

void as_credential_manager_free(AsCredentialManager *manager)
{
    if (!manager)
        return;
    as_credential_manager_clear_session(manager);
    g_hash_table_destroy(manager->session);
    g_free(manager);
}

gboolean as_credential_manager_available(AsCredentialManager *manager)
{
    return manager && manager->available;
}

gboolean as_credential_manager_set(AsCredentialManager *manager,
                                   const gchar *provider_id,
                                   const gchar *secret,
                                   gboolean persist,
                                   GError **error)
{
    g_return_val_if_fail(manager != NULL, FALSE);
    g_return_val_if_fail(provider_id != NULL, FALSE);
    if (!secret || !*secret) {
        g_set_error(error, AS_ERROR, AS_ERROR_SECRET,
                    "The API key is empty.");
        return FALSE;
    }

    as_redact_register_secret(secret);
    g_hash_table_replace(manager->session, g_strdup(provider_id),
                         g_strdup(secret));

    if (!persist)
        return TRUE;

    if (!manager->available) {
        g_set_error(error, AS_ERROR, AS_ERROR_SECRET,
                    "The Linux Secret Service is not available. The key was "
                    "kept for this session only; persistent storage requires a "
                    "running keyring.");
        return FALSE;
    }

    gchar *label = g_strdup_printf("AI Reel Studio — provider %s", provider_id);
    GError *local = NULL;
    gboolean ok = secret_password_store_sync(&provider_schema,
                                             SECRET_COLLECTION_DEFAULT,
                                             label, secret, NULL, &local,
                                             "provider_id", provider_id, NULL);
    g_free(label);
    if (!ok) {
        g_set_error(error, AS_ERROR, AS_ERROR_SECRET,
                    "Could not save the API key in the Secret Service: %s",
                    local ? local->message : "unknown error");
        g_clear_error(&local);
        return FALSE;
    }
    return TRUE;
}

gchar *as_credential_manager_get(AsCredentialManager *manager,
                                 const gchar *provider_id,
                                 GError **error)
{
    g_return_val_if_fail(manager != NULL, NULL);
    g_return_val_if_fail(provider_id != NULL, NULL);

    if (g_getenv("AI_REEL_STUDIO_ENV_KEYS") &&
        g_strcmp0(g_getenv("AI_REEL_STUDIO_ENV_KEYS"), "1") == 0) {
        gchar *var = env_var_for_provider(provider_id);
        const gchar *value = g_getenv(var);
        if (value && *value) {
            as_redact_register_secret(value);
            g_free(var);
            return g_strdup(value);
        }
        g_free(var);
    }

    const gchar *session = g_hash_table_lookup(manager->session, provider_id);
    if (session && *session)
        return g_strdup(session);

    if (!manager->available) {
        g_set_error(error, AS_ERROR, AS_ERROR_SECRET,
                    "No API key is configured for this provider, and the "
                    "Secret Service is unavailable.");
        return NULL;
    }

    GError *local = NULL;
    gchar *secret = secret_password_lookup_sync(&provider_schema, NULL, &local,
                                                "provider_id", provider_id, NULL);
    if (!secret) {
        if (local) {
            g_set_error(error, AS_ERROR, AS_ERROR_SECRET,
                        "Could not retrieve the API key: %s", local->message);
            g_clear_error(&local);
        } else {
            g_set_error(error, AS_ERROR, AS_ERROR_SECRET,
                        "No API key is saved for this provider.");
        }
        return NULL;
    }
    as_redact_register_secret(secret);
    return secret;
}

gboolean as_credential_manager_has(AsCredentialManager *manager,
                                   const gchar *provider_id)
{
    if (!manager || !provider_id)
        return FALSE;
    if (g_hash_table_contains(manager->session, provider_id))
        return TRUE;
    gchar *key = as_credential_manager_get(manager, provider_id, NULL);
    if (key) {
        as_secure_clear(key);
        return TRUE;
    }
    return FALSE;
}

gboolean as_credential_manager_remove(AsCredentialManager *manager,
                                      const gchar *provider_id,
                                      GError **error)
{
    if (!manager || !provider_id)
        return FALSE;
    g_hash_table_remove(manager->session, provider_id);
    if (!manager->available)
        return TRUE;
    GError *local = NULL;
    gboolean ok = secret_password_clear_sync(&provider_schema, NULL, &local,
                                             "provider_id", provider_id, NULL);
    if (!ok && local) {
        g_set_error(error, AS_ERROR, AS_ERROR_SECRET,
                    "Could not delete the stored API key: %s", local->message);
        g_clear_error(&local);
        return FALSE;
    }
    return TRUE;
}

void as_credential_manager_clear_session(AsCredentialManager *manager)
{
    if (!manager || !manager->session)
        return;
    g_hash_table_remove_all(manager->session);
}
