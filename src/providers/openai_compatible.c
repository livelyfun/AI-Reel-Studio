/* openai_compatible.c — generic OpenAI-compatible adapter.
 *
 * Speech and image calls are opt-in through the provider capability mask; the
 * adapter never guesses that an endpoint exists. Users supply model names and
 * optional path overrides.
 */
#include "provider_adapters.h"
#include "as_provider.h"
#include "as_core.h"
#include "as_json.h"

#include <string.h>

static gchar *endpoint(const AsProviderConfig *cfg, const gchar *override,
                       const gchar *fallback)
{
    const gchar *path = (override && *override) ? override : fallback;
    return as_provider_build_url(cfg, path, NULL, FALSE);
}

gboolean as_openai_generate_text(const AsProviderConfig *cfg,
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

    gchar *url = endpoint(cfg, cfg->chat_path, "/chat/completions");
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
                result->text = as_json_get_string(message, "content");
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

gboolean as_openai_generate_speech(const AsProviderConfig *cfg,
                                   const gchar *api_key, const gchar *model,
                                   const gchar *voice, const gchar *text,
                                   AsSpeechResult *result,
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
                    "No speech model is configured for '%s'.", cfg->name);
        return FALSE;
    }
    const gchar *use_voice = (voice && *voice)
                                 ? voice
                                 : ((cfg->speech_voice && *cfg->speech_voice)
                                        ? cfg->speech_voice
                                        : "alloy");
    JsonObject *root = json_object_new();
    json_object_set_string_member(root, "model", model);
    json_object_set_string_member(root, "input", text ? text : "");
    json_object_set_string_member(root, "voice", use_voice);
    json_object_set_string_member(root, "response_format", "wav");
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    gchar *body = as_json_to_string(node, FALSE);
    json_node_free(node);

    gchar *url = endpoint(cfg, cfg->speech_path, "/audio/speech");
    gchar *content_type = NULL;
    GBytes *bytes = as_provider_http_post_bytes(cfg, api_key, url, body,
                                                "audio/*", &content_type,
                                                cancellable, error);
    g_free(url);
    g_free(body);
    if (!bytes) {
        g_free(content_type);
        return FALSE;
    }
    result->data = bytes;
    result->voice = g_strdup(use_voice);
    if (content_type && *content_type) {
        result->mime_type = g_strdup(content_type);
        gchar *lower = g_ascii_strdown(content_type, -1);
        if (strstr(lower, "mpeg") || strstr(lower, "mp3"))
            result->extension = g_strdup("mp3");
        else if (strstr(lower, "ogg"))
            result->extension = g_strdup("ogg");
        else if (strstr(lower, "flac"))
            result->extension = g_strdup("flac");
        else if (strstr(lower, "wav") || strstr(lower, "x-wav"))
            result->extension = g_strdup("wav");
        else
            result->extension = g_strdup("bin");
        g_free(lower);
    } else {
        result->mime_type = g_strdup("audio/wav");
        result->extension = g_strdup("wav");
    }
    g_free(content_type);
    return TRUE;
}

gboolean as_openai_generate_image(const AsProviderConfig *cfg,
                                  const gchar *api_key, const gchar *model,
                                  const gchar *prompt, const gchar *size,
                                  AsImageResult *result,
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
                    "No image model is configured for '%s'.", cfg->name);
        return FALSE;
    }
    JsonObject *root = json_object_new();
    json_object_set_string_member(root, "model", model);
    json_object_set_string_member(root, "prompt", prompt ? prompt : "");
    json_object_set_int_member(root, "n", 1);
    json_object_set_string_member(root, "size",
                                  (size && *size) ? size : "1024x1024");
    json_object_set_string_member(root, "response_format", "b64_json");
    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    gchar *body = as_json_to_string(node, FALSE);
    json_node_free(node);

    gchar *url = endpoint(cfg, cfg->image_path, "/images/generations");
    JsonObject *response = as_provider_http_post_json(cfg, api_key, url, body,
                                                      FALSE, cancellable, error);
    g_free(url);
    g_free(body);
    if (!response)
        return FALSE;

    JsonArray *data = as_json_get_array(response, "data");
    if (data && json_array_get_length(data) > 0) {
        JsonNode *first = json_array_get_element(data, 0);
        if (JSON_NODE_HOLDS_OBJECT(first)) {
            JsonObject *obj = json_node_get_object(first);
            gchar *b64 = as_json_get_string(obj, "b64_json");
            if (b64 && *b64) {
                gsize length = 0;
                guchar *decoded = g_base64_decode(b64, &length);
                if (decoded && length)
                    result->data = g_bytes_new_take(decoded, length);
                else
                    g_free(decoded);
                g_free(b64);
                result->mime_type = g_strdup("image/png");
                result->extension = g_strdup("png");
            } else {
                gchar *remote = as_json_get_string(obj, "url");
                if (remote && *remote) {
                    gchar *content_type = NULL;
                    result->data = as_provider_http_get_bytes(cfg, api_key, remote,
                                                              "image/*",
                                                              &content_type,
                                                              cancellable, error);
                    if (result->data) {
                        result->mime_type = content_type
                                                ? g_strdup(content_type)
                                                : g_strdup("image/png");
                        gchar *lower = g_ascii_strdown(result->mime_type, -1);
                        if (strstr(lower, "jpeg") || strstr(lower, "jpg"))
                            result->extension = g_strdup("jpg");
                        else if (strstr(lower, "webp"))
                            result->extension = g_strdup("webp");
                        else
                            result->extension = g_strdup("png");
                        g_free(lower);
                    }
                    g_free(content_type);
                }
                g_free(remote);
            }
        }
    }
    json_object_unref(response);
    if (!result->data) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "%s returned no image data.", cfg->name);
        return FALSE;
    }
    return TRUE;
}

static GPtrArray *list_models_common(const AsProviderConfig *cfg,
                                     const gchar *api_key,
                                     GCancellable *cancellable, GError **error)
{
    if (!api_key || !*api_key) {
        g_set_error(error, AS_ERROR, AS_ERROR_AUTH,
                    "Enter an API key to list models.");
        return NULL;
    }
    gchar *url = endpoint(cfg, cfg->models_path, "/models");
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

GPtrArray *as_openai_list_models(const AsProviderConfig *cfg,
                                 const gchar *api_key,
                                 GCancellable *cancellable, GError **error)
{
    return list_models_common(cfg, api_key, cancellable, error);
}

gboolean as_openai_test_connection(const AsProviderConfig *cfg,
                                   const gchar *api_key, gchar **message,
                                   GCancellable *cancellable, GError **error)
{
    if (message)
        *message = NULL;
    GPtrArray *models = as_openai_list_models(cfg, api_key, cancellable, error);
    if (!models)
        return FALSE;
    if (message)
        *message = g_strdup_printf("Connected to %s. %u models available.",
                                   cfg->name, models->len);
    g_ptr_array_unref(models);
    return TRUE;
}
