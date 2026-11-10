/* reel_creator.c — reel generation form, parameters, background pipeline orchestration. */
#define _GNU_SOURCE
#include "as_ui.h"
#include "as_core.h"
#include "as_storage.h"
#include "as_job.h"

typedef struct {
    AsAppContext *ctx;
    AsNavigator *nav;
    GtkWidget *root;

    GtkWidget *prompt_entry;
    GtkWidget *voice_combo;
    GtkWidget *visual_combo;
    GtkWidget *duration_combo;
    GtkWidget *ratio_combo;

    GtkWidget *progress_card;
    GtkWidget *progress_bar;
    GtkWidget *progress_label;
    GtkWidget *generate_btn;
    GtkWidget *cancel_btn;
    GtkWidget *save_draft_btn;

    gchar *active_job_id;
    gchar *active_project_id;
} CreatorState;

static const gchar *VOICE_STYLES[] = {
    "Cinematic", "Deep", "Energetic", "Calm", "Documentary",
    "Storytelling", "Motivational", "News", "Friendly", "Dramatic"
};

static const gchar *VISUAL_STYLES[] = {
    "Cinematic", "Photorealistic", "Anime", "3D Animation", "Documentary",
    "Sci-Fi", "Fantasy", "Dark / Mystery", "Educational",
    "Fast-Paced Social Media", "Minimal", "Custom"
};

static const gint DURATIONS[] = { 15, 30, 45, 60 };
static const gchar *RATIOS[] = { "9:16", "16:9", "1:1" };

static void set_generating_ui(CreatorState *state, gboolean is_generating)
{
    gtk_widget_set_visible(state->progress_card, is_generating);
    gtk_widget_set_visible(state->cancel_btn, is_generating);
    gtk_widget_set_sensitive(state->generate_btn, !is_generating);
    gtk_widget_set_sensitive(state->save_draft_btn, !is_generating);
    gtk_widget_set_sensitive(state->prompt_entry, !is_generating);
    gtk_widget_set_sensitive(state->voice_combo, !is_generating);
    gtk_widget_set_sensitive(state->visual_combo, !is_generating);
    gtk_widget_set_sensitive(state->duration_combo, !is_generating);
    gtk_widget_set_sensitive(state->ratio_combo, !is_generating);
}

static void on_job_progress(AsJobManager *mgr, const gchar *job_id, const gchar *stage,
                            gdouble fraction, gpointer user_data)
{
    (void)mgr;
    CreatorState *state = user_data;
    if (g_strcmp0(state->active_job_id, job_id) != 0)
        return;

    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress_bar), fraction);
    gchar *text = g_strdup_printf("%s (%.0f%%)", stage ? stage : "Processing", fraction * 100.0);
    gtk_label_set_text(GTK_LABEL(state->progress_label), text);
    g_free(text);
}

static void on_job_finished(AsJobManager *mgr, const gchar *job_id, gboolean success,
                            const gchar *error, gpointer user_data)
{
    (void)mgr;
    CreatorState *state = user_data;
    if (g_strcmp0(state->active_job_id, job_id) != 0)
        return;

    set_generating_ui(state, FALSE);
    g_free(state->active_job_id);
    state->active_job_id = NULL;

    if (success) {
        as_ui_toast(state->root, "Reel generation completed successfully!");
        if (state->nav && state->nav->open_project && state->active_project_id) {
            state->nav->open_project(state->nav->user_data, state->active_project_id);
        }
    } else {
        gchar *msg = g_strdup_printf("Generation error: %s", error ? error : "Unknown error");
        as_ui_toast(state->root, msg);
        g_free(msg);
    }
}

static void on_cancel_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    CreatorState *state = user_data;
    if (state->active_job_id) {
        as_job_manager_cancel(state->ctx->jobs, state->active_job_id);
        as_ui_toast(state->root, "Cancelling generation...");
    }
}

static void get_request_from_ui(CreatorState *state, AsReelRequest *req)
{
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->prompt_entry));
    GtkTextIter start, end;
    gtk_text_buffer_get_start_iter(buf, &start);
    gtk_text_buffer_get_end_iter(buf, &end);
    req->prompt = gtk_text_buffer_get_text(buf, &start, &end, FALSE);

    guint voice_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->voice_combo));
    req->voice_style = g_strdup(VOICE_STYLES[MIN(voice_idx, G_N_ELEMENTS(VOICE_STYLES) - 1)]);

    guint visual_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->visual_combo));
    req->visual_style = g_strdup(VISUAL_STYLES[MIN(visual_idx, G_N_ELEMENTS(VISUAL_STYLES) - 1)]);

    guint dur_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->duration_combo));
    req->duration = DURATIONS[MIN(dur_idx, G_N_ELEMENTS(DURATIONS) - 1)];

    guint ratio_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(state->ratio_combo));
    req->aspect_ratio = as_aspect_from_string(RATIOS[MIN(ratio_idx, G_N_ELEMENTS(RATIOS) - 1)]);
}

static void on_generate_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    CreatorState *state = user_data;

    AsReelRequest req;
    memset(&req, 0, sizeof(req));
    get_request_from_ui(state, &req);

    if (!req.prompt || strlen(g_strstrip(req.prompt)) < 3) {
        as_ui_toast(state->root, "Please enter a prompt describing your video reel.");
        as_request_clear(&req);
        return;
    }

    g_free(state->active_project_id);
    state->active_project_id = as_uuid_random();

    GError *error = NULL;
    state->active_job_id = as_job_manager_start_generation(state->ctx, &req,
                                                            state->active_project_id,
                                                            FALSE, &error);
    as_request_clear(&req);

    if (!state->active_job_id) {
        gchar *err_msg = g_strdup_printf("Failed to start generation: %s", error ? error->message : "Check API routing");
        as_ui_toast(state->root, err_msg);
        g_free(err_msg);
        if (error) g_error_free(error);
        return;
    }

    set_generating_ui(state, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(state->progress_bar), 0.05);
    gtk_label_set_text(GTK_LABEL(state->progress_label), "Initializing generation pipeline...");
}

static void on_save_draft_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    CreatorState *state = user_data;
    AsReelRequest req;
    memset(&req, 0, sizeof(req));
    get_request_from_ui(state, &req);

    if (!req.prompt || strlen(g_strstrip(req.prompt)) < 3) {
        as_ui_toast(state->root, "Please enter a prompt to save a draft.");
        as_request_clear(&req);
        return;
    }

    gchar *proj_id = as_uuid_random();
    gchar *title = g_strdup(req.prompt);
    if (strlen(title) > 60) title[60] = '\0';

    GError *error = NULL;
    gboolean ok = as_project_create(state->ctx->db, proj_id, title, &req, &error);
    as_request_clear(&req);
    g_free(title);

    if (ok) {
        as_ui_toast(state->root, "Draft project saved successfully.");
        if (state->nav && state->nav->open_project)
            state->nav->open_project(state->nav->user_data, proj_id);
    } else {
        as_ui_toast(state->root, error ? error->message : "Failed to save draft");
        if (error) g_error_free(error);
    }
    g_free(proj_id);
}

GtkWidget *as_reel_creator_new(AsAppContext *ctx, AsNavigator *nav)
{
    CreatorState *state = g_new0(CreatorState, 1);
    state->ctx = ctx;
    state->nav = nav;

    g_signal_connect(ctx->jobs, "progress", G_CALLBACK(on_job_progress), state);
    g_signal_connect(ctx->jobs, "finished", G_CALLBACK(on_job_finished), state);

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(main_box, 32);
    gtk_widget_set_margin_end(main_box, 32);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 32);

    /* Section Header */
    GtkWidget *title_box = as_ui_section_title("Create Reel", "Generate AI short-form videos with complete audio, storyboards and visuals");
    gtk_box_append(GTK_BOX(main_box), title_box);

    /* Main Form Card */
    GtkWidget *form_card = as_ui_card();

    /* Prompt Input */
    GtkWidget *prompt_lbl = as_ui_label("Prompt & Topic Description", "section-title");
    gtk_box_append(GTK_BOX(form_card), prompt_lbl);

    state->prompt_entry = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(state->prompt_entry), GTK_WRAP_WORD);
    gtk_widget_set_size_request(state->prompt_entry, -1, 120);
    GtkTextBuffer *buf = gtk_text_view_get_buffer(GTK_TEXT_VIEW(state->prompt_entry));
    gtk_text_buffer_set_text(buf, "The incredible story of how Voyager 1 reached interstellar space with 1970s technology", -1);
    gtk_box_append(GTK_BOX(form_card), state->prompt_entry);

    /* Grid of parameters */
    GtkWidget *param_grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(param_grid), 20);
    gtk_grid_set_row_spacing(GTK_GRID(param_grid), 12);

    /* Voice Style */
    GtkWidget *voice_lbl = as_ui_label("Voice Style", "section-subtitle");
    state->voice_combo = gtk_drop_down_new_from_strings(VOICE_STYLES);
    gtk_grid_attach(GTK_GRID(param_grid), voice_lbl, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(param_grid), state->voice_combo, 0, 1, 1, 1);

    /* Visual Style */
    GtkWidget *visual_lbl = as_ui_label("Visual Style", "section-subtitle");
    state->visual_combo = gtk_drop_down_new_from_strings(VISUAL_STYLES);
    gtk_grid_attach(GTK_GRID(param_grid), visual_lbl, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(param_grid), state->visual_combo, 1, 1, 1, 1);

    /* Duration */
    const gchar *duration_strings[] = { "15 Seconds (Quick Hook)", "30 Seconds (Standard)", "45 Seconds (Deep Dive)", "60 Seconds (Full Story)", NULL };
    GtkWidget *dur_lbl = as_ui_label("Target Duration", "section-subtitle");
    state->duration_combo = gtk_drop_down_new_from_strings(duration_strings);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(state->duration_combo), 1); /* default 30s */
    gtk_grid_attach(GTK_GRID(param_grid), dur_lbl, 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(param_grid), state->duration_combo, 2, 1, 1, 1);

    /* Aspect Ratio */
    const gchar *ratio_strings[] = { "9:16 Vertical (Reels / Shorts / TikTok)", "16:9 Horizontal (YouTube)", "1:1 Square (Instagram Post)", NULL };
    GtkWidget *ratio_lbl = as_ui_label("Aspect Ratio", "section-subtitle");
    state->ratio_combo = gtk_drop_down_new_from_strings(ratio_strings);
    gtk_grid_attach(GTK_GRID(param_grid), ratio_lbl, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(param_grid), state->ratio_combo, 3, 1, 1, 1);

    gtk_box_append(GTK_BOX(form_card), param_grid);

    /* Active Route Info Card inside form */
    GtkWidget *route_card = as_ui_provider_route_summary(ctx);
    gtk_box_append(GTK_BOX(form_card), route_card);

    /* Progress Banner (hidden by default) */
    state->progress_card = as_ui_card();
    gtk_widget_set_visible(state->progress_card, FALSE);
    state->progress_label = as_ui_label("Generation in progress...", "section-subtitle");
    state->progress_bar = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(state->progress_bar), TRUE);

    gtk_box_append(GTK_BOX(state->progress_card), state->progress_label);
    gtk_box_append(GTK_BOX(state->progress_card), state->progress_bar);
    gtk_box_append(GTK_BOX(form_card), state->progress_card);

    /* Button Action Row */
    GtkWidget *action_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(action_row, 12);

    state->generate_btn = as_ui_button("⚡ Generate Reel", "btn-primary");
    g_signal_connect(state->generate_btn, "clicked", G_CALLBACK(on_generate_clicked), state);

    state->cancel_btn = as_ui_button("Cancel Generation", "btn-danger");
    gtk_widget_set_visible(state->cancel_btn, FALSE);
    g_signal_connect(state->cancel_btn, "clicked", G_CALLBACK(on_cancel_clicked), state);

    state->save_draft_btn = as_ui_button("Save Draft", "btn-secondary");
    g_signal_connect(state->save_draft_btn, "clicked", G_CALLBACK(on_save_draft_clicked), state);

    gtk_box_append(GTK_BOX(action_row), state->generate_btn);
    gtk_box_append(GTK_BOX(action_row), state->cancel_btn);
    gtk_box_append(GTK_BOX(action_row), state->save_draft_btn);
    gtk_box_append(GTK_BOX(form_card), action_row);

    gtk_box_append(GTK_BOX(main_box), form_card);

    GtkWidget *scrolled = as_ui_scrolled(main_box);
    state->root = scrolled;
    g_object_set_data_full(G_OBJECT(scrolled), "creator-state", state, g_free);

    return scrolled;
}
