/* as_provider.h — provider configuration, capability routing and adapters.
 *
 * Provider adapters own all provider-specific HTTP details. The generation
 * engine only sees AsProviderConfig + model + capability results.
 */
#pragma once

#include <glib.h>
#include <gio/gio.h>
#include <json-glib/json-glib.h>
#include "as_http.h"

#define AS_CAP_TEXT       (1u << 0)
#define AS_CAP_STRUCTURED (1u << 1)
#define AS_CAP_SPEECH     (1u << 2)
#define AS_CAP_IMAGE      (1u << 3)
#define AS_CAP_VISION     (1u << 4)
#define AS_CAP_ALL        ((1u << 5) - 1u)

typedef enum {
    AS_PROVIDER_GEMINI = 0,
    AS_PROVIDER_OPENROUTER,
    AS_PROVIDER_OPENAI_COMPATIBLE
} AsProviderType;

typedef enum {
    AS_AUTH_BEARER = 0,
    AS_AUTH_X_API_KEY,
    AS_AUTH_QUERY_KEY,
    AS_AUTH_NONE
} AsAuthType;

typedef enum {
    AS_ROUTE_PLANNING = 0,
    AS_ROUTE_STORYBOARD,
    AS_ROUTE_METADATA,
    AS_ROUTE_SPEECH,
    AS_ROUTE_IMAGE,
    AS_ROUTE_VISION,
    AS_ROUTE_COUNT
} AsRouteCapability;

typedef struct {
    gchar *id;
    gchar *name;
    AsProviderType type;
    gchar *base_url;
    AsAuthType auth_type;
    gchar *api_version;
    gchar *text_model;
    gchar *speech_model;
    gchar *image_model;
    gchar *speech_voice;
    guint capabilities;
    gint timeout_ms;
    gint max_retries;
    gboolean enabled;
    gboolean insecure_http_allowed;
    /* Optional validated endpoint overrides (path only, never a new host). */
    gchar *chat_path;
    gchar *models_path;
    gchar *speech_path;
    gchar *image_path;
    gchar *extra_json;
} AsProviderConfig;

typedef struct {
    gchar *provider_id;
    gchar *model_id;
} AsCapabilityRoute;

typedef struct {
    gchar *id;
    gchar *name;
    gchar *context_length;
    gchar *description;
} AsModelInfo;

typedef struct {
    gchar *text;
    gchar *finish_reason;
    gchar *usage_json;       /* provider-reported usage, redacted (no secrets) */
} AsTextResult;

typedef struct {
    GBytes *data;
    gchar *mime_type;
    gchar *extension;        /* without dot, e.g. "wav" */
    gchar *voice;
    gchar *usage_json;
} AsSpeechResult;

typedef struct {
    GBytes *data;
    gchar *mime_type;
    gchar *extension;
    gchar *usage_json;
} AsImageResult;

/* ---- lifecycle ---- */
void as_provider_config_init(AsProviderConfig *cfg);
void as_provider_config_clear(AsProviderConfig *cfg);
void as_capability_route_free(AsCapabilityRoute *route);
AsProviderConfig *as_provider_config_new(void);
void as_provider_config_free(AsProviderConfig *cfg);
AsProviderConfig *as_provider_config_copy(const AsProviderConfig *cfg);
gboolean as_provider_config_validate(const AsProviderConfig *cfg, GError **error);

const gchar *as_provider_type_to_string(AsProviderType type);
AsProviderType as_provider_type_from_string(const gchar *s);
const gchar *as_provider_type_display(AsProviderType type);
const gchar *as_auth_type_to_string(AsAuthType type);
AsAuthType as_auth_type_from_string(const gchar *s);

const gchar *as_route_to_string(AsRouteCapability cap);
AsRouteCapability as_route_from_string(const gchar *s);
guint as_route_required_capability(AsRouteCapability cap);
const gchar *as_route_display_name(AsRouteCapability cap);

/* ---- registry ---- */
typedef struct _AsProviderRegistry AsProviderRegistry;

AsProviderRegistry *as_provider_registry_new(void);
void as_provider_registry_free(AsProviderRegistry *registry);
gboolean as_provider_registry_reload(AsProviderRegistry *registry, gpointer database,
                                     GError **error);
GPtrArray *as_provider_registry_list(AsProviderRegistry *registry); /* borrowed configs */
AsProviderConfig *as_provider_registry_find(AsProviderRegistry *registry,
                                            const gchar *id); /* borrowed */

/* Route management (persisted by storage through the registry). */
gboolean as_provider_registry_set_route(AsProviderRegistry *registry,
                                        AsRouteCapability cap,
                                        const gchar *provider_id,
                                        const gchar *model_id,
                                        GError **error);
AsCapabilityRoute *as_provider_registry_get_route(AsProviderRegistry *registry,
                                                  AsRouteCapability cap); /* borrowed */
/* Resolves a route and verifies the provider is enabled and capable. */
AsProviderConfig *as_provider_registry_resolve(AsProviderRegistry *registry,
                                               AsRouteCapability cap,
                                               const gchar **model_out,
                                               GError **error);

/* ---- adapters ---- */
gboolean as_provider_generate_text(const AsProviderConfig *cfg,
                                   const gchar *api_key,
                                   const gchar *model,
                                   const gchar *system_prompt,
                                   const gchar *user_prompt,
                                   gboolean structured,
                                   gdouble temperature,
                                   AsTextResult *result,
                                   GCancellable *cancellable,
                                   GError **error);

gboolean as_provider_generate_speech(const AsProviderConfig *cfg,
                                     const gchar *api_key,
                                     const gchar *model,
                                     const gchar *voice,
                                     const gchar *text,
                                     AsSpeechResult *result,
                                     GCancellable *cancellable,
                                     GError **error);

gboolean as_provider_generate_image(const AsProviderConfig *cfg,
                                    const gchar *api_key,
                                    const gchar *model,
                                    const gchar *prompt,
                                    const gchar *size,
                                    AsImageResult *result,
                                    GCancellable *cancellable,
                                    GError **error);

GPtrArray *as_provider_list_models(const AsProviderConfig *cfg,
                                   const gchar *api_key,
                                   GCancellable *cancellable,
                                   GError **error); /* AsModelInfo* */

gboolean as_provider_test_connection(const AsProviderConfig *cfg,
                                     const gchar *api_key,
                                     gchar **message,
                                     GCancellable *cancellable,
                                     GError **error);

void as_text_result_clear(AsTextResult *result);
void as_speech_result_clear(AsSpeechResult *result);
void as_image_result_clear(AsImageResult *result);
void as_model_info_free(AsModelInfo *info);

/* Shared helper for provider adapters: posts JSON, retries transient failures,
 * maps HTTP/provider errors to AsErrorCode and returns a parsed root object. */
typedef struct {
    gboolean retry_auth_failures; /* never true in production adapters */
} AsProviderHttpOptions;

JsonObject *as_provider_http_post_json(const AsProviderConfig *cfg,
                                       const gchar *api_key,
                                       const gchar *url,
                                       const gchar *json_body,
                                       gboolean key_in_query,
                                       GCancellable *cancellable,
                                       GError **error);
JsonObject *as_provider_http_get_json(const AsProviderConfig *cfg,
                                      const gchar *api_key,
                                      const gchar *url,
                                      gboolean key_in_query,
                                      GCancellable *cancellable,
                                      GError **error);
gchar *as_provider_build_url(const AsProviderConfig *cfg, const gchar *path,
                             const gchar *api_key, gboolean key_in_query);
void as_provider_apply_auth_headers(const AsProviderConfig *cfg,
                                    const gchar *api_key,
                                    GPtrArray *headers);
GError *as_provider_error_from_http(glong status, const gchar *body,
                                    const gchar *provider_name);
gchar *as_provider_extract_error_message(JsonObject *root);
