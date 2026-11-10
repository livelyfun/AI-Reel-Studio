/* openrouter.c — OpenRouter text/structured adapter.
 *
 * OpenRouter is treated as a text/structured provider only. Speech and image
 * capabilities are never assumed: the user must configure a provider that
 * explicitly advertises those endpoints.
 */
#include "provider_adapters.h"
#include "as_provider.h"
#include "as_core.h"
#include "as_json.h"

#include <string.h>

static gchar *chat_url(const AsProviderConfig *cfg)
{
    const gchar *path = (cfg->chat_path && *cfg->chat_path)
                            ? cfg->chat_path
                            : "/chat/completions";
    return as_provider_build_url(cfg, path, NULL, FALSE);
}

static gchar *models_url(const AsProviderConfig *cfg)
{
    const gchar *path = (cfg->models_path && *cfg->models_path)
                            ? cfg->models_path
                            : "/models";
    return as_provider_build_url(cfg, path, NULL, FALSE);
}

static gchar *extract_content(JsonObject *message)
{
    JsonNode *node = json_object_get_member(message, "content");
    if (!node)
        return NULL;
    if (JSON_NODE_HOLDS_VALUE(node) &&
        json_node_get_value_type(node) == G_TYPE_STRING)
        return g_strdup(json_node_get_string(node));
    if (JSON_NODE_HOLDS_ARRAY(node)) {
        GString *out = g_string_new(NULL);
        JsonArray *parts = json_node_get_array(node);
        for (guint i = 0; i < json_array_get_length(parts); i++) {
            JsonNode *part_node = json_array_get_element(parts, i);
            if (!JSON_NODE_HOLDS_OBJECT(part_node))
                continue;
            gchar *text = as_json_get_string(json_node_get_object(part_node),
                                             "text");
            if (text && *text) {
                if (out->len)
                    g_string_append_c(out, '\n');
                g_string_append(out, text);
            }
            g_free(text);
        }
        return g_string_free(out, FALSE);
    }
    return NULL;
}

gboolean as_openrouter_generate_text(const AsProviderConfig *cfg,
                                     const gchar *api_key, const gchar *model,
                                     const gchar *system_prompt,
                                     const gchar *user_prompt, gboolean structured,
                                     gdouble temperature, AsTextResult *result,
                                     GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg && result, FALSE);
    memset(result, 0, sizeof(*result));
    if (!api_key || !*api_key) {
        g_set_error(error, AS_ERROR, AS_ERROR_AUTH,
                    "No API key is configured for '%s'.", cfg->name);
        return FALSE;
    }
    if (!model || !*model) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG,
                    "No text model is configured for '%s'.", cfg->name);
        return FALSE;
    }

    JsonObject *root = json_object_new();
    json_object_set_string_member(root, "model", model);
    JsonArray *messages = json_array_new();
    if (system_prompt && *system_prompt) {
        JsonObject *system = json_object_new();
        json_object_set_string_member(system, "role", "system");
        json_object_set_string_member(system, "content", system_prompt);
        json_array_add_object_element(messages, system);
    }
    JsonObject *user = json_object_new();
    json_object_set_string_member(user, "role", "user");
    json_object_set_string_member(user, "content", user_prompt ? user_prompt : "");
    json_array_add_object_element(messages, user);
    json_object_set_array_member(root, "messages", messages);
    json_object_set_double_member(root, "temperature", temperature);
    if (structured) {
        JsonObject *format = json_object_new();
        json_object_set_string_member(format, "type", "json_object");
        json_object_set_object_member(root, "response_format", format);
    }

    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    gchar *body = as_json_to_string(node, FALSE);
    json_node_free(node);
    gchar *url = chat_url(cfg);
    JsonObject *response = as_provider_http_post_json(cfg, api_key, url, body,
                                                      FALSE, cancellable, error);
    g_free(url);
    g_free(body);
    if (!response)
        return FALSE;

    JsonArray *choices = as_json_get_array(response, "choices");
    gboolean ok = FALSE;
    if (choices && json_array_get_length(choices) > 0) {
        JsonNode *first = json_array_get_element(choices, 0);
        if (JSON_NODE_HOLDS_OBJECT(first)) {
            JsonObject *choice = json_node_get_object(first);
            result->finish_reason = as_json_get_string(choice, "finish_reason");
            JsonObject *message = as_json_get_object(choice, "message");
            if (message)
                result->text = extract_content(message);
            ok = result->text && *result->text;
        }
    }
    JsonNode *usage = json_object_get_member(response, "usage");
    if (usage)
        result->usage_json = as_json_to_string(usage, FALSE);
    json_object_unref(response);
    if (!ok) {
        g_free(result->text);
        result->text = NULL;
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "%s returned no text content.", cfg->name);
        return FALSE;
    }
    return TRUE;
}

GPtrArray *as_openrouter_list_models(const AsProviderConfig *cfg,
                                     const gchar *api_key,
                                     GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, NULL);
    if (!api_key || !*api_key) {
        g_set_error(error, AS_ERROR, AS_ERROR_AUTH,
                    "Enter an API key to list models.");
        return NULL;
    }
    gchar *url = models_url(cfg);
    JsonObject *root = as_provider_http_get_json(cfg, api_key, url, FALSE,
                                                 cancellable, error);
    g_free(url);
    if (!root)
        return NULL;
    GPtrArray *list = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_model_info_free);
    JsonArray *data = as_json_get_array(root, "data");
    for (guint i = 0; data && i < json_array_get_length(data); i++) {
        JsonNode *node = json_array_get_element(data, i);
        if (!JSON_NODE_HOLDS_OBJECT(node))
            continue;
        JsonObject *obj = json_node_get_object(node);
        AsModelInfo *info = g_new0(AsModelInfo, 1);
        info->id = as_json_get_string(obj, "id");
        info->name = as_json_get_string(obj, "name");
        if (!info->name)
            info->name = g_strdup(info->id);
        gint64 context = as_json_get_int(obj, "context_length", 0);
        if (context > 0)
            info->context_length = g_strdup_printf("%" G_GINT64_FORMAT, context);
        info->description = as_json_get_string(obj, "description");
        if (info->id)
            g_ptr_array_add(list, info);
        else
            as_model_info_free(info);
    }
    json_object_unref(root);
    return list;
}

gboolean as_openrouter_test_connection(const AsProviderConfig *cfg,
                                       const gchar *api_key, gchar **message,
                                       GCancellable *cancellable, GError **error)
{
    if (message)
        *message = NULL;
    GPtrArray *models = as_openrouter_list_models(cfg, api_key, cancellable, error);
    if (!models)
        return FALSE;
    if (message)
        *message = g_strdup_printf("Connected to %s. %u models available.",
                                   cfg->name, models->len);
    g_ptr_array_unref(models);
    return TRUE;
}
