/* provider_registry.c — provider catalog, route resolution and dispatch. */
#include "as_provider.h"
#include "as_storage.h"
#include "as_core.h"
#include "provider_adapters.h"

#include <string.h>

struct _AsProviderRegistry {
    GPtrArray *providers;   /* AsProviderConfig* */
    GHashTable *routes;     /* gchar* -> AsCapabilityRoute* */
    gpointer database;      /* borrowed AsDatabase* */
};

static void route_table_destroy(GHashTable *table)
{
    if (table)
        g_hash_table_destroy(table);
}

static GHashTable *route_table_new(void)
{
    return g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                 (GDestroyNotify) as_capability_route_free);
}

AsProviderRegistry *as_provider_registry_new(void)
{
    AsProviderRegistry *registry = g_new0(AsProviderRegistry, 1);
    registry->providers = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_provider_config_free);
    registry->routes = route_table_new();
    return registry;
}

void as_provider_registry_free(AsProviderRegistry *registry)
{
    if (!registry)
        return;
    g_ptr_array_unref(registry->providers);
    route_table_destroy(registry->routes);
    g_free(registry);
}

gboolean as_provider_registry_reload(AsProviderRegistry *registry,
                                     gpointer database, GError **error)
{
    g_return_val_if_fail(registry != NULL, FALSE);
    registry->database = database;
    if (!database)
        return TRUE;

    GPtrArray *providers = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_provider_config_free);
    if (!as_provider_store_list((AsDatabase *) database, providers, error)) {
        g_ptr_array_unref(providers);
        return FALSE;
    }
    GHashTable *routes = route_table_new();
    if (!as_provider_store_load_routes((AsDatabase *) database, routes, error)) {
        g_ptr_array_unref(providers);
        route_table_destroy(routes);
        return FALSE;
    }
    g_ptr_array_unref(registry->providers);
    registry->providers = providers;
    route_table_destroy(registry->routes);
    registry->routes = routes;
    return TRUE;
}

GPtrArray *as_provider_registry_list(AsProviderRegistry *registry)
{
    return registry ? registry->providers : NULL;
}

AsProviderConfig *as_provider_registry_find(AsProviderRegistry *registry,
                                            const gchar *id)
{
    if (!registry || !id)
        return NULL;
    for (guint i = 0; i < registry->providers->len; i++) {
        AsProviderConfig *cfg = g_ptr_array_index(registry->providers, i);
        if (g_strcmp0(cfg->id, id) == 0)
            return cfg;
    }
    return NULL;
}

gboolean as_provider_registry_set_route(AsProviderRegistry *registry,
                                        AsRouteCapability cap,
                                        const gchar *provider_id,
                                        const gchar *model_id, GError **error)
{
    g_return_val_if_fail(registry != NULL, FALSE);
    if (cap < 0 || cap >= AS_ROUTE_COUNT) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Unknown capability route.");
        return FALSE;
    }
    if (!provider_id || !*provider_id) {
        if (registry->database &&
            !as_provider_store_clear_route((AsDatabase *) registry->database,
                                           cap, error))
            return FALSE;
        g_hash_table_remove(registry->routes, as_route_to_string(cap));
        return TRUE;
    }
    AsProviderConfig *cfg = as_provider_registry_find(registry, provider_id);
    if (!cfg) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND,
                    "Provider '%s' is not configured.", provider_id);
        return FALSE;
    }
    guint required = as_route_required_capability(cap);
    if (!(cfg->capabilities & required)) {
        g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED,
                    "'%s' does not provide the %s capability.", cfg->name,
                    as_route_display_name(cap));
        return FALSE;
    }
    if (registry->database &&
        !as_provider_store_save_route((AsDatabase *) registry->database, cap,
                                      provider_id, model_id, error))
        return FALSE;
    AsCapabilityRoute *route = g_new0(AsCapabilityRoute, 1);
    route->provider_id = g_strdup(provider_id);
    route->model_id = g_strdup(model_id ? model_id : "");
    g_hash_table_replace(registry->routes, g_strdup(as_route_to_string(cap)),
                         route);
    return TRUE;
}

AsCapabilityRoute *as_provider_registry_get_route(AsProviderRegistry *registry,
                                                  AsRouteCapability cap)
{
    if (!registry || cap < 0 || cap >= AS_ROUTE_COUNT)
        return NULL;
    return g_hash_table_lookup(registry->routes, as_route_to_string(cap));
}

AsProviderConfig *as_provider_registry_resolve(AsProviderRegistry *registry,
                                               AsRouteCapability cap,
                                               const gchar **model_out,
                                               GError **error)
{
    if (model_out)
        *model_out = NULL;
    if (!registry) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG, "No provider registry.");
        return NULL;
    }
    AsCapabilityRoute *route = as_provider_registry_get_route(registry, cap);
    if (!route) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "No provider is assigned to %s. Open API Providers to "
                    "configure a route.", as_route_display_name(cap));
        return NULL;
    }
    AsProviderConfig *cfg = as_provider_registry_find(registry, route->provider_id);
    if (!cfg) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "The provider assigned to %s no longer exists.",
                    as_route_display_name(cap));
        return NULL;
    }
    if (!cfg->enabled) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "The provider '%s' is disabled.", cfg->name);
        return NULL;
    }
    guint required = as_route_required_capability(cap);
    if (!(cfg->capabilities & required)) {
        g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED,
                    "'%s' no longer supports %s.", cfg->name,
                    as_route_display_name(cap));
        return NULL;
    }
    const gchar *model = route->model_id;
    if (!model || !*model) {
        if (cap == AS_ROUTE_SPEECH)
            model = cfg->speech_model;
        else if (cap == AS_ROUTE_IMAGE)
            model = cfg->image_model;
        else
            model = cfg->text_model;
    }
    if (!model || !*model) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "No model is configured for %s on '%s'. Enter a model id "
                    "in API Providers.", as_route_display_name(cap), cfg->name);
        return NULL;
    }
    if (model_out)
        *model_out = model;
    return cfg;
}

/* ---------------- dispatch ---------------- */

gboolean as_provider_generate_text(const AsProviderConfig *cfg,
                                   const gchar *api_key, const gchar *model,
                                   const gchar *system_prompt,
                                   const gchar *user_prompt, gboolean structured,
                                   gdouble temperature, AsTextResult *result,
                                   GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, FALSE);
    if (cfg->type == AS_PROVIDER_GEMINI)
        return as_gemini_generate_text(cfg, api_key, model, system_prompt,
                                       user_prompt, structured, temperature,
                                       result, cancellable, error);
    if (cfg->type == AS_PROVIDER_OPENROUTER)
        return as_openrouter_generate_text(cfg, api_key, model, system_prompt,
                                           user_prompt, structured, temperature,
                                           result, cancellable, error);
    return as_openai_generate_text(cfg, api_key, model, system_prompt,
                                   user_prompt, structured, temperature, result,
                                   cancellable, error);
}

gboolean as_provider_generate_speech(const AsProviderConfig *cfg,
                                     const gchar *api_key, const gchar *model,
                                     const gchar *voice, const gchar *text,
                                     AsSpeechResult *result,
                                     GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, FALSE);
    if (!(cfg->capabilities & AS_CAP_SPEECH)) {
        g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED,
                    "'%s' does not provide speech synthesis.", cfg->name);
        return FALSE;
    }
    if (cfg->type == AS_PROVIDER_GEMINI)
        return as_gemini_generate_speech(cfg, api_key, model, voice, text, result,
                                         cancellable, error);
    if (cfg->type == AS_PROVIDER_OPENAI_COMPATIBLE)
        return as_openai_generate_speech(cfg, api_key, model, voice, text, result,
                                         cancellable, error);
    g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED,
                "'%s' has no speech adapter. Choose a speech-capable provider.",
                cfg->name);
    return FALSE;
}

gboolean as_provider_generate_image(const AsProviderConfig *cfg,
                                    const gchar *api_key, const gchar *model,
                                    const gchar *prompt, const gchar *size,
                                    AsImageResult *result,
                                    GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, FALSE);
    if (!(cfg->capabilities & AS_CAP_IMAGE)) {
        g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED,
                    "'%s' does not provide image generation.", cfg->name);
        return FALSE;
    }
    if (cfg->type == AS_PROVIDER_GEMINI)
        return as_gemini_generate_image(cfg, api_key, model, prompt, size, result,
                                        cancellable, error);
    if (cfg->type == AS_PROVIDER_OPENAI_COMPATIBLE)
        return as_openai_generate_image(cfg, api_key, model, prompt, size, result,
                                        cancellable, error);
    g_set_error(error, AS_ERROR, AS_ERROR_UNSUPPORTED,
                "'%s' has no image adapter. Choose an image-capable provider.",
                cfg->name);
    return FALSE;
}

GPtrArray *as_provider_list_models(const AsProviderConfig *cfg,
                                   const gchar *api_key,
                                   GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, NULL);
    if (cfg->type == AS_PROVIDER_GEMINI)
        return as_gemini_list_models(cfg, api_key, cancellable, error);
    if (cfg->type == AS_PROVIDER_OPENROUTER)
        return as_openrouter_list_models(cfg, api_key, cancellable, error);
    return as_openai_list_models(cfg, api_key, cancellable, error);
}

gboolean as_provider_test_connection(const AsProviderConfig *cfg,
                                     const gchar *api_key, gchar **message,
                                     GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, FALSE);
    if (cfg->type == AS_PROVIDER_GEMINI)
        return as_gemini_test_connection(cfg, api_key, message, cancellable,
                                         error);
    if (cfg->type == AS_PROVIDER_OPENROUTER)
        return as_openrouter_test_connection(cfg, api_key, message, cancellable,
                                             error);
    return as_openai_test_connection(cfg, api_key, message, cancellable, error);
}
