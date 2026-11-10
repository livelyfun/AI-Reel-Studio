/* provider_common.c — shared provider configuration, routing metadata and HTTP.
 *
 * Adapters never hardcode credentials and never silently fall back to another
 * provider. Every request is bounded by the configured timeout, validated URL
 * and response size, and retries only transport/rate-limit/5xx failures.
 */
#include "as_provider.h"
#include "as_core.h"
#include "as_json.h"

#include <string.h>

static void replace_str(gchar **dst, const gchar *src)
{
    g_free(*dst);
    *dst = g_strdup(src ? src : "");
}

void as_provider_config_init(AsProviderConfig *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->type = AS_PROVIDER_OPENAI_COMPATIBLE;
    cfg->auth_type = AS_AUTH_BEARER;
    cfg->capabilities = AS_CAP_TEXT | AS_CAP_STRUCTURED;
    cfg->timeout_ms = 60000;
    cfg->max_retries = 2;
    cfg->enabled = TRUE;
    cfg->extra_json = g_strdup("{}");
}

void as_provider_config_clear(AsProviderConfig *cfg)
{
    if (!cfg)
        return;
    g_free(cfg->id);
    g_free(cfg->name);
    g_free(cfg->base_url);
    g_free(cfg->api_version);
    g_free(cfg->text_model);
    g_free(cfg->speech_model);
    g_free(cfg->image_model);
    g_free(cfg->speech_voice);
    g_free(cfg->chat_path);
    g_free(cfg->models_path);
    g_free(cfg->speech_path);
    g_free(cfg->image_path);
    g_free(cfg->extra_json);
    memset(cfg, 0, sizeof(*cfg));
}

AsProviderConfig *as_provider_config_new(void)
{
    AsProviderConfig *cfg = g_new0(AsProviderConfig, 1);
    as_provider_config_init(cfg);
    return cfg;
}

void as_provider_config_free(AsProviderConfig *cfg)
{
    if (!cfg)
        return;
    as_provider_config_clear(cfg);
    g_free(cfg);
}

AsProviderConfig *as_provider_config_copy(const AsProviderConfig *cfg)
{
    if (!cfg)
        return NULL;
    AsProviderConfig *copy = g_new0(AsProviderConfig, 1);
    as_provider_config_init(copy);
    replace_str(&copy->id, cfg->id);
    replace_str(&copy->name, cfg->name);
    replace_str(&copy->base_url, cfg->base_url);
    replace_str(&copy->api_version, cfg->api_version);
    replace_str(&copy->text_model, cfg->text_model);
    replace_str(&copy->speech_model, cfg->speech_model);
    replace_str(&copy->image_model, cfg->image_model);
    replace_str(&copy->speech_voice, cfg->speech_voice);
    replace_str(&copy->chat_path, cfg->chat_path);
    replace_str(&copy->models_path, cfg->models_path);
    replace_str(&copy->speech_path, cfg->speech_path);
    replace_str(&copy->image_path, cfg->image_path);
    replace_str(&copy->extra_json, cfg->extra_json);
    copy->type = cfg->type;
    copy->auth_type = cfg->auth_type;
    copy->capabilities = cfg->capabilities;
    copy->timeout_ms = cfg->timeout_ms;
    copy->max_retries = cfg->max_retries;
    copy->enabled = cfg->enabled;
    copy->insecure_http_allowed = cfg->insecure_http_allowed;
    return copy;
}

void as_capability_route_free(AsCapabilityRoute *route)
{
    if (!route)
        return;
    g_free(route->provider_id);
    g_free(route->model_id);
    g_free(route);
}

static gboolean valid_path_override(const gchar *path)
{
    if (!path || !*path)
        return TRUE;
    if (path[0] != '/')
        return FALSE;
    if (strstr(path, "://") != NULL || strstr(path, "..") != NULL)
        return FALSE;
    return TRUE;
}

gboolean as_provider_config_validate(const AsProviderConfig *cfg, GError **error)
{
    if (!cfg) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION, "Provider is missing.");
        return FALSE;
    }
    if (!cfg->id || !*cfg->id) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The provider needs an identifier.");
        return FALSE;
    }
    if (!cfg->name || !*cfg->name) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The provider needs a display name.");
        return FALSE;
    }
    if (!as_url_validate(cfg->base_url, cfg->insecure_http_allowed, error))
        return FALSE;
    if (cfg->type != AS_PROVIDER_GEMINI && cfg->type != AS_PROVIDER_OPENROUTER &&
        cfg->type != AS_PROVIDER_OPENAI_COMPATIBLE) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Unknown provider type.");
        return FALSE;
    }
    if (!cfg->capabilities || (cfg->capabilities & ~AS_CAP_ALL)) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The provider has no valid capabilities.");
        return FALSE;
    }
    if (cfg->timeout_ms < 5000 || cfg->timeout_ms > 600000) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The timeout must be between 5 and 600 seconds.");
        return FALSE;
    }
    if (cfg->max_retries < 0 || cfg->max_retries > 8) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Retries must be between 0 and 8.");
        return FALSE;
    }
    if (!valid_path_override(cfg->chat_path) ||
        !valid_path_override(cfg->models_path) ||
        !valid_path_override(cfg->speech_path) ||
        !valid_path_override(cfg->image_path)) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Endpoint overrides must be absolute paths on the configured "
                    "host and cannot contain '..' or a scheme.");
        return FALSE;
    }
    if (cfg->extra_json && *cfg->extra_json) {
        GError *local = NULL;
        JsonNode *node = as_json_parse(cfg->extra_json, -1, &local);
        if (!node) {
            g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                        "Provider extra JSON is invalid: %s", local->message);
            g_clear_error(&local);
            return FALSE;
        }
        gboolean object = JSON_NODE_HOLDS_OBJECT(node);
        json_node_free(node);
        if (!object) {
            g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                        "Provider extra JSON must be an object.");
            return FALSE;
        }
    }
    return TRUE;
}

const gchar *as_provider_type_to_string(AsProviderType type)
{
    switch (type) {
    case AS_PROVIDER_GEMINI: return "gemini";
    case AS_PROVIDER_OPENROUTER: return "openrouter";
    case AS_PROVIDER_OPENAI_COMPATIBLE: return "openai-compatible";
    }
    return "openai-compatible";
}

AsProviderType as_provider_type_from_string(const gchar *s)
{
    if (g_strcmp0(s, "gemini") == 0)
        return AS_PROVIDER_GEMINI;
    if (g_strcmp0(s, "openrouter") == 0)
        return AS_PROVIDER_OPENROUTER;
    return AS_PROVIDER_OPENAI_COMPATIBLE;
}

const gchar *as_provider_type_display(AsProviderType type)
{
    switch (type) {
    case AS_PROVIDER_GEMINI: return "Google Gemini";
    case AS_PROVIDER_OPENROUTER: return "OpenRouter";
    case AS_PROVIDER_OPENAI_COMPATIBLE: return "OpenAI-compatible";
    }
    return "OpenAI-compatible";
}

const gchar *as_auth_type_to_string(AsAuthType type)
{
    switch (type) {
    case AS_AUTH_BEARER: return "bearer";
    case AS_AUTH_X_API_KEY: return "x-api-key";
    case AS_AUTH_QUERY_KEY: return "query";
    case AS_AUTH_NONE: return "none";
    }
    return "bearer";
}

AsAuthType as_auth_type_from_string(const gchar *s)
{
    if (g_strcmp0(s, "x-api-key") == 0)
        return AS_AUTH_X_API_KEY;
    if (g_strcmp0(s, "query") == 0)
        return AS_AUTH_QUERY_KEY;
    if (g_strcmp0(s, "none") == 0)
        return AS_AUTH_NONE;
    return AS_AUTH_BEARER;
}

static const gchar *const route_names[AS_ROUTE_COUNT] = {
    "planning", "storyboard", "metadata", "speech", "image", "vision"
};

const gchar *as_route_to_string(AsRouteCapability cap)
{
    if (cap < 0 || cap >= AS_ROUTE_COUNT)
        return "planning";
    return route_names[cap];
}

AsRouteCapability as_route_from_string(const gchar *s)
{
    for (gint i = 0; i < AS_ROUTE_COUNT; i++)
        if (g_strcmp0(s, route_names[i]) == 0)
            return (AsRouteCapability) i;
    return AS_ROUTE_PLANNING;
}

guint as_route_required_capability(AsRouteCapability cap)
{
    switch (cap) {
    case AS_ROUTE_PLANNING:
    case AS_ROUTE_STORYBOARD:
    case AS_ROUTE_METADATA:
        return AS_CAP_STRUCTURED;
    case AS_ROUTE_SPEECH:
        return AS_CAP_SPEECH;
    case AS_ROUTE_IMAGE:
        return AS_CAP_IMAGE;
    case AS_ROUTE_VISION:
        return AS_CAP_VISION;
    default:
        return AS_CAP_TEXT;
    }
}

const gchar *as_route_display_name(AsRouteCapability cap)
{
    switch (cap) {
    case AS_ROUTE_PLANNING: return "Content planning";
    case AS_ROUTE_STORYBOARD: return "Storyboard and scenes";
    case AS_ROUTE_METADATA: return "Titles and metadata";
    case AS_ROUTE_SPEECH: return "Narration speech";
    case AS_ROUTE_IMAGE: return "Scene imagery";
    case AS_ROUTE_VISION: return "Image understanding";
    default: return "Capability";
    }
}

void as_text_result_clear(AsTextResult *result)
{
    if (!result)
        return;
    g_free(result->text);
    g_free(result->finish_reason);
    g_free(result->usage_json);
    memset(result, 0, sizeof(*result));
}

void as_speech_result_clear(AsSpeechResult *result)
{
    if (!result)
        return;
    if (result->data)
        g_bytes_unref(result->data);
    g_free(result->mime_type);
    g_free(result->extension);
    g_free(result->voice);
    g_free(result->usage_json);
    memset(result, 0, sizeof(*result));
}

void as_image_result_clear(AsImageResult *result)
{
    if (!result)
        return;
    if (result->data)
        g_bytes_unref(result->data);
    g_free(result->mime_type);
    g_free(result->extension);
    g_free(result->usage_json);
    memset(result, 0, sizeof(*result));
}

void as_model_info_free(AsModelInfo *info)
{
    if (!info)
        return;
    g_free(info->id);
    g_free(info->name);
    g_free(info->context_length);
    g_free(info->description);
    g_free(info);
}

/* ---------------- URL and headers ---------------- */

gchar *as_provider_build_url(const AsProviderConfig *cfg, const gchar *path,
                             const gchar *api_key, gboolean key_in_query)
{
    if (!cfg || !cfg->base_url)
        return NULL;
    gchar *base = g_strdup(cfg->base_url);
    gsize len = strlen(base);
    while (len > 0 && base[len - 1] == '/')
        base[--len] = '\0';
    const gchar *use_path = path ? path : "";
    gchar *url;
    if (*use_path)
        url = g_strdup_printf("%s/%s", base, use_path[0] == '/' ? use_path + 1
                                                                 : use_path);
    else
        url = g_strdup(base);
    g_free(base);
    if (key_in_query && api_key && *api_key) {
        gchar *escaped = g_uri_escape_string(api_key, NULL, TRUE);
        gchar *with = g_strdup_printf("%s%ckey=%s", url,
                                      strchr(url, '?') ? '&' : '?', escaped);
        g_free(url);
        g_free(escaped);
        url = with;
    }
    return url;
}

void as_provider_apply_auth_headers(const AsProviderConfig *cfg,
                                    const gchar *api_key, GPtrArray *headers)
{
    if (!cfg || !headers)
        return;
    if (api_key && *api_key && cfg->auth_type != AS_AUTH_NONE &&
        cfg->auth_type != AS_AUTH_QUERY_KEY) {
        gchar *header;
        if (cfg->auth_type == AS_AUTH_BEARER)
            header = g_strdup_printf("Authorization: Bearer %s", api_key);
        else if (cfg->type == AS_PROVIDER_GEMINI)
            header = g_strdup_printf("x-goog-api-key: %s", api_key);
        else
            header = g_strdup_printf("x-api-key: %s", api_key);
        g_ptr_array_add(headers, header);
    }
    /* Optional static headers (for example OpenRouter attribution). Values
     * containing CR/LF are rejected to prevent header injection. */
    if (cfg->extra_json && *cfg->extra_json) {
        GError *local = NULL;
        JsonNode *node = as_json_parse(cfg->extra_json, -1, &local);
        if (node) {
            JsonObject *root = as_json_root_object(node);
            JsonObject *extra = root ? as_json_get_object(root, "headers") : NULL;
            if (extra) {
                GList *members = json_object_get_members(extra);
                for (GList *l = members; l; l = l->next) {
                    const gchar *name = l->data;
                    gchar *value = as_json_get_string(extra, name);
                    if (name && value && *value &&
                        !strchr(name, '\r') && !strchr(name, '\n') &&
                        !strchr(name, ':') && !strchr(value, '\r') &&
                        !strchr(value, '\n'))
                        g_ptr_array_add(headers,
                                        g_strdup_printf("%s: %s", name, value));
                    g_free(value);
                }
                g_list_free(members);
            }
            json_node_free(node);
        }
        g_clear_error(&local);
    }
}

/* ---------------- error mapping ---------------- */

gchar *as_provider_extract_error_message(JsonObject *root)
{
    if (!root)
        return NULL;
    JsonNode *error_node = json_object_get_member(root, "error");
    if (error_node) {
        if (JSON_NODE_HOLDS_OBJECT(error_node)) {
            JsonObject *error = json_node_get_object(error_node);
            gchar *message = as_json_get_string(error, "message");
            if (message && *message)
                return message;
            g_free(message);
        } else if (JSON_NODE_HOLDS_VALUE(error_node) &&
                   json_node_get_value_type(error_node) == G_TYPE_STRING) {
            return g_strdup(json_node_get_string(error_node));
        }
    }
    gchar *message = as_json_get_string(root, "message");
    if (message && *message)
        return message;
    g_free(message);
    JsonObject *detail = as_json_get_object(root, "detail");
    if (detail)
        return as_json_get_string(detail, "message");
    return NULL;
}

GError *as_provider_error_from_http(glong status, const gchar *body,
                                    const gchar *provider_name)
{
    gchar *provider_message = NULL;
    if (body && *body) {
        GError *local = NULL;
        JsonNode *node = as_json_parse(body, -1, &local);
        if (node) {
            JsonObject *root = as_json_root_object(node);
            if (root)
                provider_message = as_provider_extract_error_message(root);
            json_node_free(node);
        }
        g_clear_error(&local);
    }
    if (!provider_message) {
        gchar *trimmed = g_strstrip(g_strdup(body ? body : ""));
        if (*trimmed)
            provider_message = g_strndup(trimmed, 600);
        g_free(trimmed);
    }
    if (!provider_message)
        provider_message = g_strdup("No error details were returned.");
    gchar *redacted = as_redact(provider_message);
    g_free(provider_message);

    AsErrorCode code;
    if (status == 401 || status == 403)
        code = AS_ERROR_AUTH;
    else if (status == 404)
        code = AS_ERROR_NOT_FOUND;
    else if (status == 413)
        code = AS_ERROR_TOO_LARGE;
    else if (status == 429)
        code = AS_ERROR_RATE_LIMIT;
    else if (status == 400 || status == 422)
        code = AS_ERROR_INVALID_ARGUMENT;
    else if (status >= 500)
        code = AS_ERROR_UNAVAILABLE;
    else
        code = AS_ERROR_HTTP_STATUS;

    GError *error = g_error_new(AS_ERROR, code, "%s request failed (HTTP %ld): %s",
                                provider_name && *provider_name ? provider_name
                                                               : "Provider",
                                status, redacted);
    g_free(redacted);
    return error;
}

/* ---------------- HTTP ---------------- */

static gboolean is_success(glong status)
{
    return status >= 200 && status < 300;
}

static void cancellable_sleep(GCancellable *cancellable, gint seconds)
{
    for (gint i = 0; i < seconds * 10; i++) {
        if (g_cancellable_is_cancelled(cancellable))
            return;
        g_usleep(100000);
    }
}

static AsHttpResponse *perform_request(const AsProviderConfig *cfg,
                                        const gchar *api_key, const gchar *url,
                                        const gchar *method, const gchar *body,
                                        const gchar *accept,
                                        GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, NULL);
    if (g_cancellable_set_error_if_cancelled(cancellable, error))
        return NULL;
    if (!as_url_validate(url, cfg->insecure_http_allowed, error))
        return NULL;

    GPtrArray *headers = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_add(headers,
                    g_strdup_printf("Accept: %s",
                                    accept ? accept : "application/json"));
    if (body)
        g_ptr_array_add(headers, g_strdup("Content-Type: application/json"));
    as_provider_apply_auth_headers(cfg, api_key, headers);
    g_ptr_array_add(headers, NULL);

    AsHttpClient *client = as_http_client_new(32u * 1024u * 1024u,
                                              cfg->timeout_ms,
                                              cfg->insecure_http_allowed);
    if (!client) {
        g_ptr_array_unref(headers);
        g_set_error(error, AS_ERROR, AS_ERROR_NETWORK,
                    "Could not initialize the HTTP client.");
        return NULL;
    }

    AsHttpResponse *success = NULL;
    gint attempt = 0;
    gint max_attempts = cfg->max_retries + 1;
    while (attempt < max_attempts) {
        attempt++;
        if (g_cancellable_set_error_if_cancelled(cancellable, error))
            break;
        AsHttpResponse *response = NULL;
        GError *local = NULL;
        const gchar *const *header_array = (const gchar *const *) headers->pdata;
        gboolean ok;
        if (g_strcmp0(method, "GET") == 0)
            ok = as_http_get(client, url, header_array, cancellable, &response,
                             &local);
        else
            ok = as_http_post(client, url, header_array, body,
                              body ? strlen(body) : 0, cancellable, &response,
                              &local);
        if (!ok) {
            gboolean retry = local && as_error_is_retryable(local) &&
                             attempt < max_attempts;
            if (retry) {
                g_clear_error(&local);
                cancellable_sleep(cancellable, 1 << (attempt - 1));
                continue;
            }
            g_propagate_error(error, local);
            break;
        }

        if (is_success(response->status)) {
            success = response;
            break;
        }

        gboolean retry = as_http_status_is_retryable(response->status) &&
                         attempt < max_attempts;
        if (retry) {
            gchar *seconds = as_http_retry_after_to_seconds(response->retry_after);
            gint delay = seconds ? atoi(seconds) : (1 << (attempt - 1));
            g_free(seconds);
            as_http_response_free(response);
            if (delay < 1)
                delay = 1;
            if (delay > 60)
                delay = 60;
            cancellable_sleep(cancellable, delay);
            continue;
        }

        gsize length = 0;
        const gchar *data = response->body
                                ? g_bytes_get_data(response->body, &length)
                                : NULL;
        gchar *copy = data ? g_strndup(data, MIN(length, 4096)) : NULL;
        g_propagate_error(error,
                          as_provider_error_from_http(response->status, copy,
                                                      cfg->name));
        g_free(copy);
        as_http_response_free(response);
        break;
    }

    as_http_client_free(client);
    g_ptr_array_unref(headers);
    if (!success && error && *error == NULL)
        g_set_error(error, AS_ERROR, AS_ERROR_CANCELLED,
                    "The request was cancelled.");
    return success;
}

static JsonObject *parse_object_response(const AsProviderConfig *cfg,
                                         AsHttpResponse *response, GError **error)
{
    gsize length = 0;
    const gchar *data = response->body
                            ? g_bytes_get_data(response->body, &length)
                            : NULL;
    if (!data || length == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "%s returned an empty response.", cfg->name);
        return NULL;
    }
    JsonNode *node = as_json_parse(data, (gssize) length, error);
    if (!node)
        return NULL;
    if (!JSON_NODE_HOLDS_OBJECT(node)) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "%s returned a response that is not a JSON object.",
                    cfg->name);
        json_node_free(node);
        return NULL;
    }
    JsonObject *object = json_node_dup_object(node);
    json_node_free(node);
    return object;
}

JsonObject *as_provider_http_post_json(const AsProviderConfig *cfg,
                                       const gchar *api_key, const gchar *url,
                                       const gchar *json_body,
                                       gboolean key_in_query,
                                       GCancellable *cancellable, GError **error)
{
    gchar *final_url = key_in_query
                           ? as_provider_build_url(cfg, url, api_key, TRUE)
                           : g_strdup(url);
    if (!final_url)
        final_url = g_strdup(url);
    AsHttpResponse *response = perform_request(cfg, api_key, final_url, "POST",
                                               json_body, "application/json",
                                               cancellable, error);
    g_free(final_url);
    if (!response)
        return NULL;
    JsonObject *result = parse_object_response(cfg, response, error);
    as_http_response_free(response);
    return result;
}

JsonObject *as_provider_http_get_json(const AsProviderConfig *cfg,
                                      const gchar *api_key, const gchar *url,
                                      gboolean key_in_query,
                                      GCancellable *cancellable, GError **error)
{
    gchar *final_url = key_in_query
                           ? as_provider_build_url(cfg, url, api_key, TRUE)
                           : g_strdup(url);
    if (!final_url)
        final_url = g_strdup(url);
    AsHttpResponse *response = perform_request(cfg, api_key, final_url, "GET",
                                               NULL, "application/json",
                                               cancellable, error);
    g_free(final_url);
    if (!response)
        return NULL;
    JsonObject *result = parse_object_response(cfg, response, error);
    as_http_response_free(response);
    return result;
}

GBytes *as_provider_http_post_bytes(const AsProviderConfig *cfg,
                                    const gchar *api_key, const gchar *url,
                                    const gchar *body, const gchar *accept,
                                    gchar **content_type,
                                    GCancellable *cancellable, GError **error)
{
    if (content_type)
        *content_type = NULL;
    AsHttpResponse *response = perform_request(cfg, api_key, url, "POST", body,
                                               accept, cancellable, error);
    if (!response)
        return NULL;
    if (!response->body || g_bytes_get_size(response->body) == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "%s returned an empty response.", cfg->name);
        as_http_response_free(response);
        return NULL;
    }
    GBytes *bytes = g_bytes_ref(response->body);
    if (content_type)
        *content_type = g_strdup(response->content_type);
    as_http_response_free(response);
    return bytes;
}

GBytes *as_provider_http_get_bytes(const AsProviderConfig *cfg,
                                   const gchar *api_key, const gchar *url,
                                   const gchar *accept, gchar **content_type,
                                   GCancellable *cancellable, GError **error)
{
    if (content_type)
        *content_type = NULL;
    AsHttpResponse *response = perform_request(cfg, api_key, url, "GET", NULL,
                                               accept, cancellable, error);
    if (!response)
        return NULL;
    if (!response->body || g_bytes_get_size(response->body) == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "%s returned an empty response.", cfg->name);
        as_http_response_free(response);
        return NULL;
    }
    GBytes *bytes = g_bytes_ref(response->body);
    if (content_type)
        *content_type = g_strdup(response->content_type);
    as_http_response_free(response);
    return bytes;
}
