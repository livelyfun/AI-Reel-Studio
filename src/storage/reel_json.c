/* reel_json.c — complete, versioned JSON codec for the reel document.
 *
 * The document is deliberately self-contained: it stores the provider output,
 * user edits and local media paths so a project can be reopened without
 * contacting any provider. Secrets are never part of this document.
 */
#include "reel_json.h"
#include "as_core.h"
#include "as_json.h"

#include <json-glib/json-glib.h>

static void set_string_member(JsonObject *obj, const gchar *key, const gchar *value)
{
    if (value)
        json_object_set_string_member(obj, key, value);
}

static JsonArray *string_array_new(const GPtrArray *array)
{
    JsonArray *json = json_array_new();
    if (array) {
        for (guint i = 0; i < array->len; i++)
            json_array_add_string_element(json, g_ptr_array_index(array, i));
    }
    return json;
}

static void add_word_timings(JsonObject *obj, const AsScene *scene)
{
    JsonArray *array = json_array_new();
    for (guint i = 0; i < scene->word_timings->len; i++) {
        AsWordTiming *wt = g_ptr_array_index(scene->word_timings, i);
        JsonObject *item = json_object_new();
        set_string_member(item, "word", wt->word);
        json_object_set_double_member(item, "start", wt->start);
        json_object_set_double_member(item, "end", wt->end);
        json_object_set_boolean_member(item, "is_keyword", wt->is_keyword);
        json_array_add_object_element(array, item);
    }
    json_object_set_array_member(obj, "word_timings", array);
}

static JsonObject *scene_to_object(const AsScene *scene)
{
    JsonObject *obj = json_object_new();
    json_object_set_int_member(obj, "scene_id", scene->scene_id);
    json_object_set_double_member(obj, "duration", scene->duration);
    set_string_member(obj, "narration", scene->narration);
    set_string_member(obj, "visual_description", scene->visual_description);
    set_string_member(obj, "visual_prompt", scene->visual_prompt);
    set_string_member(obj, "camera", scene->camera);
    set_string_member(obj, "lighting", scene->lighting);
    set_string_member(obj, "transition", scene->transition);
    set_string_member(obj, "on_screen_text", scene->on_screen_text);
    set_string_member(obj, "subtitle_text", scene->subtitle_text);
    json_object_set_array_member(obj, "keywords", string_array_new(scene->keywords));
    add_word_timings(obj, scene);
    set_string_member(obj, "image_path", scene->image_path);
    set_string_member(obj, "image_source_url", scene->image_source_url);
    set_string_member(obj, "image_author", scene->image_author);
    set_string_member(obj, "image_license", scene->image_license);
    set_string_member(obj, "image_license_url", scene->image_license_url);
    set_string_member(obj, "image_attribution", scene->image_attribution);
    set_string_member(obj, "audio_path", scene->audio_path);
    json_object_set_boolean_member(obj, "timing_estimated", scene->timing_estimated);
    return obj;
}

static JsonObject *visual_bible_to_object(const AsVisualBible *vb)
{
    JsonObject *obj = json_object_new();
    set_string_member(obj, "character_appearance", vb->character_appearance);
    set_string_member(obj, "clothing", vb->clothing);
    set_string_member(obj, "environment", vb->environment);
    set_string_member(obj, "locations", vb->locations);
    set_string_member(obj, "objects", vb->objects);
    set_string_member(obj, "visual_style", vb->visual_style);
    set_string_member(obj, "lighting", vb->lighting);
    set_string_member(obj, "camera_style", vb->camera_style);
    set_string_member(obj, "overall_atmosphere", vb->overall_atmosphere);
    json_object_set_array_member(obj, "color_palette",
                                 string_array_new(vb->color_palette));
    return obj;
}

static JsonObject *plan_to_object(const AsContentPlan *plan)
{
    JsonObject *obj = json_object_new();
    set_string_member(obj, "topic", plan->topic);
    set_string_member(obj, "purpose", plan->purpose);
    set_string_member(obj, "target_audience", plan->target_audience);
    set_string_member(obj, "tone", plan->tone);
    set_string_member(obj, "story_structure", plan->story_structure);
    set_string_member(obj, "hook", plan->hook);
    set_string_member(obj, "ending", plan->ending);
    set_string_member(obj, "call_to_action", plan->call_to_action);
    set_string_member(obj, "pacing", plan->pacing);
    json_object_set_int_member(obj, "scene_count", plan->scene_count);
    json_object_set_double_member(obj, "estimated_total_duration",
                                  plan->estimated_total_duration);
    json_object_set_object_member(obj, "visual_bible",
                                  visual_bible_to_object(&plan->visual_bible));
    return obj;
}

static JsonObject *metadata_to_object(const AsMetadata *meta)
{
    JsonObject *obj = json_object_new();
    set_string_member(obj, "reel_name", meta->reel_name);
    set_string_member(obj, "youtube_title", meta->youtube_title);
    set_string_member(obj, "youtube_description", meta->youtube_description);
    json_object_set_array_member(obj, "tags", string_array_new(meta->tags));
    json_object_set_array_member(obj, "hashtags", string_array_new(meta->hashtags));
    set_string_member(obj, "hook_analysis", meta->hook_analysis);
    set_string_member(obj, "audience_fit", meta->audience_fit);
    return obj;
}

static JsonObject *request_to_object(const AsReelRequest *req)
{
    JsonObject *obj = json_object_new();
    set_string_member(obj, "prompt", req->prompt);
    set_string_member(obj, "voice_style", req->voice_style);
    set_string_member(obj, "visual_style", req->visual_style);
    json_object_set_int_member(obj, "duration", req->duration);
    json_object_set_string_member(obj, "aspect_ratio",
                                  as_aspect_to_string(req->aspect_ratio));
    return obj;
}

gchar *as_reel_to_json(const AsReel *reel)
{
    g_return_val_if_fail(reel != NULL, NULL);
    JsonObject *root = json_object_new();
    json_object_set_int_member(root, "schema_version", AS_REEL_JSON_VERSION);
    set_string_member(root, "id", reel->id);
    set_string_member(root, "created_at", reel->created_at);
    json_object_set_object_member(root, "request", request_to_object(&reel->request));
    json_object_set_object_member(root, "plan", plan_to_object(&reel->plan));

    JsonArray *scenes = json_array_new();
    for (guint i = 0; i < reel->scenes->len; i++)
        json_array_add_object_element(scenes,
                                      scene_to_object(g_ptr_array_index(reel->scenes, i)));
    json_object_set_array_member(root, "scenes", scenes);

    json_object_set_object_member(root, "metadata", metadata_to_object(&reel->metadata));
    json_object_set_double_member(root, "total_duration", reel->total_duration);
    set_string_member(root, "music_mood", reel->music_mood);
    set_string_member(root, "thumbnail_path", reel->thumbnail_path);
    json_object_set_string_member(root, "caption_style",
                                  as_caption_to_string(reel->caption_style));
    set_string_member(root, "narration_text", reel->narration_text);
    set_string_member(root, "narration_path", reel->narration_path);
    set_string_member(root, "timing_source", reel->timing_source);
    set_string_member(root, "last_warning", reel->last_warning);

    JsonNode *node = json_node_new(JSON_NODE_OBJECT);
    json_node_take_object(node, root);
    gchar *text = as_json_to_string(node, TRUE);
    json_node_free(node);
    return text;
}

static gchar *string_member(JsonObject *obj, const gchar *key)
{
    return as_json_get_string(obj, key);
}

static void parse_word_timings(JsonObject *obj, AsScene *scene)
{
    JsonArray *array = as_json_get_array(obj, "word_timings");
    if (!array)
        return;
    guint length = json_array_get_length(array);
    for (guint i = 0; i < length; i++) {
        JsonNode *node = json_array_get_element(array, i);
        if (!JSON_NODE_HOLDS_OBJECT(node))
            continue;
        JsonObject *item = json_node_get_object(node);
        gchar *word = as_json_get_string(item, "word");
        if (!word || !*word) {
            g_free(word);
            continue;
        }
        AsWordTiming *wt = g_new0(AsWordTiming, 1);
        wt->word = word;
        wt->start = as_json_get_double(item, "start", 0);
        wt->end = as_json_get_double(item, "end", 0);
        wt->is_keyword = as_json_get_bool(item, "is_keyword", FALSE);
        g_ptr_array_add(scene->word_timings, wt);
    }
}

static void parse_scene(JsonObject *obj, AsReel *reel)
{
    AsScene *scene = as_scene_new();
    scene->scene_id = (gint) as_json_get_int(obj, "scene_id", 0);
    scene->duration = as_json_get_double(obj, "duration", 0);
    scene->narration = string_member(obj, "narration");
    scene->visual_description = string_member(obj, "visual_description");
    scene->visual_prompt = string_member(obj, "visual_prompt");
    scene->camera = string_member(obj, "camera");
    scene->lighting = string_member(obj, "lighting");
    scene->transition = string_member(obj, "transition");
    if (!scene->transition)
        scene->transition = g_strdup("crossfade");
    scene->on_screen_text = string_member(obj, "on_screen_text");
    scene->subtitle_text = string_member(obj, "subtitle_text");
    scene->image_path = string_member(obj, "image_path");
    scene->image_source_url = string_member(obj, "image_source_url");
    scene->image_author = string_member(obj, "image_author");
    scene->image_license = string_member(obj, "image_license");
    scene->image_license_url = string_member(obj, "image_license_url");
    scene->image_attribution = string_member(obj, "image_attribution");
    scene->audio_path = string_member(obj, "audio_path");
    scene->timing_estimated = as_json_get_bool(obj, "timing_estimated", TRUE);

    GPtrArray *keywords = as_json_get_string_array(obj, "keywords");
    if (keywords) {
        g_ptr_array_unref(scene->keywords);
        scene->keywords = keywords;
    }
    parse_word_timings(obj, scene);
    g_ptr_array_add(reel->scenes, scene);
}

static void parse_visual_bible(JsonObject *obj, AsVisualBible *vb)
{
    vb->character_appearance = string_member(obj, "character_appearance");
    vb->clothing = string_member(obj, "clothing");
    vb->environment = string_member(obj, "environment");
    vb->locations = string_member(obj, "locations");
    vb->objects = string_member(obj, "objects");
    vb->visual_style = string_member(obj, "visual_style");
    vb->lighting = string_member(obj, "lighting");
    vb->camera_style = string_member(obj, "camera_style");
    vb->overall_atmosphere = string_member(obj, "overall_atmosphere");
    GPtrArray *palette = as_json_get_string_array(obj, "color_palette");
    if (palette) {
        g_ptr_array_unref(vb->color_palette);
        vb->color_palette = palette;
    }
}

static void parse_plan(JsonObject *obj, AsContentPlan *plan)
{
    plan->topic = string_member(obj, "topic");
    plan->purpose = string_member(obj, "purpose");
    plan->target_audience = string_member(obj, "target_audience");
    plan->tone = string_member(obj, "tone");
    plan->story_structure = string_member(obj, "story_structure");
    plan->hook = string_member(obj, "hook");
    plan->ending = string_member(obj, "ending");
    plan->call_to_action = string_member(obj, "call_to_action");
    plan->pacing = string_member(obj, "pacing");
    plan->scene_count = (gint) as_json_get_int(obj, "scene_count", 0);
    plan->estimated_total_duration =
        as_json_get_double(obj, "estimated_total_duration", 0);
    JsonObject *vb = as_json_get_object(obj, "visual_bible");
    if (vb)
        parse_visual_bible(vb, &plan->visual_bible);
}

static void parse_metadata(JsonObject *obj, AsMetadata *meta)
{
    meta->reel_name = string_member(obj, "reel_name");
    meta->youtube_title = string_member(obj, "youtube_title");
    meta->youtube_description = string_member(obj, "youtube_description");
    meta->hook_analysis = string_member(obj, "hook_analysis");
    meta->audience_fit = string_member(obj, "audience_fit");
    GPtrArray *tags = as_json_get_string_array(obj, "tags");
    if (tags) {
        g_ptr_array_unref(meta->tags);
        meta->tags = tags;
    }
    GPtrArray *hashtags = as_json_get_string_array(obj, "hashtags");
    if (hashtags) {
        g_ptr_array_unref(meta->hashtags);
        meta->hashtags = hashtags;
    }
}

static void parse_request(JsonObject *obj, AsReelRequest *req)
{
    req->prompt = string_member(obj, "prompt");
    req->voice_style = string_member(obj, "voice_style");
    req->visual_style = string_member(obj, "visual_style");
    req->duration = (gint) as_json_get_int(obj, "duration", 30);
    gchar *aspect = string_member(obj, "aspect_ratio");
    req->aspect_ratio = as_aspect_from_string(aspect);
    g_free(aspect);
}

AsReel *as_reel_from_json(const gchar *json, GError **error)
{
    if (!json || !*json) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_JSON,
                    "The stored reel document is empty.");
        return NULL;
    }
    JsonNode *node = as_json_parse(json, -1, error);
    if (!node)
        return NULL;
    JsonObject *root = as_json_root_object(node);
    if (!root) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_JSON,
                    "The stored reel document is not a JSON object.");
        json_node_free(node);
        return NULL;
    }

    gint version = (gint) as_json_get_int(root, "schema_version", 0);
    if (version > AS_REEL_JSON_VERSION) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_JSON,
                    "The project document was created by a newer version "
                    "(document %d, application %d).", version, AS_REEL_JSON_VERSION);
        json_node_free(node);
        return NULL;
    }

    AsReel *reel = as_reel_new();
    reel->id = string_member(root, "id");
    reel->created_at = string_member(root, "created_at");
    JsonObject *request = as_json_get_object(root, "request");
    if (request) {
        as_request_clear(&reel->request);
        parse_request(request, &reel->request);
    }
    JsonObject *plan = as_json_get_object(root, "plan");
    if (plan)
        parse_plan(plan, &reel->plan);
    JsonObject *meta = as_json_get_object(root, "metadata");
    if (meta)
        parse_metadata(meta, &reel->metadata);

    JsonArray *scenes = as_json_get_array(root, "scenes");
    if (scenes) {
        guint length = json_array_get_length(scenes);
        for (guint i = 0; i < length; i++) {
            JsonNode *item = json_array_get_element(scenes, i);
            if (JSON_NODE_HOLDS_OBJECT(item))
                parse_scene(json_node_get_object(item), reel);
        }
    }

    reel->total_duration = as_json_get_double(root, "total_duration", 0);
    reel->music_mood = string_member(root, "music_mood");
    reel->thumbnail_path = string_member(root, "thumbnail_path");
    gchar *caption = string_member(root, "caption_style");
    if (caption) {
        reel->caption_style = as_caption_from_string(caption);
        g_free(caption);
    }
    reel->narration_text = string_member(root, "narration_text");
    reel->narration_path = string_member(root, "narration_path");
    g_free(reel->timing_source);
    reel->timing_source = string_member(root, "timing_source");
    if (!reel->timing_source)
        reel->timing_source = g_strdup("estimated");
    reel->last_warning = string_member(root, "last_warning");

    json_node_free(node);
    return reel;
}
