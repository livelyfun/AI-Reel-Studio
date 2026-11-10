/* as_security.h — Secret Service credential storage. */
#pragma once

#include <glib.h>

typedef struct _AsCredentialManager AsCredentialManager;

AsCredentialManager *as_credential_manager_new(void);
void as_credential_manager_free(AsCredentialManager *manager);

/* Whether the real Secret Service is reachable. */
gboolean as_credential_manager_available(AsCredentialManager *manager);

/* persist = TRUE writes to Secret Service; FALSE keeps the key in memory only
 * for this process. Persistent storage fails rather than falling back to
 * plaintext when the service is unavailable. */
gboolean as_credential_manager_set(AsCredentialManager *manager,
                                   const gchar *provider_id,
                                   const gchar *secret,
                                   gboolean persist,
                                   GError **error);
/* Returns a newly allocated secret; caller must as_secure_clear()/g_free(). */
gchar *as_credential_manager_get(AsCredentialManager *manager,
                                 const gchar *provider_id,
                                 GError **error);
gboolean as_credential_manager_has(AsCredentialManager *manager,
                                   const gchar *provider_id);
gboolean as_credential_manager_remove(AsCredentialManager *manager,
                                      const gchar *provider_id,
                                      GError **error);
/* Drops all in-memory session keys (called on shutdown). */
void as_credential_manager_clear_session(AsCredentialManager *manager);
