/* workspace.c — storyboard editor, video preview, captions, metadata and export manager. */
#define _GNU_SOURCE
#include "as_ui.h"
#include "as_core.h"
#include "as_storage.h"
#include "as_job.h"
#include "as_media.h"
#include "as_generation.h"

#include <glib/gstdio.h>

typedef struct {
    AsAppContext *ctx;
    AsNavigator *nav;
    gchar *project_id;
    AsProject *project;

    GtkWidget *root;
    GtkWidget *title_lbl;
    GtkWidget *scenes_box;
    GtkWidget *meta_box;

    /* Video player */
    GtkWidget *video_widget;
    GtkMediaStream *media_stream;

    /* Export widgets */
    GtkWidget *export_card;
    GtkWidget *export_progress_bar;
    GtkWidget *export_status_lbl;
    GtkWidget *render_btn;
    GtkWidget *validation_card;
    GtkWidget *validation_lbl;

    gchar *active_export_job_id;
} WorkspaceState;

static void refresh_workspace(WorkspaceState *state);

static void on_back_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    WorkspaceState *state = user_data;
    if (state->nav && state->nav->back_to_projects)
        state->nav->back_to_projects(state->nav->user_data);
}

/* Copy button callbacks */
static void on_copy_clicked(GtkButton *btn, gpointer user_data)
{
    const gchar *text = user_data;
    as_ui_copy_text(GTK_WIDGET(btn), text);
    as_ui_toast(GTK_WIDGET(btn), "Copied to clipboard!");
}

/* Export Subtitles */
static void on_export_subtitles_clicked(GtkButton *btn, gpointer user_data)
{
    WorkspaceState *state = user_data;
    if (!state->project || !state->project->reel) return;

    gchar *proj_dir = as_app_context_project_dir(state->ctx, state->project_id);
    gchar *srt_path = g_build_filename(proj_dir, "subtitles.srt", NULL);
    gchar *vtt_path = g_build_filename(proj_dir, "subtitles.vtt", NULL);

    gchar *srt = as_captions_srt(state->project->reel);
    gchar *vtt = as_captions_vtt(state->project->reel);

    g_file_set_contents(srt_path, srt ? srt : "", -1, NULL);
    g_file_set_contents(vtt_path, vtt ? vtt : "", -1, NULL);

    g_free(srt);
    g_free(vtt);

    gchar *msg = g_strdup_printf("Subtitles exported to %s", srt_path);
    as_ui_toast(GTK_WIDGET(btn), msg);
    g_free(msg);

    g_free(proj_dir);
    g_free(srt_path);
    g_free(vtt_path);
}

/* Export Metadata text package */
static void on_export_metadata_clicked(GtkButton *btn, gpointer user_data)
{
    WorkspaceState *state = user_data;
    if (!state->project || !state->project->reel) return;

    AsReel *reel = state->project->reel;
    GString *out = g_string_new(NULL);
    g_string_append_printf(out, "=====================================================\n");
    g_string_append_printf(out, "AI REEL STUDIO — YOUTUBE & SOCIAL METADATA\n");
    g_string_append_printf(out, "=====================================================\n");
    g_string_append_printf(out, "Title: %s\n", reel->metadata.youtube_title ? reel->metadata.youtube_title : "");
    g_string_append_printf(out, "Duration: %.0fs\n\n", state->project->total_duration);
    g_string_append_printf(out, "-----------------------------------------------------\n");
    g_string_append_printf(out, "DESCRIPTION\n");
    g_string_append_printf(out, "-----------------------------------------------------\n");
    g_string_append_printf(out, "%s\n\n", reel->metadata.youtube_description ? reel->metadata.youtube_description : "");
    g_string_append_printf(out, "-----------------------------------------------------\n");
    g_string_append_printf(out, "TAGS\n");
    g_string_append_printf(out, "-----------------------------------------------------\n");
    for (guint i = 0; i < reel->metadata.tags->len; i++) {
        g_string_append_printf(out, "%s%s", (gchar *)g_ptr_array_index(reel->metadata.tags, i),
                               i + 1 < reel->metadata.tags->len ? ", " : "\n\n");
    }
    g_string_append_printf(out, "-----------------------------------------------------\n");
    g_string_append_printf(out, "VIRAL HASHTAGS\n");
    g_string_append_printf(out, "-----------------------------------------------------\n");
    for (guint i = 0; i < reel->metadata.hashtags->len; i++) {
        g_string_append_printf(out, "#%s ", (gchar *)g_ptr_array_index(reel->metadata.hashtags, i));
    }
    g_string_append(out, "\n");

    gchar *proj_dir = as_app_context_project_dir(state->ctx, state->project_id);
    gchar *meta_file = g_build_filename(proj_dir, "metadata.txt", NULL);
    g_file_set_contents(meta_file, out->str, -1, NULL);

    gchar *msg = g_strdup_printf("Metadata saved to %s", meta_file);
    as_ui_toast(GTK_WIDGET(btn), msg);
    g_free(msg);

    g_string_free(out, TRUE);
    g_free(proj_dir);
    g_free(meta_file);
}

/* Video Render Callbacks */
static void on_render_job_progress(AsJobManager *mgr, const gchar *job_id, const gchar *stage,
                                   gdouble fraction, gpointer user_data)
{
    (void)mgr;
    WorkspaceState *state = user_data;
    if (g_strcmp0(state->active_export_job_id, job_id) != 0) return;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->export_progress_bar), fraction);
    gchar *txt = g_strdup_printf("%s (%.0f%%)", stage ? stage : "Rendering video", fraction * 100.0);
    gtk_label_set_text(GTK_LABEL(state->export_status_lbl), txt);
    g_free(txt);
}

static void on_render_job_finished(AsJobManager *mgr, const gchar *job_id, gboolean success,
                                   const gchar *error, gpointer user_data)
{
    (void)mgr;
    WorkspaceState *state = user_data;
    if (g_strcmp0(state->active_export_job_id, job_id) != 0) return;

    g_free(state->active_export_job_id);
    state->active_export_job_id = NULL;

    gtk_widget_set_sensitive(state->render_btn, TRUE);

    if (success) {
        as_ui_toast(state->root, "Video rendered and validated successfully!");
        refresh_workspace(state);
    } else {
        gchar *msg = g_strdup_printf("Render failed: %s", error ? error : "Unknown FFmpeg error");
        as_ui_toast(state->root, msg);
        g_free(msg);
    }
}

static void on_render_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    WorkspaceState *state = user_data;
    if (!state->project || !state->project->reel) return;

    gchar *proj_dir = as_app_context_project_dir(state->ctx, state->project_id);
    gchar *out_path = g_build_filename(proj_dir, "export.mp4", NULL);

    gtk_widget_set_visible(state->export_card, TRUE);
    gtk_widget_set_sensitive(state->render_btn, FALSE);

    GError *error = NULL;
    state->active_export_job_id = as_job_manager_start_export(state->ctx, state->project_id,
                                                              out_path, FALSE, &error);
    if (!state->active_export_job_id) {
        as_ui_toast(state->root, error ? error->message : "Failed to start render");
        gtk_widget_set_sensitive(state->render_btn, TRUE);
        if (error) g_error_free(error);
    }

    g_free(proj_dir);
    g_free(out_path);
}

/* Scene regeneration workers */
typedef struct {
    WorkspaceState *state;
    gint scene_id;
} SceneActionData;

static gpointer regen_image_task(gpointer user_data, GCancellable *cancellable, GError **error)
{
    SceneActionData *sad = user_data;
    WorkspaceState *state = sad->state;

    AsScene *target_scene = NULL;
    for (guint i = 0; i < state->project->reel->scenes->len; i++) {
        AsScene *sc = g_ptr_array_index(state->project->reel->scenes, i);
        if (sc->scene_id == sad->scene_id) {
            target_scene = sc;
            break;
        }
    }
    if (!target_scene) return NULL;

    /* Build search query */
    gchar *q = as_asset_build_query(target_scene->visual_prompt, target_scene->keywords,
                                    state->project->reel->plan.topic);
    GPtrArray *results = as_asset_search(q, state->ctx->config->unsplash_access_key, 5, cancellable, error);
    g_free(q);

    if (results && results->len > 0) {
        AsAssetResult *res = g_ptr_array_index(results, 0);
        gchar *proj_dir = as_app_context_project_dir(state->ctx, state->project_id);
        gchar *fname = g_strdup_printf("scene_%d.jpg", sad->scene_id);
        gchar *dest = g_build_filename(proj_dir, fname, NULL);
        g_free(fname);
        g_free(proj_dir);

        if (as_asset_download(res, dest, cancellable, error)) {
            g_free(target_scene->image_path);
            target_scene->image_path = dest;
            /* Save updated reel */
            as_project_save_reel(state->ctx->db, state->project_id, state->project->reel, "draft", NULL);
        } else {
            g_free(dest);
        }
        g_ptr_array_unref(results);
    }
    return NULL;
}

static void regen_image_done(gpointer result, GError *error, gpointer user_data)
{
    (void)result;
    SceneActionData *sad = user_data;
    if (!error) {
        as_ui_toast(sad->state->root, "Scene image regenerated.");
        refresh_workspace(sad->state);
    } else {
        as_ui_toast(sad->state->root, error->message);
    }
    g_free(sad);
}

static void on_regen_scene_image(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    SceneActionData *sad = user_data;
    as_ui_toast(sad->state->root, "Searching new visual asset...");
    SceneActionData *task_data = g_new0(SceneActionData, 1);
    *task_data = *sad;
    as_ui_run_async(regen_image_task, task_data, NULL, regen_image_done, task_data, NULL, NULL);
}

static void refresh_workspace(WorkspaceState *state)
{
    if (state->project) {
        as_project_free(state->project);
        state->project = NULL;
    }

    state->project = as_project_load(state->ctx->db, state->project_id, TRUE, NULL);
    if (!state->project || !state->project->reel) {
        gtk_label_set_text(GTK_LABEL(state->title_lbl), "Project not found");
        return;
    }

    /* Update Title & Header */
    gchar *hdr_text = g_strdup_printf("%s  •  %.0fs  •  %s",
                                      state->project->name,
                                      state->project->total_duration,
                                      as_aspect_to_string(state->project->aspect_ratio));
    gtk_label_set_text(GTK_LABEL(state->title_lbl), hdr_text);
    g_free(hdr_text);

    /* Update Video Preview */
    gchar *proj_dir = as_app_context_project_dir(state->ctx, state->project_id);
    gchar *mp4_path = g_build_filename(proj_dir, "export.mp4", NULL);
    if (g_file_test(mp4_path, G_FILE_TEST_EXISTS)) {
        GFile *vf = g_file_new_for_path(mp4_path);
        gtk_video_set_file(GTK_VIDEO(state->video_widget), vf);
        g_object_unref(vf);
        gtk_widget_set_visible(state->video_widget, TRUE);

        /* Inspect file with ffprobe for quality report */
        AsMediaInfo val;
        memset(&val, 0, sizeof(val));
        if (as_media_probe(state->ctx->ffmpeg, mp4_path, &val, NULL)) {
            gchar *val_txt = g_strdup_printf("Validated Render: %s (%dx%d) • Video: %s • Audio: %s • Duration: %.1fs • Size: %.2f MB",
                                             val.format_name ? val.format_name : "mp4", val.width, val.height,
                                             val.video_codec ? val.video_codec : "h264",
                                             val.audio_codec ? val.audio_codec : "aac",
                                             val.duration,
                                             (gdouble)val.size / (1024.0 * 1024.0));
            gtk_label_set_text(GTK_LABEL(state->validation_lbl), val_txt);
            gtk_widget_set_visible(state->validation_card, TRUE);
            g_free(val_txt);
            as_media_info_clear(&val);
        }
    } else {
        gtk_widget_set_visible(state->video_widget, FALSE);
        gtk_widget_set_visible(state->validation_card, FALSE);
    }
    g_free(proj_dir);
    g_free(mp4_path);

    /* 2. Rebuild Storyboard Cards */
    GtkWidget *child = NULL;
    while ((child = gtk_widget_get_first_child(state->scenes_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->scenes_box), child);

    AsReel *reel = state->project->reel;
    for (guint i = 0; i < reel->scenes->len; i++) {
        AsScene *sc = g_ptr_array_index(reel->scenes, i);
        GtkWidget *card = as_ui_card();
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

        /* Image preview */
        GtkWidget *thumb = as_ui_picture_for_file(sc->image_path, 120, 120);
        gtk_box_append(GTK_BOX(row), thumb);

        /* Scene Details */
        GtkWidget *details = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_hexpand(details, TRUE);

        gchar *sc_title = g_strdup_printf("Scene %d (%.1fs)  •  Transition: %s  •  Camera: %s",
                                          sc->scene_id, sc->duration,
                                          sc->transition ? sc->transition : "crossfade",
                                          sc->camera ? sc->camera : "Cinematic");
        GtkWidget *sc_title_lbl = as_ui_label(sc_title, "section-title");
        g_free(sc_title);

        GtkWidget *narration_lbl = as_ui_label(sc->narration, "section-subtitle");
        gtk_box_append(GTK_BOX(details), sc_title_lbl);
        gtk_box_append(GTK_BOX(details), narration_lbl);

        if (sc->on_screen_text && *sc->on_screen_text) {
            gchar *ost = g_strdup_printf("On-Screen Headline: %s", sc->on_screen_text);
            GtkWidget *ost_lbl = as_ui_label(ost, "badge-primary");
            gtk_box_append(GTK_BOX(details), ost_lbl);
            g_free(ost);
        }

        gtk_box_append(GTK_BOX(row), details);

        /* Action buttons for scene */
        GtkWidget *act_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        GtkWidget *regen_img_btn = as_ui_button("Change Visual", "btn-secondary");

        SceneActionData *sad = g_new0(SceneActionData, 1);
        sad->state = state;
        sad->scene_id = sc->scene_id;
        g_signal_connect(regen_img_btn, "clicked", G_CALLBACK(on_regen_scene_image), sad);

        gtk_box_append(GTK_BOX(act_box), regen_img_btn);
        gtk_box_append(GTK_BOX(row), act_box);

        gtk_box_append(GTK_BOX(card), row);
        gtk_box_append(GTK_BOX(state->scenes_box), card);
    }

    /* 3. Rebuild Social Metadata View */
    while ((child = gtk_widget_get_first_child(state->meta_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->meta_box), child);

    GtkWidget *meta_card = as_ui_card();

    /* Title */
    GtkWidget *yt_title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *yt_title_lbl = as_ui_label(reel->metadata.youtube_title ? reel->metadata.youtube_title : "Untitled Reel", "section-title");
    gtk_widget_set_hexpand(yt_title_lbl, TRUE);
    GtkWidget *cp_title_btn = as_ui_button("Copy Title", "btn-secondary");
    g_signal_connect_data(cp_title_btn, "clicked", G_CALLBACK(on_copy_clicked),
                          g_strdup(reel->metadata.youtube_title ? reel->metadata.youtube_title : ""),
                          (GClosureNotify)g_free, 0);
    gtk_box_append(GTK_BOX(yt_title_row), yt_title_lbl);
    gtk_box_append(GTK_BOX(yt_title_row), cp_title_btn);
    gtk_box_append(GTK_BOX(meta_card), yt_title_row);

    /* Description */
    GtkWidget *desc_lbl = as_ui_label(reel->metadata.youtube_description, "section-subtitle");
    gtk_box_append(GTK_BOX(meta_card), desc_lbl);

    /* Hashtags */
    GString *ht_str = g_string_new(NULL);
    for (guint i = 0; i < reel->metadata.hashtags->len; i++) {
        g_string_append_printf(ht_str, "#%s ", (gchar *)g_ptr_array_index(reel->metadata.hashtags, i));
    }
    GtkWidget *ht_lbl = as_ui_label(ht_str->str, "badge-primary");
    gtk_box_append(GTK_BOX(meta_card), ht_lbl);
    g_string_free(ht_str, TRUE);

    gtk_box_append(GTK_BOX(state->meta_box), meta_card);
}

GtkWidget *as_workspace_new(AsAppContext *ctx, AsNavigator *nav, const gchar *project_id)
{
    WorkspaceState *state = g_new0(WorkspaceState, 1);
    state->ctx = ctx;
    state->nav = nav;
    state->project_id = g_strdup(project_id);

    g_signal_connect(ctx->jobs, "progress", G_CALLBACK(on_render_job_progress), state);
    g_signal_connect(ctx->jobs, "finished", G_CALLBACK(on_render_job_finished), state);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(main_box, 32);
    gtk_widget_set_margin_end(main_box, 32);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 32);

    /* Navigation Back & Header */
    GtkWidget *top_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *back_btn = as_ui_button("← Back to Projects", "btn-secondary");
    g_signal_connect(back_btn, "clicked", G_CALLBACK(on_back_clicked), state);
    state->title_lbl = as_ui_label("Loading Reel...", "section-title");
    gtk_widget_set_hexpand(state->title_lbl, TRUE);

    state->render_btn = as_ui_button("🎬 Render Full Video", "btn-primary");
    g_signal_connect(state->render_btn, "clicked", G_CALLBACK(on_render_clicked), state);

    gtk_box_append(GTK_BOX(top_bar), back_btn);
    gtk_box_append(GTK_BOX(top_bar), state->title_lbl);
    gtk_box_append(GTK_BOX(top_bar), state->render_btn);
    gtk_box_append(GTK_BOX(main_box), top_bar);

    /* Render Progress Banner (hidden by default) */
    state->export_card = as_ui_card();
    gtk_widget_set_visible(state->export_card, FALSE);
    state->export_status_lbl = as_ui_label("Rendering video...", "section-subtitle");
    state->export_progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->export_progress_bar), TRUE);
    gtk_box_append(GTK_BOX(state->export_card), state->export_status_lbl);
    gtk_box_append(GTK_BOX(state->export_card), state->export_progress_bar);
    gtk_box_append(GTK_BOX(main_box), state->export_card);

    /* Video Player & Validation Preview */
    state->video_widget = gtk_video_new();
    gtk_widget_set_size_request(state->video_widget, 320, 480);
    gtk_box_append(GTK_BOX(main_box), state->video_widget);

    state->validation_card = as_ui_card();
    state->validation_lbl = as_ui_label("", "badge-success");
    gtk_box_append(GTK_BOX(state->validation_card), state->validation_lbl);
    gtk_box_append(GTK_BOX(main_box), state->validation_card);

    /* Export Actions Bar */
    GtkWidget *exp_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    GtkWidget *exp_sub_btn = as_ui_button("Export Subtitles (SRT/VTT)", "btn-secondary");
    g_signal_connect(exp_sub_btn, "clicked", G_CALLBACK(on_export_subtitles_clicked), state);
    GtkWidget *exp_meta_btn = as_ui_button("Export Social Metadata (.txt)", "btn-secondary");
    g_signal_connect(exp_meta_btn, "clicked", G_CALLBACK(on_export_metadata_clicked), state);
    gtk_box_append(GTK_BOX(exp_bar), exp_sub_btn);
    gtk_box_append(GTK_BOX(exp_bar), exp_meta_btn);
    gtk_box_append(GTK_BOX(main_box), exp_bar);

    /* Storyboard Section */
    GtkWidget *sb_hdr = as_ui_label("Storyboard & Visual Scenes", "section-title");
    gtk_box_append(GTK_BOX(main_box), sb_hdr);
    state->scenes_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(main_box), state->scenes_box);

    /* Social Metadata Section */
    GtkWidget *meta_hdr = as_ui_label("YouTube & Social Media Metadata", "section-title");
    gtk_box_append(GTK_BOX(main_box), meta_hdr);
    state->meta_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(main_box), state->meta_box);

    GtkWidget *scrolled = as_ui_scrolled(main_box);
    state->root = scrolled;

    refresh_workspace(state);
    return scrolled;
}
