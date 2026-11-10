/* json_utils.c — strict JSON parsing helpers used by providers and storage. */
#include "as_json.h"
#include "as_core.h"

#include <string.h>

JsonNode *as_json_parse(const gchar *data, gssize length, GError **error)
{
    if (!data || length == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_JSON,
                    "The server returned an empty response.");
        return NULL;
    }
    JsonParser *parser = json_parser_new();
    GError *local = NULL;
    if (!json_parser_load_from_data(parser, data, length, &local)) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_JSON,
                    "The server response is not valid JSON: %s", local->message);
        g_clear_error(&local);
        g_object_unref(parser);
        return NULL;
    }
    JsonNode *root = json_parser_get_root(parser);
    if (!root || json_node_get_node_type(root) != JSON_NODE_OBJECT) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_JSON,
                    "The server response is not a JSON object.");
        g_object_unref(parser);
        return NULL;
    }
    JsonNode *copy = json_node_copy(root);
    g_object_unref(parser);
    return copy;
}

JsonObject *as_json_root_object(JsonNode *node)
{
    if (!node || json_node_get_node_type(node) != JSON_NODE_OBJECT)
        return NULL;
    return json_node_get_object(node);
}

gchar *as_json_to_string(JsonNode *node, gboolean pretty)
{
    if (!node)
        return NULL;
    JsonGenerator *generator = json_generator_new();
    json_generator_set_root(generator, node);
    json_generator_set_pretty(generator, pretty);
    gchar *data = json_generator_to_data(generator, NULL);
    g_object_unref(generator);
    return data;
}

gchar *as_json_get_string(JsonObject *object, const gchar *member)
{
    if (!object || !json_object_has_member(object, member))
        return NULL;
    JsonNode *node = json_object_get_member(object, member);
    if (!node || json_node_get_node_type(node) != JSON_NODE_VALUE ||
        !json_node_get_value_type(node))
        return NULL;
    if (json_node_get_value_type(node) != G_TYPE_STRING)
        return NULL;
    return g_strdup(json_node_get_string(node));
}

gchar *as_json_get_string_or(JsonObject *object, const gchar *member,
                             const gchar *fallback)
{
    gchar *value = as_json_get_string(object, member);
    if (value && *value)
        return value;
    g_free(value);
    return g_strdup(fallback ? fallback : "");
}

gint64 as_json_get_int(JsonObject *object, const gchar *member, gint64 fallback)
{
    if (!object || !json_object_has_member(object, member))
        return fallback;
    JsonNode *node = json_object_get_member(object, member);
    if (!node || json_node_get_node_type(node) != JSON_NODE_VALUE)
        return fallback;
    GType type = json_node_get_value_type(node);
    if (type == G_TYPE_INT64 || type == G_TYPE_INT || type == G_TYPE_UINT ||
        type == G_TYPE_LONG || type == G_TYPE_DOUBLE)
        return json_node_get_int(node);
    return fallback;
}

gdouble as_json_get_double(JsonObject *object, const gchar *member,
                           gdouble fallback)
{
    if (!object || !json_object_has_member(object, member))
        return fallback;
    JsonNode *node = json_object_get_member(object, member);
    if (!node || json_node_get_node_type(node) != JSON_NODE_VALUE)
        return fallback;
    GType type = json_node_get_value_type(node);
    if (type == G_TYPE_DOUBLE || type == G_TYPE_FLOAT)
        return json_node_get_double(node);
    if (type == G_TYPE_INT64 || type == G_TYPE_INT)
        return (gdouble) json_node_get_int(node);
    return fallback;
}

gboolean as_json_get_bool(JsonObject *object, const gchar *member,
                          gboolean fallback)
{
    if (!object || !json_object_has_member(object, member))
        return fallback;
    JsonNode *node = json_object_get_member(object, member);
    if (!node || json_node_get_node_type(node) != JSON_NODE_VALUE)
        return fallback;
    if (json_node_get_value_type(node) != G_TYPE_BOOLEAN)
        return fallback;
    return json_node_get_boolean(node);
}

JsonObject *as_json_get_object(JsonObject *object, const gchar *member)
{
    if (!object || !json_object_has_member(object, member))
        return NULL;
    JsonNode *node = json_object_get_member(object, member);
    if (!node || json_node_get_node_type(node) != JSON_NODE_OBJECT)
        return NULL;
    return json_node_get_object(node);
}

JsonArray *as_json_get_array(JsonObject *object, const gchar *member)
{
    if (!object || !json_object_has_member(object, member))
        return NULL;
    JsonNode *node = json_object_get_member(object, member);
    if (!node || json_node_get_node_type(node) != JSON_NODE_ARRAY)
        return NULL;
    return json_node_get_array(node);
}

GPtrArray *as_json_get_string_array(JsonObject *object, const gchar *member)
{
    GPtrArray *result = g_ptr_array_new_with_free_func(g_free);
    JsonArray *array = as_json_get_array(object, member);
    if (!array)
        return result;
    guint len = json_array_get_length(array);
    for (guint i = 0; i < len; i++) {
        JsonNode *node = json_array_get_element(array, i);
        if (node && json_node_get_node_type(node) == JSON_NODE_VALUE &&
            json_node_get_value_type(node) == G_TYPE_STRING) {
            const gchar *s = json_node_get_string(node);
            if (s && *s)
                g_ptr_array_add(result, g_strdup(s));
        }
    }
    return result;
}

gchar *as_json_node_string_default(JsonNode *node, const gchar *fallback)
{
    if (node && json_node_get_node_type(node) == JSON_NODE_VALUE &&
        json_node_get_value_type(node) == G_TYPE_STRING)
        return g_strdup(json_node_get_string(node));
    return g_strdup(fallback ? fallback : "");
}
