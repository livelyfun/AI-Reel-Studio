/* as_media.h — asset retrieval, native audio synthesis, FFmpeg rendering. */
#pragma once

#include <glib.h>
#include <gio/gio.h>
#include "as_types.h"

/* ---- free asset search ---- */
typedef struct {
    gchar *source;          /* "openverse", "wikimedia", "unsplash" */
    gchar *title;
    gchar *image_url;
    gchar *thumbnail_url;
    gchar *source_page;
    gchar *author;
    gchar *license_name;
    gchar *license_url;
    gchar *attribution;
    gint width;
    gint height;
} AsAssetResult;

GPtrArray *as_asset_search(const gchar *query, const gchar *unsplash_key,
                           gint limit, GCancellable *cancellable, GError **error);
gchar *as_asset_build_query(const gchar *visual_prompt, const GPtrArray *keywords,
                            const gchar *topic);
/* Downloads and verifies a JPEG/PNG/WebP file. dest_path extension must match. */
gboolean as_asset_download(const AsAssetResult *result, const gchar *dest_path,
                           GCancellable *cancellable, GError **error);
void as_asset_result_free(AsAssetResult *result);

/* ---- music ---- */
const gchar *as_music_mood_for_styles(const gchar *voice_style,
                                      const gchar *visual_style);
gboolean as_music_synthesize(const gchar *mood, gdouble seconds,
                             const gchar *dest_path, GError **error);

/* ---- process execution ---- */
typedef struct _AsFfmpegRunner AsFfmpegRunner;
typedef void (*AsRenderProgressFunc)(gdouble fraction, const gchar *stage,
                                     gpointer user_data);

AsFfmpegRunner *as_ffmpeg_runner_new(const gchar *ffmpeg_path,
                                     const gchar *ffprobe_path);
void as_ffmpeg_runner_free(AsFfmpegRunner *runner);
gboolean as_ffmpeg_runner_available(AsFfmpegRunner *runner);
const gchar *as_ffmpeg_runner_ffmpeg_path(AsFfmpegRunner *runner);
const gchar *as_ffmpeg_runner_ffprobe_path(AsFfmpegRunner *runner);

gboolean as_ffmpeg_run(AsFfmpegRunner *runner, const gchar *const *argv,
                       gdouble expected_seconds,
                       AsRenderProgressFunc progress, gpointer user_data,
                       GCancellable *cancellable, GError **error);

/* ---- probe/validation ---- */
typedef struct {
    gchar *format_name;
    gchar *video_codec;
    gchar *audio_codec;
    gint width;
    gint height;
    gdouble fps;
    gdouble duration;
    gint64 size;
    gboolean has_video;
    gboolean has_audio;
} AsMediaInfo;

gboolean as_media_probe(AsFfmpegRunner *runner, const gchar *path,
                        AsMediaInfo *info, GError **error);
gdouble as_media_duration(AsFfmpegRunner *runner, const gchar *path,
                          GError **error);
gboolean as_media_validate_export(AsFfmpegRunner *runner, const gchar *path,
                                  AsAspectRatio ratio, gdouble expected_duration,
                                  AsMediaInfo *info, GError **error);
void as_media_info_clear(AsMediaInfo *info);

/* ---- captions ---- */
gchar *as_captions_srt(const AsReel *reel);
gchar *as_captions_vtt(const AsReel *reel);
gchar *as_captions_json(const AsReel *reel);
gboolean as_captions_write_ass(const AsReel *reel, const AsScene *scene,
                               gint width, gint height, const gchar *path,
                               GError **error);
gchar *as_captions_metadata_text(const AsReel *reel);
gdouble as_speech_duration_estimate(const gchar *text);

/* ---- rendering ---- */
typedef struct {
    gint width;
    gint height;
    gint fps;
    gboolean preview;
    gdouble music_gain;
    gdouble narration_gain;
} AsRenderOptions;

typedef struct {
    gchar *output_path;
    gint width;
    gint height;
    gdouble duration;
    gint64 file_size;
    gchar *video_codec;
    gchar *audio_codec;
    gboolean verified;
} AsRenderResult;

gboolean as_video_render(const AsReel *reel, const gchar *project_dir,
                         AsFfmpegRunner *runner, const AsRenderOptions *options,
                         const gchar *output_path, AsRenderProgressFunc progress,
                         gpointer user_data, GCancellable *cancellable,
                         AsRenderResult *result, GError **error);
void as_render_result_clear(AsRenderResult *result);

/* Helpers used by the UI and tests. */
void as_render_options_for_ratio(AsAspectRatio ratio, gboolean preview,
                                 AsRenderOptions *options);
gchar *as_media_thumbnail_from_image(const gchar *image_path,
                                     const gchar *dest_path,
                                     AsFfmpegRunner *runner, GError **error);
