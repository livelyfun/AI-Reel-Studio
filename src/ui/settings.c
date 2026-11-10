/* settings.c — application preferences, default styles, paths and cache management. */
#define _GNU_SOURCE
#include "as_ui.h"
#include "as_core.h"
#include "as_storage.h"

#include <glib/gstdio.h>

typedef struct {
    AsAppContext *ctx;
    AsNavigator *nav;
    GtkWidget *root;

    GtkWidget *export_dir_entry;
    GtkWidget *ratio_combo;
    GtkWidget *duration_combo;
    GtkWidget *ffmpeg_path_lbl;
    GtkWidget *ffprobe_path_lbl;
    GtkWidget *cache_size_lbl;
} SettingsState;

static void refresh_settings(SettingsState *state)
{
    if (state->ctx->config && state->ctx->config->export_dir) {
        gtk_editable_set_text(GTK_EDITABLE(state->export_dir_entry), state->ctx->config->export_dir);
    }

    /* FFmpeg status */
    gboolean ff_ok = as_ffmpeg_runner_available(state->ctx->ffmpeg);
    const gchar *ff_path = as_ffmpeg_runner_ffmpeg_path(state->ctx->ffmpeg);
    gchar *ff_text = g_strdup_printf("FFmpeg: %s (%s)", ff_path ? ff_path : "Not found", ff_ok ? "Available" : "Missing");
    gtk_label_set_text(GTK_LABEL(state->ffmpeg_path_lbl), ff_text);
    g_free(ff_text);

    const gchar *fp_path = as_ffmpeg_runner_ffprobe_path(state->ctx->ffmpeg);
    gchar *fp_text = g_strdup_printf("ffprobe: %s (%s)", fp_path ? fp_path : "Not found", ff_ok ? "Available" : "Missing");
    gtk_label_set_text(GTK_LABEL(state->ffprobe_path_lbl), fp_text);
    g_free(fp_text);

    /* Cache size */
    gsize cache_bytes = 0;
    if (state->ctx->paths && state->ctx->paths->cache_dir)
        cache_bytes = as_dir_size(state->ctx->paths->cache_dir);
    gchar *cache_text = g_strdup_printf("Local Cache Usage: %.1f MB", (gdouble)cache_bytes / (1024.0 * 1024.0));
    gtk_label_set_text(GTK_LABEL(state->cache_size_lbl), cache_text);
    g_free(cache_text);
}

static void on_save_settings_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    SettingsState *state = user_data;
    const gchar *exp = gtk_editable_get_text(GTK_EDITABLE(state->export_dir_entry));

    g_free(state->ctx->config->export_dir);
    state->ctx->config->export_dir = g_strdup(exp);

    GError *error = NULL;
    if (as_config_save(state->ctx->config, state->ctx->paths->settings_path, &error)) {
        as_ui_toast(state->root, "Settings saved.");
    } else {
        as_ui_toast(state->root, error ? error->message : "Failed to save settings");
        if (error) g_error_free(error);
    }
}

static void on_clear_cache_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    SettingsState *state = user_data;
    if (state->ctx->paths && state->ctx->paths->cache_dir) {
        GDir *dir = g_dir_open(state->ctx->paths->cache_dir, 0, NULL);
        if (dir) {
            const gchar *name;
            while ((name = g_dir_read_name(dir)) != NULL) {
                gchar *child = g_build_filename(state->ctx->paths->cache_dir, name, NULL);
                g_unlink(child);
                g_free(child);
            }
            g_dir_close(dir);
        }
    }
    as_ui_toast(state->root, "Cache cleared.");
    refresh_settings(state);
}

static void on_settings_refresh(gpointer page)
{
    SettingsState *state = g_object_get_data(G_OBJECT(page), "settings-state");
    if (state)
        refresh_settings(state);
}

GtkWidget *as_settings_new(AsAppContext *ctx, AsNavigator *nav)
{
    SettingsState *state = g_new0(SettingsState, 1);
    state->ctx = ctx;
    state->nav = nav;

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(main_box, 32);
    gtk_widget_set_margin_end(main_box, 32);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 32);

    GtkWidget *hdr = as_ui_section_title("Studio Settings", "Configure rendering preferences, output paths and multimedia tools");
    gtk_box_append(GTK_BOX(main_box), hdr);

    /* Export Directory Card */
    GtkWidget *dir_card = as_ui_card();
    GtkWidget *dir_title = as_ui_label("Default Video Export Directory", "section-title");
    gtk_box_append(GTK_BOX(dir_card), dir_title);

    state->export_dir_entry = gtk_entry_new();
    gtk_box_append(GTK_BOX(dir_card), state->export_dir_entry);
    gtk_box_append(GTK_BOX(main_box), dir_card);

    /* System Multimedia Tools Card */
    GtkWidget *media_card = as_ui_card();
    GtkWidget *media_title = as_ui_label("Multimedia Engines", "section-title");
    gtk_box_append(GTK_BOX(media_card), media_title);

    state->ffmpeg_path_lbl = as_ui_label("FFmpeg: Checking...", "section-subtitle");
    state->ffprobe_path_lbl = as_ui_label("ffprobe: Checking...", "section-subtitle");
    gtk_box_append(GTK_BOX(media_card), state->ffmpeg_path_lbl);
    gtk_box_append(GTK_BOX(media_card), state->ffprobe_path_lbl);
    gtk_box_append(GTK_BOX(main_box), media_card);

    /* Cache Management Card */
    GtkWidget *cache_card = as_ui_card();
    GtkWidget *cache_title = as_ui_label("Cache & Storage", "section-title");
    gtk_box_append(GTK_BOX(cache_card), cache_title);

    state->cache_size_lbl = as_ui_label("Local Cache Usage: 0.0 MB", "section-subtitle");
    gtk_box_append(GTK_BOX(cache_card), state->cache_size_lbl);

    GtkWidget *clear_btn = as_ui_button("Clear Cached Assets", "btn-secondary");
    g_signal_connect(clear_btn, "clicked", G_CALLBACK(on_clear_cache_clicked), state);
    gtk_box_append(GTK_BOX(cache_card), clear_btn);
    gtk_box_append(GTK_BOX(main_box), cache_card);

    /* Save Settings Button */
    GtkWidget *save_btn = as_ui_button("Save Preferences", "btn-primary");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_save_settings_clicked), state);
    gtk_box_append(GTK_BOX(main_box), save_btn);

    GtkWidget *scrolled = as_ui_scrolled(main_box);
    state->root = scrolled;
    g_object_set_data_full(G_OBJECT(scrolled), "settings-state", state, g_free);
    as_page_set_refresh(scrolled, on_settings_refresh, state);

    refresh_settings(state);
    return scrolled;
}

