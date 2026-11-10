/* types.c — data model lifecycle, catalogues, validation, safe copying. */
#include "as_types.h"
#include "as_core.h"

#include <string.h>

const gchar *const AS_VOICE_STYLES[] = {
    "Cinematic", "Deep", "Energetic", "Dramatic", "Storytelling",
    "Documentary", "Motivational", "Calm", "Friendly", "News"
};
const gsize AS_VOICE_STYLE_COUNT = G_N_ELEMENTS(AS_VOICE_STYLES);

const gchar *const AS_VISUAL_STYLES[] = {
    "Cinematic", "Photorealistic", "Sci-Fi", "Dark / Mystery", "Documentary",
    "3D Animation", "Anime", "Fantasy", "Educational", "Fast-Paced Social Media",
    "Minimal", "Custom"
};
const gsize AS_VISUAL_STYLE_COUNT = G_N_ELEMENTS(AS_VISUAL_STYLES);

const gint AS_DURATIONS[] = { 15, 30, 45, 60 };
const gsize AS_DURATION_COUNT = G_N_ELEMENTS(AS_DURATIONS);

const gchar *const AS_MUSIC_MOODS[] = {
    "Cinematic", "Electronic", "Dramatic", "Ambient", "Motivational"
};
const gsize AS_MUSIC_MOOD_COUNT = G_N_ELEMENTS(AS_MUSIC_MOODS);

const AsSamplePrompt AS_SAMPLE_PROMPTS[] = {
    { "Black Hole Mystery",
      "Create a cinematic 45-second reel explaining why black holes are so mysterious. Make it exciting, dramatic and easy for teenagers to understand.",
      "Cinematic", "Sci-Fi", 45 },
    { "How Pyramids Were Built",
      "A gripping 30-second reel revealing the lost engineering secret of the Great Pyramid of Giza. Start with a shocking fact.",
      "Documentary", "Photorealistic", 30 },
    { "The $100B AI Chip War",
      "An energetic 30-second breakdown of why silicon microchips are the new oil of the 21st century. Fast-paced and high retention.",
      "Energetic", "Fast-Paced Social Media", 30 },
    { "Why You Procrastinate",
      "A psychological 30-second reel explaining the battle between your amygdala and prefrontal cortex when you delay work. Offer one instant fix.",
      "Storytelling", "3D Animation", 30 }
};
const gsize AS_SAMPLE_PROMPT_COUNT = G_N_ELEMENTS(AS_SAMPLE_PROMPTS);

/* ---------- small helpers ---------- */

static void clear_string(gchar **s)
{
    if (*s) {
        g_free(*s);
        *s = NULL;
    }
}

static gchar *copy_string(const gchar *s)
{
    return s ? g_strdup(s) : NULL;
}

static GPtrArray *string_array_copy(GPtrArray *array)
{
    GPtrArray *copy = g_ptr_array_new_with_free_func(g_free);
    if (array) {
        for (guint i = 0; i < array->len; i++)
            g_ptr_array_add(copy, g_strdup(g_ptr_array_index(array, i)));
    }
    return copy;
}

static GPtrArray *string_array_new(void)
{
    return g_ptr_array_new_with_free_func(g_free);
}

/* ---------- word timings ---------- */

void as_word_timing_free(AsWordTiming *wt)
{
    if (!wt)
        return;
    g_free(wt->word);
    g_free(wt);
}

AsWordTiming *as_word_timing_copy(const AsWordTiming *wt)
{
    if (!wt)
        return NULL;
    AsWordTiming *copy = g_new0(AsWordTiming, 1);
    copy->word = g_strdup(wt->word);
    copy->start = wt->start;
    copy->end = wt->end;
    copy->is_keyword = wt->is_keyword;
    return copy;
}

/* ---------- scene ---------- */

void as_scene_init(AsScene *scene)
{
    memset(scene, 0, sizeof(*scene));
    scene->keywords = string_array_new();
    scene->word_timings = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_word_timing_free);
    scene->timing_estimated = TRUE;
}

void as_scene_clear(AsScene *scene)
{
    if (!scene)
        return;
    clear_string(&scene->narration);
    clear_string(&scene->visual_description);
    clear_string(&scene->visual_prompt);
    clear_string(&scene->camera);
    clear_string(&scene->lighting);
    clear_string(&scene->transition);
    clear_string(&scene->on_screen_text);
    clear_string(&scene->subtitle_text);
    clear_string(&scene->image_path);
    clear_string(&scene->image_source_url);
    clear_string(&scene->image_author);
    clear_string(&scene->image_license);
    clear_string(&scene->image_license_url);
    clear_string(&scene->image_attribution);
    clear_string(&scene->audio_path);
    if (scene->keywords) {
        g_ptr_array_unref(scene->keywords);
        scene->keywords = NULL;
    }
    if (scene->word_timings) {
        g_ptr_array_unref(scene->word_timings);
        scene->word_timings = NULL;
    }
}

AsScene *as_scene_new(void)
{
    AsScene *scene = g_new0(AsScene, 1);
    as_scene_init(scene);
    return scene;
}

AsScene *as_scene_copy(const AsScene *scene)
{
    if (!scene)
        return NULL;
    AsScene *copy = as_scene_new();
    copy->scene_id = scene->scene_id;
    copy->duration = scene->duration;
    copy->narration = copy_string(scene->narration);
    copy->visual_description = copy_string(scene->visual_description);
    copy->visual_prompt = copy_string(scene->visual_prompt);
    copy->camera = copy_string(scene->camera);
    copy->lighting = copy_string(scene->lighting);
    copy->transition = copy_string(scene->transition);
    copy->on_screen_text = copy_string(scene->on_screen_text);
    copy->subtitle_text = copy_string(scene->subtitle_text);
    copy->image_path = copy_string(scene->image_path);
    copy->image_source_url = copy_string(scene->image_source_url);
    copy->image_author = copy_string(scene->image_author);
    copy->image_license = copy_string(scene->image_license);
    copy->image_license_url = copy_string(scene->image_license_url);
    copy->image_attribution = copy_string(scene->image_attribution);
    copy->audio_path = copy_string(scene->audio_path);
    copy->timing_estimated = scene->timing_estimated;
    g_ptr_array_unref(copy->keywords);
    copy->keywords = string_array_copy(scene->keywords);
    g_ptr_array_unref(copy->word_timings);
    copy->word_timings = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_word_timing_free);
    for (guint i = 0; i < scene->word_timings->len; i++) {
        g_ptr_array_add(copy->word_timings, as_word_timing_copy(
            g_ptr_array_index(scene->word_timings, i)));
    }
    return copy;
}

void as_scene_free(AsScene *scene)
{
    if (!scene)
        return;
    as_scene_clear(scene);
    g_free(scene);
}

/* ---------- visual bible / plan / metadata / request ---------- */

void as_visual_bible_init(AsVisualBible *vb)
{
    memset(vb, 0, sizeof(*vb));
    vb->color_palette = string_array_new();
}

void as_visual_bible_clear(AsVisualBible *vb)
{
    if (!vb)
        return;
    clear_string(&vb->character_appearance);
    clear_string(&vb->clothing);
    clear_string(&vb->environment);
    clear_string(&vb->locations);
    clear_string(&vb->objects);
    clear_string(&vb->visual_style);
    clear_string(&vb->lighting);
    clear_string(&vb->camera_style);
    clear_string(&vb->overall_atmosphere);
    if (vb->color_palette) {
        g_ptr_array_unref(vb->color_palette);
        vb->color_palette = NULL;
    }
}

static void visual_bible_copy(AsVisualBible *dst, const AsVisualBible *src)
{
    as_visual_bible_init(dst);
    dst->character_appearance = copy_string(src->character_appearance);
    dst->clothing = copy_string(src->clothing);
    dst->environment = copy_string(src->environment);
    dst->locations = copy_string(src->locations);
    dst->objects = copy_string(src->objects);
    dst->visual_style = copy_string(src->visual_style);
    dst->lighting = copy_string(src->lighting);
    dst->camera_style = copy_string(src->camera_style);
    dst->overall_atmosphere = copy_string(src->overall_atmosphere);
    g_ptr_array_unref(dst->color_palette);
    dst->color_palette = string_array_copy(src->color_palette);
}

void as_content_plan_init(AsContentPlan *plan)
{
    memset(plan, 0, sizeof(*plan));
    as_visual_bible_init(&plan->visual_bible);
}

void as_content_plan_clear(AsContentPlan *plan)
{
    if (!plan)
        return;
    clear_string(&plan->topic);
    clear_string(&plan->purpose);
    clear_string(&plan->target_audience);
    clear_string(&plan->tone);
    clear_string(&plan->story_structure);
    clear_string(&plan->hook);
    clear_string(&plan->ending);
    clear_string(&plan->call_to_action);
    clear_string(&plan->pacing);
    as_visual_bible_clear(&plan->visual_bible);
}

void as_metadata_init(AsMetadata *meta)
{
    memset(meta, 0, sizeof(*meta));
    meta->tags = string_array_new();
    meta->hashtags = string_array_new();
}

void as_metadata_clear(AsMetadata *meta)
{
    if (!meta)
        return;
    clear_string(&meta->reel_name);
    clear_string(&meta->youtube_title);
    clear_string(&meta->youtube_description);
    clear_string(&meta->hook_analysis);
    clear_string(&meta->audience_fit);
    if (meta->tags) {
        g_ptr_array_unref(meta->tags);
        meta->tags = NULL;
    }
    if (meta->hashtags) {
        g_ptr_array_unref(meta->hashtags);
        meta->hashtags = NULL;
    }
}

void as_request_init(AsReelRequest *req)
{
    memset(req, 0, sizeof(*req));
    req->duration = 30;
    req->aspect_ratio = AS_ASPECT_9_16;
}

void as_request_clear(AsReelRequest *req)
{
    if (!req)
        return;
    clear_string(&req->prompt);
    clear_string(&req->voice_style);
    clear_string(&req->visual_style);
}

void as_request_copy(AsReelRequest *dst, const AsReelRequest *src)
{
    as_request_init(dst);
    dst->prompt = copy_string(src->prompt);
    dst->voice_style = copy_string(src->voice_style);
    dst->visual_style = copy_string(src->visual_style);
    dst->duration = src->duration;
    dst->aspect_ratio = src->aspect_ratio;
}

/* ---------- reel ---------- */

void as_reel_init(AsReel *reel)
{
    memset(reel, 0, sizeof(*reel));
    as_request_init(&reel->request);
    as_content_plan_init(&reel->plan);
    as_metadata_init(&reel->metadata);
    reel->scenes = g_ptr_array_new_with_free_func((GDestroyNotify) as_scene_free);
    reel->caption_style = AS_CAPTION_TIKTOK;
    reel->timing_source = g_strdup("estimated");
}

void as_reel_clear(AsReel *reel)
{
    if (!reel)
        return;
    clear_string(&reel->id);
    clear_string(&reel->created_at);
    as_request_clear(&reel->request);
    as_content_plan_clear(&reel->plan);
    as_metadata_clear(&reel->metadata);
    if (reel->scenes) {
        g_ptr_array_unref(reel->scenes);
        reel->scenes = NULL;
    }
    clear_string(&reel->music_mood);
    clear_string(&reel->thumbnail_path);
    clear_string(&reel->narration_text);
    clear_string(&reel->narration_path);
    clear_string(&reel->timing_source);
    clear_string(&reel->last_warning);
}

AsReel *as_reel_new(void)
{
    AsReel *reel = g_new0(AsReel, 1);
    as_reel_init(reel);
    return reel;
}

AsReel *as_reel_copy(const AsReel *reel)
{
    if (!reel)
        return NULL;
    AsReel *copy = as_reel_new();
    copy->id = copy_string(reel->id);
    copy->created_at = copy_string(reel->created_at);
    as_request_clear(&copy->request);
    as_request_copy(&copy->request, &reel->request);

    as_content_plan_clear(&copy->plan);
    copy->plan.topic = copy_string(reel->plan.topic);
    copy->plan.purpose = copy_string(reel->plan.purpose);
    copy->plan.target_audience = copy_string(reel->plan.target_audience);
    copy->plan.tone = copy_string(reel->plan.tone);
    copy->plan.story_structure = copy_string(reel->plan.story_structure);
    copy->plan.hook = copy_string(reel->plan.hook);
    copy->plan.ending = copy_string(reel->plan.ending);
    copy->plan.call_to_action = copy_string(reel->plan.call_to_action);
    copy->plan.pacing = copy_string(reel->plan.pacing);
    copy->plan.scene_count = reel->plan.scene_count;
    copy->plan.estimated_total_duration = reel->plan.estimated_total_duration;
    as_visual_bible_clear(&copy->plan.visual_bible);
    visual_bible_copy(&copy->plan.visual_bible, &reel->plan.visual_bible);

    as_metadata_clear(&copy->metadata);
    copy->metadata.reel_name = copy_string(reel->metadata.reel_name);
    copy->metadata.youtube_title = copy_string(reel->metadata.youtube_title);
    copy->metadata.youtube_description = copy_string(reel->metadata.youtube_description);
    copy->metadata.hook_analysis = copy_string(reel->metadata.hook_analysis);
    copy->metadata.audience_fit = copy_string(reel->metadata.audience_fit);
    g_ptr_array_unref(copy->metadata.tags);
    copy->metadata.tags = string_array_copy(reel->metadata.tags);
    g_ptr_array_unref(copy->metadata.hashtags);
    copy->metadata.hashtags = string_array_copy(reel->metadata.hashtags);

    g_ptr_array_unref(copy->scenes);
    copy->scenes = g_ptr_array_new_with_free_func((GDestroyNotify) as_scene_free);
    for (guint i = 0; i < reel->scenes->len; i++)
        g_ptr_array_add(copy->scenes, as_scene_copy(g_ptr_array_index(reel->scenes, i)));

    copy->total_duration = reel->total_duration;
    copy->music_mood = copy_string(reel->music_mood);
    copy->thumbnail_path = copy_string(reel->thumbnail_path);
    copy->caption_style = reel->caption_style;
    copy->narration_text = copy_string(reel->narration_text);
    copy->narration_path = copy_string(reel->narration_path);
    clear_string(&copy->timing_source);
    copy->timing_source = copy_string(reel->timing_source);
    copy->last_warning = copy_string(reel->last_warning);
    return copy;
}

void as_reel_free(AsReel *reel)
{
    if (!reel)
        return;
    as_reel_clear(reel);
    g_free(reel);
}

/* ---------- enum strings ---------- */

const gchar *as_aspect_to_string(AsAspectRatio ratio)
{
    switch (ratio) {
    case AS_ASPECT_16_9: return "16:9";
    case AS_ASPECT_1_1: return "1:1";
    case AS_ASPECT_9_16:
    default: return "9:16";
    }
}

AsAspectRatio as_aspect_from_string(const gchar *s)
{
    if (!s)
        return AS_ASPECT_9_16;
    if (g_strcmp0(s, "16:9") == 0)
        return AS_ASPECT_16_9;
    if (g_strcmp0(s, "1:1") == 0)
        return AS_ASPECT_1_1;
    return AS_ASPECT_9_16;
}

void as_aspect_dimensions(AsAspectRatio ratio, gint *w, gint *h)
{
    switch (ratio) {
    case AS_ASPECT_16_9:
        *w = 1920; *h = 1080; break;
    case AS_ASPECT_1_1:
        *w = 1080; *h = 1080; break;
    case AS_ASPECT_9_16:
    default:
        *w = 1080; *h = 1920; break;
    }
}

const gchar *as_caption_to_string(AsCaptionStyle style)
{
    switch (style) {
    case AS_CAPTION_KARAOKE: return "karaoke_pop";
    case AS_CAPTION_CINEMATIC: return "cinematic_glow";
    case AS_CAPTION_MINIMAL: return "minimalist_clean";
    case AS_CAPTION_NEON: return "neon_punch";
    case AS_CAPTION_TIKTOK:
    default: return "tiktok_bold";
    }
}

AsCaptionStyle as_caption_from_string(const gchar *s)
{
    if (!s)
        return AS_CAPTION_TIKTOK;
    if (g_strcmp0(s, "karaoke_pop") == 0)
        return AS_CAPTION_KARAOKE;
    if (g_strcmp0(s, "cinematic_glow") == 0)
        return AS_CAPTION_CINEMATIC;
    if (g_strcmp0(s, "minimalist_clean") == 0)
        return AS_CAPTION_MINIMAL;
    if (g_strcmp0(s, "neon_punch") == 0)
        return AS_CAPTION_NEON;
    return AS_CAPTION_TIKTOK;
}

const gchar *as_caption_display_name(AsCaptionStyle style)
{
    switch (style) {
    case AS_CAPTION_KARAOKE: return "Karaoke Pop";
    case AS_CAPTION_CINEMATIC: return "Cinematic Glow";
    case AS_CAPTION_MINIMAL: return "Minimal Clean";
    case AS_CAPTION_NEON: return "Neon Punch";
    case AS_CAPTION_TIKTOK:
    default: return "TikTok Viral";
    }
}

const gchar *as_transition_to_string(AsTransition t)
{
    switch (t) {
    case AS_TRANSITION_ZOOM_IN: return "zoom-in";
    case AS_TRANSITION_PAN_LEFT: return "pan-left";
    case AS_TRANSITION_GLITCH: return "glitch";
    case AS_TRANSITION_FADE_BLACK: return "fade-black";
    case AS_TRANSITION_WIPE: return "wipe";
    case AS_TRANSITION_CROSSFADE:
    default: return "crossfade";
    }
}

AsTransition as_transition_from_string(const gchar *s)
{
    if (!s)
        return AS_TRANSITION_CROSSFADE;
    if (g_strcmp0(s, "zoom-in") == 0 || g_strcmp0(s, "zoom_in") == 0 ||
        g_strcmp0(s, "zoom") == 0)
        return AS_TRANSITION_ZOOM_IN;
    if (g_strcmp0(s, "pan-left") == 0 || g_strcmp0(s, "pan_left") == 0 ||
        g_strcmp0(s, "slide-left") == 0)
        return AS_TRANSITION_PAN_LEFT;
    if (g_strcmp0(s, "glitch") == 0)
        return AS_TRANSITION_GLITCH;
    if (g_strcmp0(s, "fade-black") == 0 || g_strcmp0(s, "fade_black") == 0 ||
        g_strcmp0(s, "fade-to-black") == 0)
        return AS_TRANSITION_FADE_BLACK;
    if (g_strcmp0(s, "wipe") == 0 || g_strcmp0(s, "wipe-left") == 0)
        return AS_TRANSITION_WIPE;
    return AS_TRANSITION_CROSSFADE;
}

const gchar *as_transition_normalize(const gchar *s)
{
    return as_transition_to_string(as_transition_from_string(s));
}

const gchar *as_ffmpeg_xfade(AsTransition t)
{
    switch (t) {
    case AS_TRANSITION_ZOOM_IN: return "zoomin";
    case AS_TRANSITION_PAN_LEFT: return "slideleft";
    case AS_TRANSITION_GLITCH: return "pixelize";
    case AS_TRANSITION_FADE_BLACK: return "fadeblack";
    case AS_TRANSITION_WIPE: return "wipeleft";
    case AS_TRANSITION_CROSSFADE:
    default: return "fade";
    }
}

/* ---------- catalog validation ---------- */

gboolean as_voice_style_valid(const gchar *s)
{
    if (!s)
        return FALSE;
    for (gsize i = 0; i < AS_VOICE_STYLE_COUNT; i++)
        if (g_strcmp0(s, AS_VOICE_STYLES[i]) == 0)
            return TRUE;
    return FALSE;
}

gboolean as_visual_style_valid(const gchar *s)
{
    if (!s)
        return FALSE;
    for (gsize i = 0; i < AS_VISUAL_STYLE_COUNT; i++)
        if (g_strcmp0(s, AS_VISUAL_STYLES[i]) == 0)
            return TRUE;
    return FALSE;
}

gboolean as_duration_valid(gint duration)
{
    for (gsize i = 0; i < AS_DURATION_COUNT; i++)
        if (AS_DURATIONS[i] == duration)
            return TRUE;
    return FALSE;
}

/* ---------- validation ---------- */

gboolean as_request_validate(const AsReelRequest *req, GError **error)
{
    if (!req || !req->prompt || !*req->prompt) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "A non-empty reel prompt is required.");
        return FALSE;
    }
    if (strlen(req->prompt) > 20000) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The reel prompt is too long (maximum 20000 characters).");
        return FALSE;
    }
    if (!as_voice_style_valid(req->voice_style)) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Unknown voice style '%s'.", req->voice_style ? req->voice_style : "");
        return FALSE;
    }
    if (!as_visual_style_valid(req->visual_style)) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Unknown visual style '%s'.", req->visual_style ? req->visual_style : "");
        return FALSE;
    }
    if (!as_duration_valid(req->duration)) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Unsupported duration %d seconds.", req->duration);
        return FALSE;
    }
    if (req->aspect_ratio != AS_ASPECT_9_16 && req->aspect_ratio != AS_ASPECT_16_9 &&
        req->aspect_ratio != AS_ASPECT_1_1) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION, "Unsupported aspect ratio.");
        return FALSE;
    }
    return TRUE;
}

gboolean as_scene_validate(const AsScene *scene, GError **error)
{
    if (!scene) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION, "Scene is missing.");
        return FALSE;
    }
    if (scene->scene_id <= 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Scene has an invalid scene_id.");
        return FALSE;
    }
    if (!(scene->duration > 0.5) || scene->duration > 30.0) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Scene %d has an invalid duration (%.2fs).", scene->scene_id,
                    scene->duration);
        return FALSE;
    }
    if (!scene->narration || !*scene->narration) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Scene %d has no narration text.", scene->scene_id);
        return FALSE;
    }
    if (!scene->visual_prompt || !*scene->visual_prompt) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "Scene %d has no visual prompt.", scene->scene_id);
        return FALSE;
    }
    return TRUE;
}

gboolean as_reel_validate(const AsReel *reel, GError **error)
{
    if (!reel) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION, "Reel is missing.");
        return FALSE;
    }
    if (!as_request_validate(&reel->request, error))
        return FALSE;
    if (!reel->scenes || reel->scenes->len == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The storyboard contains no scenes.");
        return FALSE;
    }
    if (reel->scenes->len > 12) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The storyboard contains too many scenes (%u).", reel->scenes->len);
        return FALSE;
    }
    GHashTable *ids = g_hash_table_new(g_direct_hash, g_direct_equal);
    gdouble total = 0;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        if (!as_scene_validate(scene, error)) {
            g_hash_table_destroy(ids);
            return FALSE;
        }
        if (g_hash_table_contains(ids, GINT_TO_POINTER(scene->scene_id))) {
            g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                        "Duplicate scene id %d.", scene->scene_id);
            g_hash_table_destroy(ids);
            return FALSE;
        }
        g_hash_table_add(ids, GINT_TO_POINTER(scene->scene_id));
        total += scene->duration;
    }
    g_hash_table_destroy(ids);
    if (total <= 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_VALIDATION,
                    "The storyboard has zero total duration.");
        return FALSE;
    }
    return TRUE;
}

/* ---------- timing estimation ---------- */

static gboolean keyword_matches(const gchar *word, const GPtrArray *keywords)
{
    if (!keywords || !word)
        return FALSE;
    gchar *clean = g_ascii_strdown(word, -1);
    gchar *filtered = g_strdup(clean);
    gchar *out = filtered;
    for (const gchar *p = clean; *p; p++) {
        if (g_ascii_isalnum(*p))
            *out++ = *p;
    }
    *out = '\0';
    gboolean found = FALSE;
    for (guint i = 0; i < keywords->len && !found; i++) {
        const gchar *kw = g_ptr_array_index((GPtrArray *) keywords, i);
        if (!kw || !*kw)
            continue;
        gchar *k = g_ascii_strdown(kw, -1);
        if ((*filtered && strstr(k, filtered)) || (*k && strstr(filtered, k)))
            found = TRUE;
        g_free(k);
    }
    g_free(filtered);
    g_free(clean);
    return found;
}

GPtrArray *as_word_timings_estimate(const gchar *narration, gdouble duration,
                                    const GPtrArray *keywords)
{
    GPtrArray *timings = g_ptr_array_new_with_free_func(
        (GDestroyNotify) as_word_timing_free);
    if (!narration || !*narration || duration <= 0)
        return timings;

    g_auto(GStrv) words = g_strsplit_set(narration, " \t\n\r", -1);
    guint count = 0;
    for (guint i = 0; words[i]; i++) {
        if (*words[i])
            count++;
    }
    if (count == 0)
        return timings;

    gdouble per_word = duration / (gdouble) count;
    guint index = 0;
    for (guint i = 0; words[i]; i++) {
        if (!*words[i])
            continue;
        AsWordTiming *wt = g_new0(AsWordTiming, 1);
        wt->word = g_strdup(words[i]);
        wt->start = index * per_word;
        wt->end = (index + 1) * per_word;
        wt->is_keyword = keyword_matches(words[i], keywords);
        g_ptr_array_add(timings, wt);
        index++;
    }
    return timings;
}
