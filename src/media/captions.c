/* captions.c — subtitle sidecars and styled ASS captions for rendering. */
#include "as_media.h"
#include "as_core.h"

#include <math.h>
#include <string.h>

#include <json-glib/json-glib.h>
#include <glib/gstdio.h>

static gchar *scene_text(const AsScene *scene)
{
    if (scene->subtitle_text && *scene->subtitle_text)
        return g_strdup(scene->subtitle_text);
    if (scene->narration && *scene->narration)
        return g_strdup(scene->narration);
    if (scene->on_screen_text && *scene->on_screen_text)
        return g_strdup(scene->on_screen_text);
    return g_strdup("");
}

static gchar *single_line(const gchar *text)
{
    gchar *copy = g_strdup(text ? text : "");
    for (gchar *p = copy; *p; p++)
        if (*p == '\n' || *p == '\r')
            *p = ' ';
    g_strstrip(copy);
    return copy;
}

static void append_srt_time(GString *out, gdouble seconds)
{
    if (seconds < 0)
        seconds = 0;
    gint total_ms = (gint) llround(seconds * 1000.0);
    gint h = total_ms / 3600000;
    total_ms %= 3600000;
    gint m = total_ms / 60000;
    total_ms %= 60000;
    gint s = total_ms / 1000;
    gint ms = total_ms % 1000;
    g_string_append_printf(out, "%02d:%02d:%02d,%03d", h, m, s, ms);
}

gchar *as_captions_srt(const AsReel *reel)
{
    if (!reel)
        return g_strdup("");
    GString *out = g_string_new(NULL);
    gdouble cursor = 0;
    guint index = 1;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        gdouble start = cursor;
        gdouble end = cursor + scene->duration;
        cursor = end;
        gchar *text = scene_text(scene);
        gchar *line = single_line(text);
        g_free(text);
        if (*line) {
            g_string_append_printf(out, "%u\n", index++);
            append_srt_time(out, start);
            g_string_append(out, " --> ");
            append_srt_time(out, end);
            g_string_append_printf(out, "\n%s\n\n", line);
        }
        g_free(line);
    }
    return g_string_free(out, FALSE);
}

static void append_vtt_time(GString *out, gdouble seconds)
{
    if (seconds < 0)
        seconds = 0;
    gint total_ms = (gint) llround(seconds * 1000.0);
    gint h = total_ms / 3600000;
    total_ms %= 3600000;
    gint m = total_ms / 60000;
    total_ms %= 60000;
    gint s = total_ms / 1000;
    gint ms = total_ms % 1000;
    g_string_append_printf(out, "%02d:%02d:%02d.%03d", h, m, s, ms);
}

gchar *as_captions_vtt(const AsReel *reel)
{
    if (!reel)
        return g_strdup("WEBVTT\n");
    GString *out = g_string_new("WEBVTT\n\n");
    gdouble cursor = 0;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        gdouble start = cursor;
        gdouble end = cursor + scene->duration;
        cursor = end;
        gchar *text = scene_text(scene);
        gchar *line = single_line(text);
        if (*line) {
            append_vtt_time(out, start);
            g_string_append(out, " --> ");
            append_vtt_time(out, end);
            g_string_append_printf(out, "\n%s\n\n", line);
        }
        g_free(line);
        g_free(text);
    }
    return g_string_free(out, FALSE);
}

gchar *as_captions_json(const AsReel *reel)
{
    JsonBuilder *builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "timing_source");
    json_builder_add_string_value(builder,
                                  reel && reel->timing_source
                                      ? reel->timing_source
                                      : "estimated");
    json_builder_set_member_name(builder, "cues");
    json_builder_begin_array(builder);
    gdouble cursor = 0;
    if (reel) {
        for (guint i = 0; i < reel->scenes->len; i++) {
            AsScene *scene = g_ptr_array_index(reel->scenes, i);
            gdouble start = cursor;
            gdouble end = cursor + scene->duration;
            cursor = end;
            gchar *text = scene_text(scene);
            json_builder_begin_object(builder);
            json_builder_set_member_name(builder, "scene_id");
            json_builder_add_int_value(builder, scene->scene_id);
            json_builder_set_member_name(builder, "start");
            json_builder_add_double_value(builder, start);
            json_builder_set_member_name(builder, "end");
            json_builder_add_double_value(builder, end);
            json_builder_set_member_name(builder, "text");
            json_builder_add_string_value(builder, text);
            json_builder_set_member_name(builder, "estimated");
            json_builder_add_boolean_value(builder, scene->timing_estimated);
            g_free(text);
            json_builder_end_object(builder);
        }
    }
    json_builder_end_array(builder);
    json_builder_end_object(builder);
    JsonGenerator *generator = json_generator_new();
    JsonNode *root = json_builder_get_root(builder);
    json_generator_set_root(generator, root);
    json_generator_set_pretty(generator, TRUE);
    gchar *text = json_generator_to_data(generator, NULL);
    json_node_free(root);
    g_object_unref(generator);
    g_object_unref(builder);
    return text;
}

gchar *as_captions_metadata_text(const AsReel *reel)
{
    if (!reel)
        return g_strdup("");
    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "AI Reel Studio export\n");
    g_string_append_printf(out, "=====================\n\n");
    if (reel->metadata.reel_name)
        g_string_append_printf(out, "Reel: %s\n", reel->metadata.reel_name);
    if (reel->metadata.youtube_title)
        g_string_append_printf(out, "YouTube title: %s\n",
                               reel->metadata.youtube_title);
    if (reel->metadata.youtube_description)
        g_string_append_printf(out, "\nDescription:\n%s\n",
                               reel->metadata.youtube_description);
    if (reel->metadata.tags && reel->metadata.tags->len) {
        g_string_append(out, "\nTags: ");
        for (guint i = 0; i < reel->metadata.tags->len; i++)
            g_string_append_printf(out, "%s%s",
                                   i ? ", " : "",
                                   (gchar *) g_ptr_array_index(reel->metadata.tags, i));
        g_string_append_c(out, '\n');
    }
    if (reel->metadata.hashtags && reel->metadata.hashtags->len) {
        g_string_append(out, "\nHashtags: ");
        for (guint i = 0; i < reel->metadata.hashtags->len; i++)
            g_string_append_printf(out, "%s%s", i ? " " : "",
                                   (gchar *) g_ptr_array_index(reel->metadata.hashtags,
                                                               i));
        g_string_append_c(out, '\n');
    }
    g_string_append_printf(out, "\nDuration: %.1fs\n", reel->total_duration);
    g_string_append_printf(out, "Captions: %s timing (%s)\n",
                           reel->timing_source ? reel->timing_source : "estimated",
                           reel->timing_source &&
                                   g_strcmp0(reel->timing_source, "estimated") == 0
                               ? "word timings are estimated, not provider-reported"
                               : "provider-reported");
    g_string_append_printf(out, "Music mood: %s\n",
                           reel->music_mood ? reel->music_mood : "none");
    g_string_append(out, "\nScene media and attribution\n");
    g_string_append(out, "---------------------------\n");
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *scene = g_ptr_array_index(reel->scenes, i);
        g_string_append_printf(out, "\nScene %d (%.1fs)\n", scene->scene_id,
                               scene->duration);
        if (scene->image_author || scene->image_license) {
            g_string_append_printf(out, "  Author: %s\n",
                                   scene->image_author ? scene->image_author : "unknown");
            g_string_append_printf(out, "  License: %s\n",
                                   scene->image_license ? scene->image_license
                                                        : "unknown");
            if (scene->image_license_url)
                g_string_append_printf(out, "  License URL: %s\n",
                                       scene->image_license_url);
            if (scene->image_source_url)
                g_string_append_printf(out, "  Source: %s\n",
                                       scene->image_source_url);
        } else if (scene->image_path) {
            g_string_append_printf(out, "  Local image: %s\n", scene->image_path);
        }
    }
    return g_string_free(out, FALSE);
}

/* ---------------- ASS ---------------- */

static gchar *ass_escape(const gchar *text)
{
    GString *out = g_string_new(NULL);
    for (const gchar *p = text ? text : ""; *p; p++) {
        if (*p == '\n' || *p == '\r')
            g_string_append(out, "\\N");
        else if (*p == '{' || *p == '}')
            g_string_append_c(out, ' ');
        else
            g_string_append_c(out, *p);
    }
    return g_string_free(out, FALSE);
}

static void ass_time(GString *out, gdouble seconds)
{
    if (seconds < 0)
        seconds = 0;
    gint cs = (gint) llround(seconds * 100.0);
    gint h = cs / 360000;
    cs %= 360000;
    gint m = cs / 6000;
    cs %= 6000;
    gint s = cs / 100;
    cs %= 100;
    g_string_append_printf(out, "%d:%02d:%02d.%02d", h, m, s, cs);
}

static const gchar *ass_colour(AsCaptionStyle style)
{
    switch (style) {
    case AS_CAPTION_KARAOKE: return "&H0000E5FF";   /* warm yellow BGR */
    case AS_CAPTION_CINEMATIC: return "&H00F2F2F2";
    case AS_CAPTION_MINIMAL: return "&H00FFFFFF";
    case AS_CAPTION_NEON: return "&H00FFFF00";       /* cyan BGR */
    case AS_CAPTION_TIKTOK:
    default: return "&H00FFFFFF";
    }
}

static const gchar *ass_accent(AsCaptionStyle style)
{
    switch (style) {
    case AS_CAPTION_NEON: return "&H00FF00FF";       /* magenta */
    case AS_CAPTION_CINEMATIC: return "&H0080D0FF";
    default: return "&H0000E5FF";
    }
}

static void write_style_line(GString *out, AsCaptionStyle style, gint width,
                             gint height)
{
    gint size = (gint) (height * 0.052);
    gint outline = 4;
    gint shadow = 1;
    gint margin_v = (gint) (height * 0.13);
    const gchar *font = "DejaVu Sans";
    gint bold = -1;
    gint blur = 0;
    switch (style) {
    case AS_CAPTION_CINEMATIC:
        font = "DejaVu Serif";
        size = (gint) (height * 0.045);
        outline = 1;
        shadow = 2;
        margin_v = (gint) (height * 0.10);
        break;
    case AS_CAPTION_MINIMAL:
        outline = 0;
        shadow = 0;
        margin_v = (gint) (height * 0.09);
        break;
    case AS_CAPTION_NEON:
        size = (gint) (height * 0.056);
        outline = 3;
        blur = 3;
        break;
    case AS_CAPTION_KARAOKE:
    case AS_CAPTION_TIKTOK:
    default:
        break;
    }
    g_string_append_printf(
        out,
        "Style: Caption,%s,%d,%s,%s,&H000000FF,&H00000000,&H80000000,%d,%d,%d,%d,"
        "100,100,0,0,1,%d,0,2,40,40,%d,1\n",
        font, size, ass_colour(style), ass_accent(style), bold, 0, outline,
        shadow, blur, margin_v);
    g_string_append_printf(
        out,
        "Style: Headline,DejaVu Sans,%d,&H00FFFFFF,&H000000FF,&H00000000,"
        "&H80000000,-1,0,2,2,100,100,0,0,1,2,0,8,40,40,%d,1\n",
        (gint) (height * 0.042), (gint) (height * 0.06));
    (void) width;
}

static gchar *build_dialogue_text(const AsScene *scene, AsCaptionStyle style)
{
    if (style == AS_CAPTION_KARAOKE && scene->word_timings->len > 0) {
        GString *out = g_string_new(NULL);
        for (guint i = 0; i < scene->word_timings->len; i++) {
            AsWordTiming *wt = g_ptr_array_index(scene->word_timings, i);
            gint cs = (gint) llround(MAX(0.0, wt->end - wt->start) * 100.0);
            if (cs < 8)
                cs = 8;
            gchar *word = ass_escape(wt->word);
            if (i)
                g_string_append_c(out, ' ');
            g_string_append_printf(out, "{\\k%d}%s", cs, word);
            g_free(word);
        }
        return g_string_free(out, FALSE);
    }

    gchar *text = scene_text(scene);
    if (style == AS_CAPTION_TIKTOK || style == AS_CAPTION_NEON) {
        gchar **words = g_strsplit_set(text, " \t\n\r", -1);
        GString *out = g_string_new(NULL);
        for (guint i = 0; words[i]; i++) {
            if (!*words[i])
                continue;
            gboolean keyword = FALSE;
            for (guint k = 0; k < scene->word_timings->len && !keyword; k++) {
                AsWordTiming *wt = g_ptr_array_index(scene->word_timings, k);
                if (wt->is_keyword &&
                    g_ascii_strcasecmp(wt->word, words[i]) == 0)
                    keyword = TRUE;
            }
            gchar *line = ass_escape(words[i]);
            if (out->len)
                g_string_append_c(out, ' ');
            if (keyword)
                g_string_append_printf(out, "{\\c%s}%s{\\c%s}",
                                       ass_accent(style), line,
                                       ass_colour(style));
            else
                g_string_append(out, line);
            g_free(line);
        }
        g_strfreev(words);
        g_free(text);
        return g_string_free(out, FALSE);
    }
    gchar *line = ass_escape(text);
    g_free(text);
    return line;
}

gboolean as_captions_write_ass(const AsReel *reel, const AsScene *scene,
                               gint width, gint height, const gchar *path,
                               GError **error)
{
    if (!reel || !scene || !path || width <= 0 || height <= 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_INVALID_ARGUMENT,
                    "Invalid ASS caption arguments.");
        return FALSE;
    }
    GString *out = g_string_new(
        "[Script Info]\nScriptType: v4.00+\n");
    g_string_append_printf(out,
                           "PlayResX: %d\nPlayResY: %d\n"
                           "ScaledBorderAndShadow: yes\n"
                           "WrapStyle: 2\n\n[V4+ Styles]\n"
                           "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
                           "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
                           "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
                           "Alignment, MarginL, MarginR, MarginV, Encoding\n",
                           width, height);
    write_style_line(out, reel->caption_style, width, height);
    g_string_append(out, "\n[Events]\n"
                         "Format: Layer, Start, End, Style, Name, MarginL, "
                         "MarginR, MarginV, Effect, Text\n");

    if (scene->on_screen_text && *scene->on_screen_text) {
        gchar *headline = ass_escape(scene->on_screen_text);
        g_string_append(out, "Dialogue: 1,");
        ass_time(out, 0);
        g_string_append_c(out, ',');
        ass_time(out, scene->duration);
        g_string_append_printf(out, ",Headline,,0,0,0,,%s\n", headline);
        g_free(headline);
    }

    gchar *dialogue = build_dialogue_text(scene, reel->caption_style);
    if (dialogue && *dialogue) {
        g_string_append(out, "Dialogue: 0,");
        ass_time(out, 0);
        g_string_append_c(out, ',');
        ass_time(out, scene->duration);
        g_string_append_printf(out, ",Caption,,0,0,0,,%s\n", dialogue);
    }
    g_free(dialogue);

    gboolean ok = g_file_set_contents(path, out->str, (gssize) out->len, error);
    g_string_free(out, TRUE);
    if (!ok && error && *error)
        return FALSE;
    return ok;
}

gdouble as_speech_duration_estimate(const gchar *text)
{
    if (!text || !*text)
        return 0;
    guint words = 0;
    gchar **parts = g_strsplit_set(text, " \t\n\r", -1);
    for (guint i = 0; parts[i]; i++)
        if (*parts[i])
            words++;
    g_strfreev(parts);
    if (!words)
        return 0;
    /* 150 words per minute, plus a short natural tail. */
    return MAX(0.8, words / 2.5 + 0.35);
}
