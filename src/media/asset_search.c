/* asset_search.c — free/open image search and verified downloads.
 *
 * Sources: Openverse (no key), Wikimedia Commons (no key) and Unsplash (user
 * supplied access key). The undocumented legacy Unsplash endpoint is not used.
 * Every result carries author/license/source so exports can include attribution.
 */
#include "as_media.h"
#include "as_http.h"
#include "as_core.h"
#include "as_json.h"

#include <string.h>

#include <glib/gstdio.h>

static const gchar *const UA = "AI-Reel-Studio/1.0";
static const gchar *const OPENVERSE = "https://api.openverse.org/v1/images/";
static const gchar *const WIKIMEDIA = "https://commons.wikimedia.org/w/api.php";
static const gchar *const UNSPLASH = "https://api.unsplash.com/search/photos";

void as_asset_result_free(AsAssetResult *result)
{
    if (!result)
        return;
    g_free(result->source);
    g_free(result->title);
    g_free(result->image_url);
    g_free(result->thumbnail_url);
    g_free(result->source_page);
    g_free(result->author);
    g_free(result->license_name);
    g_free(result->license_url);
    g_free(result->attribution);
    g_free(result);
}

static JsonObject *fetch_json(const gchar *url, GCancellable *cancellable,
                              GError **error)
{
    AsHttpClient *client = as_http_client_new(8u * 1024u * 1024u, 45000, FALSE);
    if (!client) {
        g_set_error(error, AS_ERROR, AS_ERROR_NETWORK,
                    "Could not create the HTTP client.");
        return NULL;
    }
    const gchar *headers[] = { "Accept: application/json",
                               "User-Agent: AI-Reel-Studio/1.0", NULL };
    AsHttpResponse *response = NULL;
    GError *local = NULL;
    if (!as_http_get(client, url, headers, cancellable, &response, &local)) {
        g_propagate_error(error, local);
        as_http_client_free(client);
        return NULL;
    }
    as_http_client_free(client);
    if (response->status < 200 || response->status >= 300) {
        g_set_error(error, AS_ERROR, AS_ERROR_HTTP_STATUS,
                    "Image search failed (HTTP %ld).", response->status);
        as_http_response_free(response);
        return NULL;
    }
    gsize length = 0;
    const gchar *data = response->body
                            ? g_bytes_get_data(response->body, &length)
                            : NULL;
    if (!data || !length) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The image search returned an empty response.");
        as_http_response_free(response);
        return NULL;
    }
    JsonNode *node = as_json_parse(data, (gssize) length, error);
    as_http_response_free(response);
    if (!node)
        return NULL;
    JsonObject *root = as_json_root_object(node);
    if (!root) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The image search returned invalid JSON.");
        json_node_free(node);
        return NULL;
    }
    JsonObject *copy = json_object_ref(root);
    json_node_free(node);
    return copy;
}

static gchar *strip_html(const gchar *html)
{
    if (!html)
        return NULL;
    GString *out = g_string_new(NULL);
    gboolean in_tag = FALSE;
    for (const gchar *p = html; *p; p++) {
        if (*p == '<')
            in_tag = TRUE;
        else if (*p == '>')
            in_tag = FALSE;
        else if (!in_tag)
            g_string_append_c(out, *p);
    }
    g_strstrip(out->str);
    return g_string_free(out, FALSE);
}

static gchar *make_attribution(const gchar *author, const gchar *license,
                               const gchar *source)
{
    return g_strdup_printf("%s — %s (%s)", author && *author ? author : "Unknown",
                           license && *license ? license : "Unknown license",
                           source ? source : "source");
}

static gchar *license_label(const gchar *license, const gchar *version)
{
    if (!license || !*license)
        return g_strdup("Unknown license");
    gchar *upper = g_ascii_strup(license, -1);
    gchar *label;
    if (g_strcmp0(upper, "CC0") == 0)
        label = g_strdup("CC0");
    else if (g_strcmp0(upper, "PDM") == 0)
        label = g_strdup("Public Domain Mark");
    else if (g_str_has_prefix(upper, "CC-"))
        label = g_strdup_printf("CC %s%s%s", upper + 3,
                                version && *version ? " " : "",
                                version ? version : "");
    else
        label = g_strdup(license);
    g_free(upper);
    return label;
}

static void add_openverse(JsonObject *root, const gchar *query, gint limit,
                          GPtrArray *out)
{
    JsonArray *results = as_json_get_array(root, "results");
    for (guint i = 0; results && i < json_array_get_length(results) &&
                        out->len < (guint) limit;
         i++) {
        JsonNode *node = json_array_get_element(results, i);
        if (!JSON_NODE_HOLDS_OBJECT(node))
            continue;
        JsonObject *obj = json_node_get_object(node);
        gchar *image = as_json_get_string(obj, "url");
        if (!image)
            continue;
        AsAssetResult *r = g_new0(AsAssetResult, 1);
        r->source = g_strdup("Openverse");
        r->title = as_json_get_string(obj, "title");
        if (!r->title)
            r->title = g_strdup(query);
        r->image_url = image;
        r->thumbnail_url = as_json_get_string(obj, "thumbnail");
        r->source_page = as_json_get_string(obj, "foreign_landing_url");
        r->author = as_json_get_string(obj, "creator");
        gchar *license = as_json_get_string(obj, "license");
        gchar *version = as_json_get_string(obj, "license_version");
        r->license_name = license_label(license, version);
        g_free(license);
        g_free(version);
        r->license_url = as_json_get_string(obj, "license_url");
        r->width = (gint) as_json_get_int(obj, "width", 0);
        r->height = (gint) as_json_get_int(obj, "height", 0);
        r->attribution = make_attribution(r->author, r->license_name,
                                          r->source);
        g_ptr_array_add(out, r);
    }
}

static gchar *wikimedia_search_url(const gchar *query, gint limit)
{
    gchar *escaped = g_uri_escape_string(query, NULL, TRUE);
    gchar *url = g_strdup_printf(
        "%s?action=query&format=json&generator=search&gsrnamespace=6"
        "&gsrlimit=%d&gsrsearch=%s%%20filetype:bitmap&prop=imageinfo"
        "&iiprop=url|size|extmetadata&iiurlwidth=1600",
        WIKIMEDIA, limit, escaped);
    g_free(escaped);
    return url;
}

static void add_wikimedia(JsonObject *root, GPtrArray *out, gint limit)
{
    JsonObject *pages = as_json_get_object(root, "query");
    if (!pages)
        return;
    pages = as_json_get_object(pages, "pages");
    if (!pages)
        return;
    GList *members = json_object_get_members(pages);
    for (GList *l = members; l && out->len < (guint) limit; l = l->next) {
        JsonNode *page_node = json_object_get_member(pages, l->data);
        if (!page_node || !JSON_NODE_HOLDS_OBJECT(page_node))
            continue;
        JsonObject *page = json_node_get_object(page_node);
        JsonArray *infos = as_json_get_array(page, "imageinfo");
        if (!infos || json_array_get_length(infos) == 0)
            continue;
        JsonNode *info_node = json_array_get_element(infos, 0);
        if (!JSON_NODE_HOLDS_OBJECT(info_node))
            continue;
        JsonObject *info = json_node_get_object(info_node);
        gchar *image = as_json_get_string(info, "thumburl");
        if (!image)
            image = as_json_get_string(info, "url");
        if (!image)
            continue;
        AsAssetResult *r = g_new0(AsAssetResult, 1);
        r->source = g_strdup("Wikimedia Commons");
        gchar *title = as_json_get_string(page, "title");
        if (title && g_str_has_prefix(title, "File:")) {
            r->title = g_strdup(title + 5);
            g_free(title);
        } else {
            r->title = title;
        }
        r->image_url = image;
        r->thumbnail_url = as_json_get_string(info, "thumburl");
        r->source_page = as_json_get_string(info, "descriptionurl");
        r->width = (gint) as_json_get_int(info, "width", 0);
        r->height = (gint) as_json_get_int(info, "height", 0);
        JsonObject *meta = as_json_get_object(info, "extmetadata");
        if (meta) {
            JsonObject *artist = as_json_get_object(meta, "Artist");
            JsonObject *license = as_json_get_object(meta, "LicenseShortName");
            JsonObject *license_url = as_json_get_object(meta, "LicenseUrl");
            if (artist) {
                gchar *raw = as_json_get_string(artist, "value");
                r->author = strip_html(raw);
                g_free(raw);
            }
            if (license)
                r->license_name = as_json_get_string(license, "value");
            if (license_url)
                r->license_url = as_json_get_string(license_url, "value");
        }
        if (!r->license_name)
            r->license_name = g_strdup("See source page");
        r->attribution = make_attribution(r->author, r->license_name, r->source);
        g_ptr_array_add(out, r);
    }
    g_list_free(members);
}

static void add_unsplash(JsonObject *root, GPtrArray *out, gint limit)
{
    JsonArray *results = as_json_get_array(root, "results");
    for (guint i = 0; results && i < json_array_get_length(results) &&
                        out->len < (guint) limit;
         i++) {
        JsonNode *node = json_array_get_element(results, i);
        if (!JSON_NODE_HOLDS_OBJECT(node))
            continue;
        JsonObject *obj = json_node_get_object(node);
        JsonObject *urls = as_json_get_object(obj, "urls");
        if (!urls)
            continue;
        gchar *image = as_json_get_string(urls, "regular");
        if (!image)
            continue;
        AsAssetResult *r = g_new0(AsAssetResult, 1);
        r->source = g_strdup("Unsplash");
        r->title = as_json_get_string(obj, "description");
        if (!r->title)
            r->title = as_json_get_string(obj, "alt_description");
        r->image_url = image;
        r->thumbnail_url = as_json_get_string(urls, "small");
        JsonObject *links = as_json_get_object(obj, "links");
        if (links)
            r->source_page = as_json_get_string(links, "html");
        JsonObject *user = as_json_get_object(obj, "user");
        if (user)
            r->author = as_json_get_string(user, "name");
        r->license_name = g_strdup("Unsplash License");
        r->license_url = g_strdup("https://unsplash.com/license");
        r->width = (gint) as_json_get_int(obj, "width", 0);
        r->height = (gint) as_json_get_int(obj, "height", 0);
        r->attribution = make_attribution(r->author, r->license_name, r->source);
        g_ptr_array_add(out, r);
    }
}

GPtrArray *as_asset_search(const gchar *query, const gchar *unsplash_key,
                           gint limit, GCancellable *cancellable, GError **error)
{
    if (!query || !*query) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "An image search query is required.");
        return NULL;
    }
    if (limit <= 0 || limit > 50)
        limit = 8;
    GPtrArray *results = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_asset_result_free);

    gchar *escaped = g_uri_escape_string(query, NULL, TRUE);
    gchar *url = g_strdup_printf("%s?q=%s&page_size=%d&license_type=all",
                                 OPENVERSE, escaped, limit);
    g_free(escaped);
    GError *local = NULL;
    JsonObject *root = fetch_json(url, cancellable, &local);
    g_free(url);
    if (root) {
        add_openverse(root, query, limit, results);
        json_object_unref(root);
    }
    g_clear_error(&local);

    if (results->len < (guint) limit) {
        url = wikimedia_search_url(query, limit - (gint) results->len);
        root = fetch_json(url, cancellable, &local);
        g_free(url);
        if (root) {
            add_wikimedia(root, results, limit);
            json_object_unref(root);
        }
        g_clear_error(&local);
    }

    if (unsplash_key && *unsplash_key && results->len < (guint) limit) {
        gchar *key = g_uri_escape_string(unsplash_key, NULL, TRUE);
        escaped = g_uri_escape_string(query, NULL, TRUE);
        url = g_strdup_printf("%s?query=%s&per_page=%d&orientation=portrait"
                              "&client_id=%s",
                              UNSPLASH, escaped, limit - (gint) results->len, key);
        g_free(escaped);
        g_free(key);
        root = fetch_json(url, cancellable, &local);
        g_free(url);
        if (root) {
            add_unsplash(root, results, limit);
            json_object_unref(root);
        }
        g_clear_error(&local);
    }

    if (results->len == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_NOT_FOUND,
                    "No usable images were found for '%s'.", query);
        g_ptr_array_unref(results);
        return NULL;
    }
    return results;
}

gchar *as_asset_build_query(const gchar *visual_prompt,
                            const GPtrArray *keywords, const gchar *topic)
{
    GString *query = g_string_new(NULL);
    if (keywords && keywords->len) {
        for (guint i = 0; i < keywords->len && i < 5; i++) {
            const gchar *kw = g_ptr_array_index((GPtrArray *) keywords, i);
            if (!kw || !*kw)
                continue;
            if (query->len)
                g_string_append_c(query, ' ');
            g_string_append(query, kw);
        }
    }
    if (query->len == 0) {
        static const gchar *const stop[] = {
            "cinematic", "photorealistic", "4k", "8k", "highly", "detailed",
            "ultra", "realistic", "vertical", "aspect", "ratio", "camera",
            "lighting", "style", "shot", "image", "video", "render", "scene",
            "professional", "quality", "sharp", "focus", "background", "with",
            "and", "the", "a", "an", "of", "in", "on", NULL
        };
        gchar **words = g_strsplit_set(visual_prompt ? visual_prompt : "", " ,.;:-_/", -1);
        guint added = 0;
        for (guint i = 0; words[i] && added < 8; i++) {
            gchar *word = g_strstrip(words[i]);
            if (!*word)
                continue;
            gboolean skip = FALSE;
            for (guint k = 0; stop[k]; k++)
                if (g_ascii_strcasecmp(word, stop[k]) == 0)
                    skip = TRUE;
            if (skip)
                continue;
            if (query->len)
                g_string_append_c(query, ' ');
            g_string_append(query, word);
            added++;
        }
        g_strfreev(words);
    }
    if (topic && *topic && query->len < 90) {
        if (query->len)
            g_string_append_c(query, ' ');
        g_string_append(query, topic);
    }
    g_strstrip(query->str);
    gchar *result = g_string_free(query, FALSE);
    if (strlen(result) > 160)
        result[160] = '\0';
    return result;
}

static gboolean looks_like(const guchar *data, gsize length, const gchar *ext)
{
    if (!data || length < 12)
        return FALSE;
    if (g_ascii_strcasecmp(ext, "jpg") == 0 ||
        g_ascii_strcasecmp(ext, "jpeg") == 0)
        return data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF;
    if (g_ascii_strcasecmp(ext, "png") == 0)
        return data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' &&
               data[3] == 'G';
    if (g_ascii_strcasecmp(ext, "webp") == 0)
        return memcmp(data, "RIFF", 4) == 0 && memcmp(data + 8, "WEBP", 4) == 0;
    return FALSE;
}

gboolean as_asset_download(const AsAssetResult *result, const gchar *dest_path,
                           GCancellable *cancellable, GError **error)
{
    if (!result || !result->image_url || !dest_path) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "Invalid image download arguments.");
        return FALSE;
    }
    const gchar *dot = strrchr(dest_path, '.');
    const gchar *ext = dot ? dot + 1 : "";
    if (g_ascii_strcasecmp(ext, "jpg") != 0 &&
        g_ascii_strcasecmp(ext, "jpeg") != 0 &&
        g_ascii_strcasecmp(ext, "png") != 0 &&
        g_ascii_strcasecmp(ext, "webp") != 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "Unsupported image destination extension '.%s'.", ext);
        return FALSE;
    }

    AsHttpClient *client = as_http_client_new(30u * 1024u * 1024u, 60000, FALSE);
    if (!client) {
        g_set_error(error, AS_ERROR, AS_ERROR_NETWORK,
                    "Could not create the HTTP client.");
        return FALSE;
    }
    const gchar *headers[] = { "Accept: image/jpeg,image/png,image/webp,image/*",
                               "User-Agent: AI-Reel-Studio/1.0", NULL };
    gchar *content_type = NULL;
    gsize size = 0;
    gboolean ok = as_http_download_to_file(client, result->image_url, headers,
                                           dest_path, 30u * 1024u * 1024u,
                                           &content_type, &size, cancellable,
                                           error);
    as_http_client_free(client);
    if (!ok) {
        g_free(content_type);
        return FALSE;
    }

    gchar *contents = NULL;
    gsize length = 0;
    if (!g_file_get_contents(dest_path, &contents, &length, error)) {
        g_free(content_type);
        return FALSE;
    }
    gboolean valid = looks_like((const guchar *) contents, length, ext);
    g_free(contents);
    if (!valid) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The downloaded file is not a valid %s image (content type "
                    "'%s').", ext, content_type ? content_type : "unknown");
        g_free(content_type);
        g_unlink(dest_path);
        return FALSE;
    }
    if (size < 1024) {
        g_set_error(error, AS_ERROR, AS_ERROR_MEDIA,
                    "The downloaded image is unexpectedly small.");
        g_free(content_type);
        g_unlink(dest_path);
        return FALSE;
    }
    g_free(content_type);
    return TRUE;
}
