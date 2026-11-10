/* planner.c — provider-driven planning, strict JSON parsing and metadata.
 *
 * The planner is provider-agnostic: it resolves the planning/metadata routes
 * through the registry and never falls back to another provider automatically.
 */
#include "as_generation.h"
#include "as_core.h"
#include "as_json.h"

#include <string.h>

static gchar *strip_json_text(const gchar *text)
{
    if (!text)
        return NULL;
    const gchar *start = strchr(text, '{');
    const gchar *end = strrchr(text, '}');
    if (!start || !end || end <= start)
        return g_strdup(text);
    return g_strndup(start, (gsize) (end - start + 1));
}

static gchar *get_string_alias(JsonObject *obj, const gchar *a, const gchar *b,
                               const gchar *c)
{
    gchar *value = NULL;
    if (a)
        value = as_json_get_string(obj, a);
    if (!value && b)
        value = as_json_get_string(obj, b);
    if (!value && c)
        value = as_json_get_string(obj, c);
    return value;
}

static JsonArray *get_array_alias(JsonObject *obj, const gchar *a,
                                  const gchar *b)
{
    JsonArray *array = a ? as_json_get_array(obj, a) : NULL;
    if (!array && b)
        array = as_json_get_array(obj, b);
    return array;
}

static JsonObject *get_object_alias(JsonObject *obj, const gchar *a,
                                    const gchar *b)
{
    JsonObject *value = a ? as_json_get_object(obj, a) : NULL;
    if (!value && b)
        value = as_json_get_object(obj, b);
    return value;
}

static gchar *normalized_transition(const gchar *value)
{
    return g_strdup(as_transition_normalize(value && *value ? value : "crossfade"));
}

static void parse_visual_bible(JsonObject *obj, AsVisualBible *vb)
{
    vb->character_appearance =
        get_string_alias(obj, "character_appearance", "character", NULL);
    vb->clothing = as_json_get_string(obj, "clothing");
    vb->environment = as_json_get_string(obj, "environment");
    vb->locations = as_json_get_string(obj, "locations");
    vb->objects = as_json_get_string(obj, "objects");
    vb->visual_style = as_json_get_string(obj, "visual_style");
    vb->lighting = as_json_get_string(obj, "lighting");
    vb->camera_style = as_json_get_string(obj, "camera_style");
    vb->overall_atmosphere =
        get_string_alias(obj, "overall_atmosphere", "atmosphere", NULL);
    GPtrArray *palette = as_json_get_string_array(obj, "color_palette");
    if (palette) {
        g_ptr_array_unref(vb->color_palette);
        vb->color_palette = palette;
    }
}

static gboolean parse_scene_json(JsonObject *obj, gint fallback_id,
                                 const AsReelRequest *request, AsScene *scene,
                                 GError **error)
{
    scene->scene_id = (gint) as_json_get_int(obj, "scene_id", fallback_id);
    if (scene->scene_id <= 0)
        scene->scene_id = fallback_id;
    scene->narration = get_string_alias(obj, "narration", "voiceover", "script");
    scene->visual_description =
        get_string_alias(obj, "visual_description", "visual", "description");
    scene->visual_prompt =
        get_string_alias(obj, "visual_prompt", "image_prompt", "prompt");
    scene->camera = as_json_get_string(obj, "camera");
    scene->lighting = as_json_get_string(obj, "lighting");
    gchar *transition = as_json_get_string(obj, "transition");
    scene->transition = normalized_transition(transition);
    g_free(transition);
    scene->on_screen_text =
        get_string_alias(obj, "on_screen_text", "headline", "onScreenText");
    scene->subtitle_text = get_string_alias(obj, "subtitle_text", "subtitle", NULL);

    if (!scene->visual_prompt && scene->visual_description)
        scene->visual_prompt = g_strdup(scene->visual_description);
    if (!scene->visual_prompt) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "Scene %d has no visual prompt.", scene->scene_id);
        return FALSE;
    }
    if (!scene->narration) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "Scene %d has no narration.", scene->scene_id);
        return FALSE;
    }

    GPtrArray *keywords = as_json_get_string_array(obj, "keywords");
    if (keywords) {
        g_ptr_array_unref(scene->keywords);
        scene->keywords = keywords;
    }

    scene->duration = as_json_get_double(obj, "duration", 0);
    if (scene->duration <= 0.5)
        scene->duration = as_speech_duration_estimate(scene->narration);

    JsonArray *timings = get_array_alias(obj, "word_timings", "words");
    if (timings) {
        for (guint i = 0; i < json_array_get_length(timings); i++) {
            JsonNode *node = json_array_get_element(timings, i);
            if (!JSON_NODE_HOLDS_OBJECT(node))
                continue;
            JsonObject *item = json_node_get_object(node);
            gchar *word = as_json_get_string(item, "word");
            if (!word)
                word = as_json_get_string(item, "text");
            if (!word || !*word) {
                g_free(word);
                continue;
            }
            AsWordTiming *wt = g_new0(AsWordTiming, 1);
            wt->word = word;
            wt->start = as_json_get_double(item, "start", 0);
            wt->end = as_json_get_double(item, "end", wt->start);
            wt->is_keyword = as_json_get_bool(item, "is_keyword", FALSE);
            g_ptr_array_add(scene->word_timings, wt);
        }
        if (scene->word_timings->len)
            scene->timing_estimated = FALSE;
    }
    (void) request;
    return TRUE;
}

static void parse_plan_object(JsonObject *obj, AsContentPlan *plan)
{
    plan->topic = as_json_get_string(obj, "topic");
    plan->purpose = as_json_get_string(obj, "purpose");
    plan->target_audience =
        get_string_alias(obj, "target_audience", "audience", NULL);
    plan->tone = as_json_get_string(obj, "tone");
    plan->story_structure =
        get_string_alias(obj, "story_structure", "structure", NULL);
    plan->hook = as_json_get_string(obj, "hook");
    plan->ending = as_json_get_string(obj, "ending");
    plan->call_to_action =
        get_string_alias(obj, "call_to_action", "cta", NULL);
    plan->pacing = as_json_get_string(obj, "pacing");
    JsonObject *vb = get_object_alias(obj, "visual_bible", "visualBible");
    if (vb)
        parse_visual_bible(vb, &plan->visual_bible);
}

static void parse_metadata_object(JsonObject *obj, AsMetadata *meta)
{
    meta->reel_name = get_string_alias(obj, "reel_name", "name", NULL);
    meta->youtube_title =
        get_string_alias(obj, "youtube_title", "title", NULL);
    meta->youtube_description =
        get_string_alias(obj, "youtube_description", "description", NULL);
    meta->hook_analysis = as_json_get_string(obj, "hook_analysis");
    meta->audience_fit = as_json_get_string(obj, "audience_fit");
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

static void normalize_scene_durations(AsReel *reel, gint target)
{
    gdouble sum = 0;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        if (scene->duration < 1.0)
            scene->duration = 1.0;
        sum += scene->duration;
    }
    if (sum <= 0 || reel->scenes->len == 0)
        return;
    gdouble scale = target > 0 ? (gdouble) target / sum : 1.0;
    gdouble assigned = 0;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        if (i + 1 == reel->scenes->len) {
            scene->duration = MAX(1.0, (gdouble) target - assigned);
        } else {
            scene->duration = MAX(1.0, scene->duration * scale);
            assigned += scene->duration;
        }
    }
    gdouble total = 0;
    for (guint i = 0; i < reel->scenes->len; i++)
        total += ((AsScene *) g_ptr_array_index(reel->scenes, i))->duration;
    reel->total_duration = total;
}

gboolean as_generation_parse_plan_json(const gchar *json,
                                       const AsReelRequest *request,
                                       AsReel *reel, GError **error)
{
    g_return_val_if_fail(reel != NULL && request != NULL, FALSE);
    gchar *clean = strip_json_text(json);
    if (!clean) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The planning provider returned no content.");
        return FALSE;
    }
    GError *local = NULL;
    JsonNode *node = as_json_parse(clean, -1, &local);
    g_free(clean);
    if (!node) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The planning provider returned invalid JSON: %s",
                    local->message);
        g_clear_error(&local);
        return FALSE;
    }
    JsonObject *root = as_json_root_object(node);
    if (!root) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The planning response is not a JSON object.");
        json_node_free(node);
        return FALSE;
    }

    as_reel_clear(reel);
    as_reel_init(reel);
    as_request_copy(&reel->request, request);
    reel->id = as_uuid_random();
    reel->created_at = as_now_iso8601();

    JsonArray *scenes = get_array_alias(root, "scenes", "storyboard");
    if (!scenes) {
        JsonObject *board = get_object_alias(root, "storyboard", "story_board");
        if (board)
            scenes = get_array_alias(board, "scenes", "storyboard");
    }
    if (!scenes || json_array_get_length(scenes) == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The planning response contains no scenes.");
        json_node_free(node);
        return FALSE;
    }
    if (json_array_get_length(scenes) > 12) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                    "The planning response contains too many scenes (%u).",
                    json_array_get_length(scenes));
        json_node_free(node);
        return FALSE;
    }
    for (guint i = 0; i < json_array_get_length(scenes); i++) {
        JsonNode *scene_node = json_array_get_element(scenes, i);
        if (!JSON_NODE_HOLDS_OBJECT(scene_node)) {
            g_set_error(error, AS_ERROR, AS_ERROR_INVALID_RESPONSE,
                        "Scene %u is not an object.", i + 1);
            json_node_free(node);
            return FALSE;
        }
        AsScene *scene = as_scene_new();
        if (!parse_scene_json(json_node_get_object(scene_node), (gint) i + 1,
                              request, scene, error)) {
            as_scene_free(scene);
            json_node_free(node);
            return FALSE;
        }
        g_ptr_array_add(reel->scenes, scene);
    }

    JsonObject *plan = get_object_alias(root, "plan", "content_plan");
    if (plan)
        parse_plan_object(plan, &reel->plan);
    JsonObject *meta = get_object_alias(root, "metadata", "youtube_metadata");
    if (meta)
        parse_metadata_object(meta, &reel->metadata);
    gchar *reel_name = as_json_get_string(root, "reel_name");
    if (reel_name && !reel->metadata.reel_name)
        reel->metadata.reel_name = g_strdup(reel_name);
    g_free(reel_name);

    normalize_scene_durations(reel, request->duration);
    reel->plan.scene_count = (gint) reel->scenes->len;
    reel->plan.estimated_total_duration = reel->total_duration;
    if (!reel->plan.topic)
        reel->plan.topic = g_strdup(request->prompt);
    if (!reel->metadata.reel_name)
        reel->metadata.reel_name = g_strdup(reel->plan.topic);

    json_node_free(node);
    as_generation_finalize_captions(reel);
    if (!as_reel_validate(reel, error))
        return FALSE;
    return TRUE;
}

void as_generation_finalize_captions(AsReel *reel)
{
    if (!reel)
        return;
    GString *narration = g_string_new(NULL);
    gdouble total = 0;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        total += scene->duration;
        if (narration->len && scene->narration)
            g_string_append_c(narration, ' ');
        if (scene->narration)
            g_string_append(narration, scene->narration);
        if (scene->word_timings->len == 0) {
            GPtrArray *estimated = as_word_timings_estimate(
                scene->narration, scene->duration, scene->keywords);
            for (guint k = 0; k < estimated->len; k++)
                g_ptr_array_add(scene->word_timings,
                                as_word_timing_copy(g_ptr_array_index(estimated, k)));
            g_ptr_array_unref(estimated);
            scene->timing_estimated = TRUE;
        }
        if (!scene->subtitle_text)
            scene->subtitle_text = g_strdup(scene->narration);
        if (!scene->transition)
            scene->transition = g_strdup("crossfade");
        else {
            gchar *normalized = normalized_transition(scene->transition);
            g_free(scene->transition);
            scene->transition = normalized;
        }
    }
    g_free(reel->narration_text);
    reel->narration_text = g_string_free(narration, FALSE);
    reel->total_duration = total;
    g_free(reel->timing_source);
    gboolean any_estimated = FALSE;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        if (scene->timing_estimated)
            any_estimated = TRUE;
    }
    reel->timing_source = g_strdup(any_estimated ? "estimated" : "provider");
    if (!reel->music_mood) {
        reel->music_mood = g_strdup(as_music_mood_for_styles(
            reel->request.voice_style, reel->request.visual_style));
    }
}

static gchar *build_planning_system_prompt(void)
{
    return g_strdup(
        "You are a senior short-form video director. Return ONLY one valid JSON "
        "object with this exact shape:\n"
        "{\"reel_name\":\"...\",\"plan\":{\"topic\":\"...\",\"purpose\":\"...\","
        "\"target_audience\":\"...\",\"tone\":\"...\",\"story_structure\":\"...\","
        "\"hook\":\"...\",\"ending\":\"...\",\"call_to_action\":\"...\","
        "\"pacing\":\"...\",\"visual_bible\":{\"character_appearance\":\"...\","
        "\"clothing\":\"...\",\"environment\":\"...\",\"locations\":\"...\","
        "\"objects\":\"...\",\"visual_style\":\"...\",\"lighting\":\"...\","
        "\"color_palette\":[\"...\"],\"camera_style\":\"...\","
        "\"overall_atmosphere\":\"...\"}},\"scenes\":[{\"scene_id\":1,"
        "\"duration\":2.5,\"narration\":\"spoken words\","
        "\"visual_description\":\"...\",\"visual_prompt\":\"detailed image "
        "generation prompt with no text or watermark\",\"camera\":\"...\","
        "\"lighting\":\"...\",\"transition\":\"crossfade|zoom-in|pan-left|"
        "glitch|fade-black|wipe\",\"on_screen_text\":\"short headline\","
        "\"subtitle_text\":\"caption line\",\"keywords\":[\"...\"]}],"
        "\"metadata\":{\"youtube_title\":\"...\",\"youtube_description\":\"...\","
        "\"tags\":[\"...\"],\"hashtags\":[\"#...\"],\"hook_analysis\":\"...\","
        "\"audience_fit\":\"...\"}}\n"
        "Scenes must total the requested duration, use 4-9 scenes, tell one "
        "coherent story with a strong hook, and never mention that you are an "
        "AI. Do not wrap JSON in markdown fences.");
}

gboolean as_generation_plan(AsGenerationContext *ctx,
                            const AsReelRequest *request, AsReel **out,
                            GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(ctx && ctx->providers && request && out, FALSE);
    *out = NULL;
    if (!as_request_validate(request, error))
        return FALSE;
    const gchar *model = NULL;
    AsProviderConfig *cfg = as_provider_registry_resolve(
        ctx->providers, AS_ROUTE_PLANNING, &model, error);
    if (!cfg)
        return FALSE;
    gchar *key = as_credential_manager_get(ctx->credentials, cfg->id, error);
    if (!key)
        return FALSE;
    if (*key)
        as_redact_register_secret(key);

    gchar *system = build_planning_system_prompt();
    gchar *user = g_strdup_printf(
        "Create a %d-second %s reel. Voice style: %s. Visual style: %s.\n"
        "User request:\n%s",
        request->duration, as_aspect_to_string(request->aspect_ratio),
        request->voice_style, request->visual_style, request->prompt);
    AsTextResult result;
    memset(&result, 0, sizeof(result));
    gboolean ok = as_provider_generate_text(cfg, key, model, system, user, TRUE,
                                            0.85, &result, cancellable, error);
    g_free(system);
    g_free(user);
    as_secure_clear(key);
    if (!ok)
        return FALSE;

    AsReel *reel = as_reel_new();
    ok = as_generation_parse_plan_json(result.text, request, reel, error);
    as_text_result_clear(&result);
    if (!ok) {
        as_reel_free(reel);
        return FALSE;
    }
    *out = reel;
    return TRUE;
}

static gboolean generate_metadata(AsGenerationContext *ctx, AsReel *reel,
                                  GCancellable *cancellable, GError **error)
{
    const gchar *model = NULL;
    AsProviderConfig *cfg = as_provider_registry_resolve(
        ctx->providers, AS_ROUTE_METADATA, &model, NULL);
    if (!cfg)
        return TRUE; /* deterministic fallback below */

    gchar *key = as_credential_manager_get(ctx->credentials, cfg->id, NULL);
    if (!key)
        return TRUE;
    if (*key)
        as_redact_register_secret(key);
    GString *synopsis = g_string_new(NULL);
    g_string_append_printf(synopsis, "Topic: %s\nHook: %s\nNarration:\n",
                           reel->plan.topic ? reel->plan.topic : "",
                           reel->plan.hook ? reel->plan.hook : "");
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        g_string_append_printf(synopsis, "%d. %s\n", scene->scene_id,
                               scene->narration ? scene->narration : "");
    }
    gchar *system = g_strdup(
        "Return ONLY a JSON object with keys youtube_title (under 100 chars), "
        "youtube_description (2-4 sentences plus a call to action), tags "
        "(8-15 plain SEO tags), hashtags (5-8 starting with #), hook_analysis, "
        "audience_fit and reel_name. No markdown fences.");
    AsTextResult result;
    memset(&result, 0, sizeof(result));
    gboolean ok = as_provider_generate_text(cfg, key, model, system,
                                            synopsis->str, TRUE, 0.7, &result,
                                            cancellable, NULL);
    g_free(system);
    g_string_free(synopsis, TRUE);
    as_secure_clear(key);
    if (!ok) {
        as_text_result_clear(&result);
        return TRUE;
    }
    gchar *clean = strip_json_text(result.text);
    JsonNode *node = as_json_parse(clean ? clean : "", -1, NULL);
    g_free(clean);
    if (node) {
        JsonObject *root = as_json_root_object(node);
        if (root)
            parse_metadata_object(root, &reel->metadata);
        json_node_free(node);
    }
    as_text_result_clear(&result);
    return TRUE;
}

static void metadata_fallback(AsReel *reel)
{
    if (!reel->metadata.reel_name)
        reel->metadata.reel_name = g_strdup(reel->plan.topic
                                                ? reel->plan.topic
                                                : "AI Reel");
    if (!reel->metadata.youtube_title) {
        gchar *topic = reel->plan.topic ? reel->plan.topic : "AI Reel";
        reel->metadata.youtube_title = g_strdup_printf("%s #shorts", topic);
    }
    if (!reel->metadata.youtube_description) {
        GString *description = g_string_new(NULL);
        if (reel->plan.hook)
            g_string_append_printf(description, "%s\n\n", reel->plan.hook);
        for (guint i = 0; i < reel->scenes->len && i < 3; i++) {
            AsScene *scene = g_ptr_array_index(reel->scenes, i);
            if (scene->narration)
                g_string_append_printf(description, "%s ", scene->narration);
        }
        if (reel->plan.call_to_action)
            g_string_append_printf(description, "\n\n%s",
                                   reel->plan.call_to_action);
        reel->metadata.youtube_description = g_string_free(description, FALSE);
    }
    if (reel->metadata.tags->len == 0) {
        const gchar *candidates[] = { "shorts", "reels", "explainer",
                                      "storytelling", NULL };
        for (gint i = 0; candidates[i]; i++)
            g_ptr_array_add(reel->metadata.tags, g_strdup(candidates[i]));
        for (guint i = 0; i < reel->scenes->len; i++) {
            AsScene *scene = g_ptr_array_index(reel->scenes, i);
            for (guint k = 0; k < scene->keywords->len && k < 2; k++)
                g_ptr_array_add(reel->metadata.tags,
                                g_strdup(g_ptr_array_index(scene->keywords, k)));
        }
    }
    if (reel->metadata.hashtags->len == 0) {
        g_ptr_array_add(reel->metadata.hashtags, g_strdup("#shorts"));
        g_ptr_array_add(reel->metadata.hashtags, g_strdup("#reels"));
        g_ptr_array_add(reel->metadata.hashtags, g_strdup("#story"));
    }
}

gboolean as_generation_attach_metadata(AsGenerationContext *ctx, AsReel *reel,
                                       GCancellable *cancellable, GError **error)
{
    (void) error;
    generate_metadata(ctx, reel, cancellable, NULL);
    metadata_fallback(reel);
    return TRUE;
}

gboolean as_generation_save(AsGenerationContext *ctx, const gchar *project_id,
                            AsReel *reel, const gchar *status, GError **error)
{
    if (!ctx || !ctx->database)
        return FALSE;
    return as_project_save_reel(ctx->database, project_id, reel, status, error);
}
