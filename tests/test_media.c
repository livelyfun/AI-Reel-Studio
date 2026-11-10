/* test_media.c — tests for audio synthesis and subtitle generation. */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include "as_media.h"
#include "as_types.h"

static void test_music_synthesis(void)
{
    gchar *wav_path = g_build_filename(g_get_tmp_dir(), "test_music.wav", NULL);
    g_unlink(wav_path);

    GError *error = NULL;
    gboolean ok = as_music_synthesize("Cinematic", 2.0, wav_path, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    g_assert_true(g_file_test(wav_path, G_FILE_TEST_EXISTS));
    GStatBuf sb;
    g_assert_cmpint(g_stat(wav_path, &sb), ==, 0);
    g_assert_cmpint(sb.st_size, >, 1000); /* 2 seconds of 44.1kHz 16-bit WAV is > 100KB */

    g_unlink(wav_path);
    g_free(wav_path);
}

static void test_subtitles_formatting(void)
{
    AsReel *reel = as_reel_new();
    reel->id = g_strdup("test-reel");
    reel->plan.topic = g_strdup("Space exploration");

    AsScene *sc1 = as_scene_new();
    sc1->scene_id = 1;
    sc1->duration = 4.5;
    sc1->narration = g_strdup("Looking up at the night sky, we wonder.");
    g_ptr_array_add(reel->scenes, sc1);

    AsScene *sc2 = as_scene_new();
    sc2->scene_id = 2;
    sc2->duration = 5.0;
    sc2->narration = g_strdup("Voyager traveled farther than any machine before.");
    g_ptr_array_add(reel->scenes, sc2);

    gchar *srt_content = as_captions_srt(reel);
    g_assert_nonnull(srt_content);
    g_assert_true(strstr(srt_content, "Looking up at the night sky") != NULL);
    g_assert_true(strstr(srt_content, "00:00:00,000 --> 00:00:04,500") != NULL);
    g_free(srt_content);

    gchar *vtt_content = as_captions_vtt(reel);
    g_assert_nonnull(vtt_content);
    g_assert_true(strstr(vtt_content, "WEBVTT") != NULL);
    g_free(vtt_content);

    as_reel_free(reel);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/media/music_synthesis", test_music_synthesis);
    g_test_add_func("/media/subtitles_formatting", test_subtitles_formatting);
    return g_test_run();
}
