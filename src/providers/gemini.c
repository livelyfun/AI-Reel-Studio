/* gemini.c — Google Gemini adapter (text, structured JSON, speech, images). */
#include "provider_adapters.h"
#include "as_provider.h"
#include "as_core.h"
#include "as_json.h"

#include <string.h>

static gchar *gemini_model_id(const gchar *model)
{
    if (!model || !*model)
        return NULL;
    if (g_str_has_prefix(model, "models/"))
        return g_strdup(model + 7);
    return g_strdup(model);
}

static gchar *gemini_url(const AsProviderConfig *cfg, const gchar *model,
                         const gchar *verb)
{
    gchar *clean = gemini_model_id(model);
    if (!clean) {
        g_free(clean);
        return NULL;
    }
    const gchar *version = (cfg->api_version && *cfg->api_version)
                               ? cfg->api_version
                               : "v1beta";
    gchar *path;
    if (cfg->chat_path && *cfg->chat_path) {
        gchar *replaced = g_strdup(cfg->chat_path);
        gchar *placeholder = strstr(replaced, "{model}");
        if (placeholder) {
            gchar *prefix = g_strndup(replaced, placeholder - replaced);
            path = g_strdup_printf("%s%s%s", prefix, clean, placeholder + 7);
            g_free(prefix);
            g_free(replaced);
        } else {
            path = replaced;
        }
    } else {
        path = g_strdup_printf("/%s/models/%s:%s", version, clean, verb);
    }
    gchar *url = as_provider_build_url(cfg, path, NULL, FALSE);
    g_free(path);
    g_free(clean);
    return url;
}

static gchar *node_to_string(JsonNode *node)
{
    if (!node)
        return NULL;
    return as_json_to_string(node, FALSE);
}

static gchar *join_parts(JsonObject *root, gboolean *saw_text)
{
    GString *text = g_string_new(NULL);
    *saw_text = FALSE;
    JsonArray *candidates = as_json_get_array(root, "candidates");
    if (!candidates)
        return g_string_free(text, FALSE);
    for (guint i = 0; i < json_array_get_length(candidates); i++) {
        JsonNode *candidate_node = json_array_get_element(candidates, i);
        if (!JSON_NODE_HOLDS_OBJECT(candidate_node))
            continue;
        JsonObject *candidate = json_node_get_object(candidate_node);
        JsonObject *content = as_json_get_object(candidate, "content");
        JsonArray *parts = content ? as_json_get_array(content, "parts") : NULL;
        if (!parts)
            continue;
        for (guint j = 0; j < json_array_get_length(parts); j++) {
            JsonNode *part_node = json_array_get_element(parts, j);
            if (!JSON_NODE_HOLDS_OBJECT(part_node))
                continue;
            gchar *part = as_json_get_string(json_node_get_object(part_node),
                                             "text");
            if (part && *part) {
                if (text->len > 0)
                    g_string_append_c(text, '\n');
                g_string_append(text, part);
                *saw_text = TRUE;
            }
            g_free(part);
        }
    }
    return g_string_free(text, FALSE);
}

gboolean as_gemini_generate_text(const AsProviderConfig *cfg,
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
    gchar *url = gemini_url(cfg, model, "generateContent");
    if (!url) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG, "Invalid Gemini model id.");
        return FALSE;
    }

    JsonObject *root = json_object_new();
    JsonArray *contents = json_array_new();
    JsonObject *content = json_object_new();
    json_object_set_string_member(content, "role", "user");
    JsonArray *parts = json_array_new();
    JsonObject *part = json_object_new();
    json_object_set_string_member(part, "text", user_prompt ? user_prompt : "");
    json_array_add_object_element(parts, part);
    json_object_set_array_member(content, "parts", parts);
    json_array_add_object_element(contents, content);
    json_object_set_array_member(root, "contents", contents);

    if (system_prompt && *system_prompt) {
        JsonObject *instruction = json_object_new();
        JsonArray *system_parts = json_array_new();
        JsonObject *system_part = json_object_new();
        json_object_set_string_member(system_part, "text", system_prompt);
        json_array_add_object_element(system_parts, system_part);
        json_object_set_array_member(instruction, "parts", system_parts);
        json_object_set_object_member(root, "systemInstruction", instruction);
    }

    JsonObject *generation = json_object_new();
    json_object_set_double_member(generation, "temperature", temperature);
    if (structured)
        json_object_set_string_member(generation, "responseMimeType",
                                      "application/json");
    json_object_set_object_member(root, "generationConfig", generation);

    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    gchar *body = as_json_to_string(node, FALSE);
    json_node_free(node);

    JsonObject *response = as_provider_http_post_json(cfg, api_key, url, body,
                                                      FALSE, cancellable, error);
    g_free(url);
    g_free(body);
    if (!response)
        return FALSE;

    gboolean saw_text = FALSE;
    result->text = join_parts(response, &saw_text);
    if (!saw_text || !result->text || !*result->text) {
        GError *blocked = NULL;
        JsonObject *feedback = as_json_get_object(response, "promptFeedback");
        gchar *reason = feedback ? as_json_get_string(feedback, "blockReason") : NULL;
        if (reason)
            blocked = as_error_new(AS_ERROR_INVALID_RESPONSE,
                                   "Gemini blocked the request (%s).", reason);
        g_free(reason);
        json_object_unref(response);
        g_free(result->text);
        result->text = NULL;
        if (blocked)
            g_propagate_error(error, blocked);
        else
            g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                        "Gemini returned no text content.");
        return FALSE;
    }

    JsonArray *candidates = as_json_get_array(response, "candidates");
    if (candidates && json_array_get_length(candidates) > 0) {
        JsonNode *first = json_array_get_element(candidates, 0);
        if (JSON_NODE_HOLDS_OBJECT(first)) {
            JsonObject *candidate = json_node_get_object(first);
            result->finish_reason = as_json_get_string(candidate, "finishReason");
        }
    }
    JsonNode *usage = json_object_get_member(response, "usageMetadata");
    if (usage)
        result->usage_json = node_to_string(usage);
    json_object_unref(response);
    return TRUE;
}

static GBytes *pcm_to_wav(GBytes *pcm, gint rate, gint channels, gint bits)
{
    gsize pcm_len = 0;
    const guchar *pcm_data = g_bytes_get_data(pcm, &pcm_len);
    gint block_align = channels * bits / 8;
    guint32 byte_rate = (guint32) rate * block_align;
    gsize total = 44 + pcm_len;
    guchar *wav = g_malloc0(total);
    memcpy(wav, "RIFF", 4);
    guint32 riff_size = (guint32) (total - 8);
    memcpy(wav + 4, &riff_size, 4);
    memcpy(wav + 8, "WAVEfmt ", 8);
    guint32 fmt_size = 16;
    memcpy(wav + 16, &fmt_size, 4);
    guint16 audio_format = 1;
    memcpy(wav + 20, &audio_format, 2);
    guint16 ch = (guint16) channels;
    memcpy(wav + 22, &ch, 2);
    guint32 sample_rate = (guint32) rate;
    memcpy(wav + 24, &sample_rate, 4);
    memcpy(wav + 28, &byte_rate, 4);
    guint16 align = (guint16) block_align;
    memcpy(wav + 32, &align, 2);
    guint16 bit_depth = (guint16) bits;
    memcpy(wav + 34, &bit_depth, 2);
    memcpy(wav + 36, "data", 4);
    guint32 data_size = (guint32) pcm_len;
    memcpy(wav + 40, &data_size, 4);
    memcpy(wav + 44, pcm_data, pcm_len);
    return g_bytes_new_take(wav, total);
}

gboolean as_gemini_generate_speech(const AsProviderConfig *cfg,
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
    gchar *url = gemini_url(cfg, model, "generateContent");
    if (!url) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG, "Invalid Gemini model id.");
        return FALSE;
    }

    JsonObject *root = json_object_new();
    JsonArray *contents = json_array_new();
    JsonObject *content = json_object_new();
    JsonArray *parts = json_array_new();
    JsonObject *part = json_object_new();
    json_object_set_string_member(part, "text", text ? text : "");
    json_array_add_object_element(parts, part);
    json_object_set_array_member(content, "parts", parts);
    json_array_add_object_element(contents, content);
    json_object_set_array_member(root, "contents", contents);

    JsonObject *generation = json_object_new();
    JsonArray *modalities = json_array_new();
    json_array_add_string_element(modalities, "AUDIO");
    json_object_set_array_member(generation, "responseModalities", modalities);
    if (voice && *voice) {
        JsonObject *speech = json_object_new();
        JsonObject *voice_config = json_object_new();
        JsonObject *prebuilt = json_object_new();
        json_object_set_string_member(prebuilt, "voiceName", voice);
        json_object_set_object_member(voice_config, "prebuiltVoiceConfig", prebuilt);
        json_object_set_object_member(speech, "voiceConfig", voice_config);
        json_object_set_object_member(generation, "speechConfig", speech);
    }
    json_object_set_object_member(root, "generationConfig", generation);

    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    gchar *body = as_json_to_string(node, FALSE);
    json_node_free(node);

    JsonObject *response = as_provider_http_post_json(cfg, api_key, url, body,
                                                      FALSE, cancellable, error);
    g_free(url);
    g_free(body);
    if (!response)
        return FALSE;

    GBytes *audio = NULL;
    gchar *mime = NULL;
    JsonArray *candidates = as_json_get_array(response, "candidates");
    if (candidates) {
        for (guint i = 0; i < json_array_get_length(candidates) && !audio; i++) {
            JsonNode *cn = json_array_get_element(candidates, i);
            if (!JSON_NODE_HOLDS_OBJECT(cn))
                continue;
            JsonObject *candidate = json_node_get_object(cn);
            JsonObject *content_obj = as_json_get_object(candidate, "content");
            JsonArray *content_parts =
                content_obj ? as_json_get_array(content_obj, "parts") : NULL;
            if (!content_parts)
                continue;
            for (guint j = 0; j < json_array_get_length(content_parts); j++) {
                JsonNode *pn = json_array_get_element(content_parts, j);
                if (!JSON_NODE_HOLDS_OBJECT(pn))
                    continue;
                JsonObject *inline_data =
                    as_json_get_object(json_node_get_object(pn), "inlineData");
                if (!inline_data)
                    inline_data = as_json_get_object(json_node_get_object(pn),
                                                     "inline_data");
                if (!inline_data)
                    continue;
                gchar *data = as_json_get_string(inline_data, "data");
                mime = as_json_get_string(inline_data, "mimeType");
                if (!mime)
                    mime = as_json_get_string(inline_data, "mime_type");
                if (data) {
                    gsize len = 0;
                    guchar *decoded = g_base64_decode(data, &len);
                    g_free(data);
                    if (decoded && len)
                        audio = g_bytes_new_take(decoded, len);
                    else
                        g_free(decoded);
                }
                if (audio)
                    break;
            }
        }
    }
    JsonNode *usage = json_object_get_member(response, "usageMetadata");
    if (usage)
        result->usage_json = node_to_string(usage);
    json_object_unref(response);

    if (!audio) {
        g_free(mime);
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "Gemini returned no audio data. Check that '%s' is a "
                    "speech-capable model.", model);
        return FALSE;
    }

    result->voice = g_strdup(voice);
    gchar *lower = g_ascii_strdown(mime ? mime : "", -1);
    if (strstr(lower, "l16") || strstr(lower, "pcm")) {
        gint rate = 24000;
        const gchar *rate_pos = strstr(lower, "rate=");
        if (rate_pos)
            rate = atoi(rate_pos + 5);
        if (rate <= 0)
            rate = 24000;
        result->data = pcm_to_wav(audio, rate, 1, 16);
        g_bytes_unref(audio);
        result->mime_type = g_strdup("audio/wav");
        result->extension = g_strdup("wav");
    } else if (strstr(lower, "wav")) {
        result->data = audio;
        result->mime_type = g_strdup(mime);
        result->extension = g_strdup("wav");
    } else if (strstr(lower, "mpeg") || strstr(lower, "mp3")) {
        result->data = audio;
        result->mime_type = g_strdup(mime);
        result->extension = g_strdup("mp3");
    } else {
        result->data = audio;
        result->mime_type = g_strdup(mime ? mime : "application/octet-stream");
        result->extension = g_strdup("bin");
    }
    g_free(lower);
    g_free(mime);
    return TRUE;
}

gboolean as_gemini_generate_image(const AsProviderConfig *cfg,
                                    const gchar *api_key, const gchar *model,
                                    const gchar *prompt, const gchar *size,
                                    AsImageResult *result,
                                    GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg && result, FALSE);
    memset(result, 0, sizeof(*result));
    (void) size;
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
    gchar *url = gemini_url(cfg, model, "generateContent");
    if (!url) {
        g_set_error(error, AS_ERROR, AS_ERROR_CONFIG, "Invalid Gemini model id.");
        return FALSE;
    }

    JsonObject *root = json_object_new();
    JsonArray *contents = json_array_new();
    JsonObject *content = json_object_new();
    JsonArray *parts = json_array_new();
    JsonObject *part = json_object_new();
    json_object_set_string_member(part, "text", prompt ? prompt : "");
    json_array_add_object_element(parts, part);
    json_object_set_array_member(content, "parts", parts);
    json_array_add_object_element(contents, content);
    json_object_set_array_member(root, "contents", contents);
    JsonObject *generation = json_object_new();
    JsonArray *modalities = json_array_new();
    json_array_add_string_element(modalities, "IMAGE");
    json_object_set_array_member(generation, "responseModalities", modalities);
    json_object_set_object_member(root, "generationConfig", generation);

    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    gchar *body = as_json_to_string(node, FALSE);
    json_node_free(node);

    JsonObject *response = as_provider_http_post_json(cfg, api_key, url, body,
                                                      FALSE, cancellable, error);
    g_free(url);
    g_free(body);
    if (!response)
        return FALSE;

    JsonArray *candidates = as_json_get_array(response, "candidates");
    for (guint i = 0; candidates && i < json_array_get_length(candidates) &&
                         !result->data; i++) {
        JsonNode *cn = json_array_get_element(candidates, i);
        if (!JSON_NODE_HOLDS_OBJECT(cn))
            continue;
        JsonObject *content_obj =
            as_json_get_object(json_node_get_object(cn), "content");
        JsonArray *content_parts =
            content_obj ? as_json_get_array(content_obj, "parts") : NULL;
        for (guint j = 0; content_parts && j < json_array_get_length(content_parts);
             j++) {
            JsonNode *pn = json_array_get_element(content_parts, j);
            if (!JSON_NODE_HOLDS_OBJECT(pn))
                continue;
            JsonObject *inline_data =
                as_json_get_object(json_node_get_object(pn), "inlineData");
            if (!inline_data)
                inline_data = as_json_get_object(json_node_get_object(pn),
                                                 "inline_data");
            if (!inline_data)
                continue;
            gchar *data = as_json_get_string(inline_data, "data");
            gchar *mime = as_json_get_string(inline_data, "mimeType");
            if (!mime)
                mime = as_json_get_string(inline_data, "mime_type");
            if (data) {
                gsize len = 0;
                guchar *decoded = g_base64_decode(data, &len);
                g_free(data);
                if (decoded && len) {
                    result->data = g_bytes_new_take(decoded, len);
                    result->mime_type = g_strdup(mime ? mime : "image/png");
                    gchar *lower = g_ascii_strdown(result->mime_type, -1);
                    if (strstr(lower, "jpeg") || strstr(lower, "jpg"))
                        result->extension = g_strdup("jpg");
                    else if (strstr(lower, "webp"))
                        result->extension = g_strdup("webp");
                    else if (strstr(lower, "gif"))
                        result->extension = g_strdup("gif");
                    else
                        result->extension = g_strdup("png");
                    g_free(lower);
                } else {
                    g_free(decoded);
                }
            }
            g_free(mime);
        }
    }
    json_object_unref(response);
    if (!result->data) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "Gemini returned no image data. Check that '%s' is an "
                    "image-capable model.", model);
        return FALSE;
    }
    return TRUE;
}

GPtrArray *as_gemini_list_models(const AsProviderConfig *cfg,
                                   const gchar *api_key,
                                   GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(cfg != NULL, NULL);
    if (!api_key || !*api_key) {
        g_set_error(error, AS_ERROR, AS_ERROR_AUTH,
                    "Enter an API key to list models.");
        return NULL;
    }
    const gchar *version = (cfg->api_version && *cfg->api_version)
                               ? cfg->api_version
                               : "v1beta";
    gchar *path;
    if (cfg->models_path && *cfg->models_path)
        path = g_strdup(cfg->models_path);
    else
        path = g_strdup_printf("/%s/models?pageSize=200", version);
    gchar *url = as_provider_build_url(cfg, path, NULL, FALSE);
    g_free(path);
    JsonObject *root = as_provider_http_get_json(cfg, api_key, url, FALSE,
                                                 cancellable, error);
    g_free(url);
    if (!root)
        return NULL;

    GPtrArray *list = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_model_info_free);
    JsonArray *models = as_json_get_array(root, "models");
    for (guint i = 0; models && i < json_array_get_length(models); i++) {
        JsonNode *node = json_array_get_element(models, i);
        if (!JSON_NODE_HOLDS_OBJECT(node))
            continue;
        JsonObject *obj = json_node_get_object(node);
        AsModelInfo *info = g_new0(AsModelInfo, 1);
        gchar *name = as_json_get_string(obj, "name");
        if (name && g_str_has_prefix(name, "models/")) {
            info->id = g_strdup(name + 7);
            g_free(name);
        } else {
            info->id = name;
        }
        info->name = as_json_get_string(obj, "displayName");
        if (!info->name)
            info->name = g_strdup(info->id);
        gint64 limit = as_json_get_int(obj, "inputTokenLimit", 0);
        if (limit > 0)
            info->context_length = g_strdup_printf("%" G_GINT64_FORMAT, limit);
        info->description = as_json_get_string(obj, "description");
        if (info->id)
            g_ptr_array_add(list, info);
        else
            as_model_info_free(info);
    }
    json_object_unref(root);
    return list;
}

gboolean as_gemini_test_connection(const AsProviderConfig *cfg,
                                     const gchar *api_key, gchar **message,
                                     GCancellable *cancellable, GError **error)
{
    if (message)
        *message = NULL;
    GPtrArray *models = as_gemini_list_models(cfg, api_key, cancellable, error);
    if (!models)
        return FALSE;
    guint count = models->len;
    if (message)
        *message = g_strdup_printf("Connected to %s. %u model%s available.",
                                   cfg->name, count, count == 1 ? "" : "s");
    g_ptr_array_unref(models);
    return TRUE;
}
