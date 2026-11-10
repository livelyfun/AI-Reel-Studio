/* test_generation.c — verification of planner JSON decoding, storyboard validation and pipeline. */
#define _GNU_SOURCE
#include <glib.h>
#include <glib/gstdio.h>
#include "as_types.h"
#include "as_core.h"
#include "as_storage.h"
#include "storage_internal.h"
#include "reel_json.h"

static const gchar *SAMPLE_REEL_JSON =
    "{\n"
    "  \"schema_version\": 1,\n"
    "  \"id\": \"reel-test-001\",\n"
    "  \"created_at\": \"2026-11-10T00:00:00Z\",\n"
    "  \"request\": {\n"
    "    \"prompt\": \"Deep Sea Exploration\",\n"
    "    \"voice_style\": \"Cinematic\",\n"
    "    \"visual_style\": \"Documentary\",\n"
    "    \"duration\": 30,\n"
    "    \"aspect_ratio\": \"9:16\"\n"
    "  },\n"
    "  \"plan\": {\n"
    "    \"topic\": \"Deep Sea Bioluminescence\",\n"
    "    \"purpose\": \"Educational Entertainment\",\n"
    "    \"target_audience\": \"Science enthusiasts\",\n"
    "    \"tone\": \"Mysterious and fascinating\",\n"
    "    \"story_structure\": \"Hook, exploration, reveal, takeaway\",\n"
    "    \"hook\": \"Miles beneath the surface, life creates its own light.\",\n"
    "    \"ending\": \"The deep ocean remains Earth's greatest unexplored frontier.\",\n"
    "    \"call_to_action\": \"Follow for more incredible ocean science!\",\n"
    "    \"pacing\": \"Dynamic\",\n"
    "    \"scene_count\": 2,\n"
    "    \"estimated_total_duration\": 10.0,\n"
    "    \"visual_bible\": {\n"
    "      \"environment\": \"Midnight zone of the ocean\",\n"
    "      \"locations\": \"Mariana Trench abyssal plain\",\n"
    "      \"objects\": \"Glowing jellyfish, anglerfish lantern\",\n"
    "      \"visual_style\": \"Cinematic documentary\",\n"
    "      \"lighting\": \"Dark with glowing cyan and violet neon bioluminescence\",\n"
    "      \"camera_style\": \"Slow floating underwater macro\",\n"
    "      \"overall_atmosphere\": \"Alien and serene\",\n"
    "      \"color_palette\": [\"#050814\", \"#00f0ff\", \"#7928ca\"]\n"
    "    }\n"
    "  },\n"
    "  \"metadata\": {\n"
    "    \"reel_name\": \"Bioluminescence_Deep_Sea\",\n"
    "    \"youtube_title\": \"The Secret Glowing World 10,000 Feet Below | Deep Ocean\",\n"
    "    \"youtube_description\": \"Discover how deep-sea creatures illuminate the darkest depths of our oceans with living light.\",\n"
    "    \"hook_analysis\": \"Strong visual contrast hook\",\n"
    "    \"audience_fit\": \"High engagement for science feeds\",\n"
    "    \"tags\": [\"ocean\", \"deepsea\", \"marinebiology\", \"science\"],\n"
    "    \"hashtags\": [\"deepsea\", \"oceanlife\", \"shorts\", \"reels\"]\n"
    "  },\n"
    "  \"scenes\": [\n"
    "    {\n"
    "      \"scene_id\": 1,\n"
    "      \"duration\": 5.0,\n"
    "      \"narration\": \"Miles beneath the ocean surface, sunlight never penetrates.\",\n"
    "      \"visual_description\": \"A pitch black abyss suddenly glows with pulsing blue tendrils.\",\n"
    "      \"visual_prompt\": \"deep sea bioluminescent jellyfish glowing in pitch black water, 8k cinematic macro\",\n"
    "      \"camera\": \"Slow macro pan\",\n"
    "      \"lighting\": \"Deep cyan bioluminescent glow\",\n"
    "      \"transition\": \"crossfade\",\n"
    "      \"on_screen_text\": \"EARTH'S ALIEN WORLD\",\n"
    "      \"keywords\": [\"deep sea\", \"bioluminescence\", \"ocean\"]\n"
    "    },\n"
    "    {\n"
    "      \"scene_id\": 2,\n"
    "      \"duration\": 5.0,\n"
    "      \"narration\": \"Yet in this eternal darkness, 90 percent of animals create their own light.\",\n"
    "      \"visual_description\": \"An anglerfish moves slowly, its bioluminescent lure flickering.\",\n"
    "      \"visual_prompt\": \"anglerfish in deep ocean glowing lure, national geographic style photorealistic\",\n"
    "      \"camera\": \"Slow tracking shot\",\n"
    "      \"lighting\": \"Faint glowing lure highlighting predatory teeth\",\n"
    "      \"transition\": \"fade-black\",\n"
    "      \"on_screen_text\": \"90% CREATE LIGHT\",\n"
    "      \"keywords\": [\"anglerfish\", \"darkness\", \"predator\"]\n"
    "    }\n"
    "  ],\n"
    "  \"total_duration\": 10.0,\n"
    "  \"music_mood\": \"mysterious\",\n"
    "  \"caption_style\": \"bottom-banner\"\n"
    "}";

static void test_json_codec_and_validation(void)
{
    GError *error = NULL;
    AsReel *reel = as_reel_from_json(SAMPLE_REEL_JSON, &error);
    g_assert_no_error(error);
    g_assert_nonnull(reel);

    /* Verify Content Plan fields */
    g_assert_cmpstr(reel->plan.topic, ==, "Deep Sea Bioluminescence");
    g_assert_cmpstr(reel->plan.tone, ==, "Mysterious and fascinating");
    g_assert_cmpstr(reel->plan.hook, ==, "Miles beneath the surface, life creates its own light.");
    g_assert_cmpint(reel->plan.scene_count, ==, 2);

    /* Verify Visual Bible */
    g_assert_cmpstr(reel->plan.visual_bible.environment, ==, "Midnight zone of the ocean");
    g_assert_cmpstr(reel->plan.visual_bible.lighting, ==, "Dark with glowing cyan and violet neon bioluminescence");
    g_assert_cmpuint(reel->plan.visual_bible.color_palette->len, ==, 3);

    /* Verify Scenes */
    g_assert_cmpuint(reel->scenes->len, ==, 2);
    AsScene *sc1 = g_ptr_array_index(reel->scenes, 0);
    g_assert_cmpint(sc1->scene_id, ==, 1);
    g_assert_cmpfloat(sc1->duration, ==, 5.0);
    g_assert_cmpstr(sc1->transition, ==, "crossfade");
    g_assert_cmpstr(sc1->on_screen_text, ==, "EARTH'S ALIEN WORLD");

    AsScene *sc2 = g_ptr_array_index(reel->scenes, 1);
    g_assert_cmpint(sc2->scene_id, ==, 2);
    g_assert_cmpstr(sc2->transition, ==, "fade-black");

    /* Verify Metadata */
    g_assert_cmpstr(reel->metadata.youtube_title, ==, "The Secret Glowing World 10,000 Feet Below | Deep Ocean");
    g_assert_cmpuint(reel->metadata.tags->len, ==, 4);
    g_assert_cmpuint(reel->metadata.hashtags->len, ==, 4);

    /* Verify Validation */
    gboolean valid = as_reel_validate(reel, &error);
    g_assert_no_error(error);
    g_assert_true(valid);

    /* Verify serialization back to JSON */
    gchar *encoded_json = as_reel_to_json(reel);
    g_assert_nonnull(encoded_json);
    g_assert_true(strstr(encoded_json, "Deep Sea Bioluminescence") != NULL);
    g_assert_true(strstr(encoded_json, "EARTH'S ALIEN WORLD") != NULL);

    /* Verify round-trip parsing */
    AsReel *roundtrip = as_reel_from_json(encoded_json, &error);
    g_assert_no_error(error);
    g_assert_nonnull(roundtrip);
    g_assert_cmpstr(roundtrip->plan.topic, ==, reel->plan.topic);
    g_assert_cmpuint(roundtrip->scenes->len, ==, 2);

    as_reel_free(roundtrip);
    g_free(encoded_json);
    as_reel_free(reel);
}

static void test_project_store_with_reel(void)
{
    gchar *tmp_db = g_build_filename(g_get_tmp_dir(), "test_proj_reel.db", NULL);
    g_unlink(tmp_db);

    GError *error = NULL;
    AsDatabase *db = as_database_open(tmp_db, &error);
    g_assert_no_error(error);

    AsReelRequest req;
    as_request_init(&req);
    req.prompt = g_strdup("Deep Sea Exploration");
    req.voice_style = g_strdup("Cinematic");
    req.visual_style = g_strdup("Documentary");
    req.duration = 30;
    req.aspect_ratio = AS_ASPECT_9_16;

    gboolean ok = as_project_create(db, "proj-bio-1", "Bioluminescence Reel", &req, &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    AsReel *reel = as_reel_from_json(SAMPLE_REEL_JSON, &error);
    g_assert_no_error(error);

    /* Save reel document into project */
    ok = as_project_save_reel(db, "proj-bio-1", reel, "ready", &error);
    g_assert_no_error(error);
    g_assert_true(ok);

    /* Reload project with reel */
    AsProject *loaded = as_project_load(db, "proj-bio-1", TRUE, &error);
    g_assert_no_error(error);
    g_assert_nonnull(loaded);
    g_assert_nonnull(loaded->reel);
    g_assert_cmpstr(loaded->reel->plan.topic, ==, "Deep Sea Bioluminescence");
    g_assert_cmpuint(loaded->reel->scenes->len, ==, 2);

    as_project_free(loaded);
    as_reel_free(reel);
    as_request_clear(&req);
    as_database_close(db);
    g_unlink(tmp_db);
    g_free(tmp_db);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/generation/json_codec_and_validation", test_json_codec_and_validation);
    g_test_add_func("/generation/project_store_with_reel", test_project_store_with_reel);
    return g_test_run();
}
