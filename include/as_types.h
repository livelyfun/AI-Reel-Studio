/* as_types.h — core immutable-ish data model shared by all modules.
 *
 * Ownership: every struct owns the strings/arrays stored in it and must be
 * released with its matching *_free() or *_clear() function. Functions that
 * return a struct by value transfer owned pointers to the caller.
 */
#pragma once

#include <glib.h>

typedef enum {
    AS_ASPECT_9_16 = 0,
    AS_ASPECT_16_9,
    AS_ASPECT_1_1
} AsAspectRatio;

typedef enum {
    AS_CAPTION_TIKTOK = 0,
    AS_CAPTION_KARAOKE,
    AS_CAPTION_CINEMATIC,
    AS_CAPTION_MINIMAL,
    AS_CAPTION_NEON
} AsCaptionStyle;

typedef enum {
    AS_TRANSITION_CROSSFADE = 0,
    AS_TRANSITION_ZOOM_IN,
    AS_TRANSITION_PAN_LEFT,
    AS_TRANSITION_GLITCH,
    AS_TRANSITION_FADE_BLACK,
    AS_TRANSITION_WIPE
} AsTransition;

typedef struct {
    gchar *word;
    gdouble start;
    gdouble end;
    gboolean is_keyword;
} AsWordTiming;

typedef struct {
    gint scene_id;
    gdouble duration;
    gchar *narration;
    gchar *visual_description;
    gchar *visual_prompt;
    gchar *camera;
    gchar *lighting;
    gchar *transition;          /* canonical string, validated */
    gchar *on_screen_text;
    gchar *subtitle_text;
    GPtrArray *keywords;        /* gchar* */
    GPtrArray *word_timings;    /* AsWordTiming* */
    gchar *image_path;          /* absolute local file or NULL */
    gchar *image_source_url;
    gchar *image_author;
    gchar *image_license;
    gchar *image_license_url;
    gchar *image_attribution;
    gchar *audio_path;          /* absolute local file or NULL */
    gboolean timing_estimated;
} AsScene;

typedef struct {
    gchar *character_appearance;
    gchar *clothing;
    gchar *environment;
    gchar *locations;
    gchar *objects;
    gchar *visual_style;
    gchar *lighting;
    GPtrArray *color_palette;   /* gchar* */
    gchar *camera_style;
    gchar *overall_atmosphere;
} AsVisualBible;

typedef struct {
    gchar *topic;
    gchar *purpose;
    gchar *target_audience;
    gchar *tone;
    gchar *story_structure;
    gchar *hook;
    gchar *ending;
    gchar *call_to_action;
    gchar *pacing;
    gint scene_count;
    gdouble estimated_total_duration;
    AsVisualBible visual_bible;
} AsContentPlan;

typedef struct {
    gchar *reel_name;
    gchar *youtube_title;
    gchar *youtube_description;
    GPtrArray *tags;            /* gchar* */
    GPtrArray *hashtags;        /* gchar* */
    gchar *hook_analysis;
    gchar *audience_fit;
} AsMetadata;

typedef struct {
    gchar *prompt;
    gchar *voice_style;
    gchar *visual_style;
    gint duration;              /* 15, 30, 45, 60 */
    AsAspectRatio aspect_ratio;
} AsReelRequest;

typedef struct {
    gchar *id;
    gchar *created_at;
    AsReelRequest request;
    AsContentPlan plan;
    GPtrArray *scenes;          /* AsScene* */
    AsMetadata metadata;
    gdouble total_duration;
    gchar *music_mood;
    gchar *thumbnail_path;
    AsCaptionStyle caption_style;
    gchar *narration_text;
    gchar *narration_path;
    gchar *timing_source;       /* "estimated" or "provider" */
    gchar *last_warning;        /* user-facing, redacted */
} AsReel;

/* ---- lifecycle ---- */
void as_word_timing_free(AsWordTiming *wt);
AsWordTiming *as_word_timing_copy(const AsWordTiming *wt);
void as_scene_init(AsScene *scene);
void as_scene_clear(AsScene *scene);
AsScene *as_scene_new(void);
AsScene *as_scene_copy(const AsScene *scene);
void as_scene_free(AsScene *scene);
void as_visual_bible_init(AsVisualBible *vb);
void as_visual_bible_clear(AsVisualBible *vb);
void as_content_plan_init(AsContentPlan *plan);
void as_content_plan_clear(AsContentPlan *plan);
void as_metadata_init(AsMetadata *meta);
void as_metadata_clear(AsMetadata *meta);
void as_request_init(AsReelRequest *req);
void as_request_clear(AsReelRequest *req);
void as_request_copy(AsReelRequest *dst, const AsReelRequest *src);
void as_reel_init(AsReel *reel);
void as_reel_clear(AsReel *reel);
AsReel *as_reel_new(void);
void as_reel_free(AsReel *reel);
AsReel *as_reel_copy(const AsReel *reel);

/* ---- enums <-> stable storage strings ---- */
const gchar *as_aspect_to_string(AsAspectRatio ratio);
AsAspectRatio as_aspect_from_string(const gchar *s);
void as_aspect_dimensions(AsAspectRatio ratio, gint *w, gint *h);
const gchar *as_caption_to_string(AsCaptionStyle style);
AsCaptionStyle as_caption_from_string(const gchar *s);
const gchar *as_caption_display_name(AsCaptionStyle style);
const gchar *as_transition_to_string(AsTransition t);
AsTransition as_transition_from_string(const gchar *s);
const gchar *as_transition_normalize(const gchar *s);
const gchar *as_ffmpeg_xfade(AsTransition t);

/* ---- catalogues (preserved from the web application) ---- */
extern const gchar *const AS_VOICE_STYLES[];
extern const gsize AS_VOICE_STYLE_COUNT;
extern const gchar *const AS_VISUAL_STYLES[];
extern const gsize AS_VISUAL_STYLE_COUNT;
extern const gint AS_DURATIONS[];
extern const gsize AS_DURATION_COUNT;
extern const gchar *const AS_MUSIC_MOODS[];
extern const gsize AS_MUSIC_MOOD_COUNT;

gboolean as_voice_style_valid(const gchar *s);
gboolean as_visual_style_valid(const gchar *s);
gboolean as_duration_valid(gint duration);

/* Sample prompts preserved from the creator UI. */
typedef struct {
    const gchar *title;
    const gchar *prompt;
    const gchar *voice;
    const gchar *visual;
    gint duration;
} AsSamplePrompt;
extern const AsSamplePrompt AS_SAMPLE_PROMPTS[];
extern const gsize AS_SAMPLE_PROMPT_COUNT;

/* ---- validation/normalisation ---- */
gboolean as_request_validate(const AsReelRequest *req, GError **error);
gboolean as_scene_validate(const AsScene *scene, GError **error);
gboolean as_reel_validate(const AsReel *reel, GError **error);

/* Estimated word timings are generated only when a provider has not supplied
 * real timing data. They are explicitly marked on the scene and in exports. */
GPtrArray *as_word_timings_estimate(const gchar *narration, gdouble duration,
                                    const GPtrArray *keywords);
