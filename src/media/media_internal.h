/* media_internal.h — helpers shared by the FFmpeg-backed media modules. */
#pragma once

#include "as_media.h"

/* Preferred H.264 encoder for this build (libx264 when present, otherwise
 * libopenh264 as shipped by Fedora's ffmpeg-free). */
const gchar *as_ffmpeg_video_encoder(AsFfmpegRunner *runner);
const gchar *as_ffmpeg_audio_encoder(AsFfmpegRunner *runner);

/* Appends encoder-appropriate quality flags to argv. */
void as_ffmpeg_add_video_quality(AsFfmpegRunner *runner, GPtrArray *args,
                                 gint crf, const gchar *preset);
