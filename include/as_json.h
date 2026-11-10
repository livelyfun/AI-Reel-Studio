/* as_json.h — small JSON-GLib helpers with strict error handling. */
#pragma once

#include <glib.h>
#include <json-glib/json-glib.h>

JsonNode *as_json_parse(const gchar *data, gssize length, GError **error);
JsonObject *as_json_root_object(JsonNode *node);
gchar *as_json_to_string(JsonNode *node, gboolean pretty);

gchar *as_json_get_string(JsonObject *object, const gchar *member);
gchar *as_json_get_string_or(JsonObject *object, const gchar *member,
                             const gchar *fallback);
gint64 as_json_get_int(JsonObject *object, const gchar *member, gint64 fallback);
gdouble as_json_get_double(JsonObject *object, const gchar *member,
                           gdouble fallback);
gboolean as_json_get_bool(JsonObject *object, const gchar *member,
                          gboolean fallback);
JsonObject *as_json_get_object(JsonObject *object, const gchar *member);
JsonArray *as_json_get_array(JsonObject *object, const gchar *member);
GPtrArray *as_json_get_string_array(JsonObject *object, const gchar *member);
gchar *as_json_node_string_default(JsonNode *node, const gchar *fallback);
