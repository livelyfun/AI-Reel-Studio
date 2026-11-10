/* project_library.c — project management, search, duplicate, delete and open. */
#define _GNU_SOURCE
#include "as_ui.h"
#include "as_core.h"
#include "as_storage.h"

typedef struct {
    AsAppContext *ctx;
    AsNavigator *nav;
    GtkWidget *root;
    GtkWidget *search_entry;
    GtkWidget *list_box;
} LibraryState;

typedef struct {
    LibraryState *state;
    gchar *project_id;
    gchar *project_name;
} ProjectActionData;

static void free_action_data(gpointer data)
{
    ProjectActionData *ad = data;
    g_free(ad->project_id);
    g_free(ad->project_name);
    g_free(ad);
}

static void refresh_library(LibraryState *state);

static void on_open_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ProjectActionData *ad = user_data;
    if (ad->state->nav && ad->state->nav->open_project)
        ad->state->nav->open_project(ad->state->nav->user_data, ad->project_id);
}

static void on_duplicate_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ProjectActionData *ad = user_data;
    GError *error = NULL;
    gchar *new_id = as_project_duplicate(ad->state->ctx->db, ad->project_id, &error);
    if (new_id) {
        as_ui_toast(ad->state->root, "Project duplicated.");
        g_free(new_id);
        refresh_library(ad->state);
    } else {
        as_ui_toast(ad->state->root, error ? error->message : "Duplication failed");
        if (error) g_error_free(error);
    }
}

static void on_delete_confirmed(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ProjectActionData *ad = user_data;
    GError *error = NULL;
    if (as_project_delete(ad->state->ctx->db, ad->project_id, &error)) {
        as_ui_toast(ad->state->root, "Project deleted.");
        refresh_library(ad->state);
    } else {
        as_ui_toast(ad->state->root, error ? error->message : "Delete failed");
        if (error) g_error_free(error);
    }
}

static void on_delete_clicked(GtkButton *btn, gpointer user_data)
{
    on_delete_confirmed(btn, user_data);
}

static void refresh_library(LibraryState *state)
{
    GtkWidget *child = NULL;
    while ((child = gtk_widget_get_first_child(state->list_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->list_box), child);

    const gchar *search = gtk_editable_get_text(GTK_EDITABLE(state->search_entry));
    GPtrArray *projects = as_project_list(state->ctx->db, search && *search ? search : NULL, NULL);

    if (!projects || projects->len == 0) {
        GtkWidget *empty = as_ui_card();
        GtkWidget *lbl = as_ui_label(search && *search ? "No matching projects found." : "No projects saved yet. Create a new reel to get started!", "section-subtitle");
        gtk_box_append(GTK_BOX(empty), lbl);
        gtk_box_append(GTK_BOX(state->list_box), empty);
        if (projects) g_ptr_array_unref(projects);
        return;
    }

    for (guint i = 0; i < projects->len; i++) {
        AsProjectSummary *ps = g_ptr_array_index(projects, i);
        GtkWidget *card = as_ui_card();
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

        GtkWidget *thumb = as_ui_picture_for_file(ps->thumbnail_path, 90, 90);
        gtk_box_append(GTK_BOX(row), thumb);

        GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        gtk_widget_set_hexpand(info, TRUE);

        GtkWidget *name_lbl = as_ui_label(ps->name, "section-title");
        gchar *meta = g_strdup_printf("Status: %s  •  Duration: %.0fs  •  Updated: %s",
                                      ps->status ? ps->status : "draft",
                                      ps->total_duration,
                                      ps->updated_at ? ps->updated_at : "recently");
        GtkWidget *meta_lbl = as_ui_label(meta, "section-subtitle");
        g_free(meta);

        gtk_box_append(GTK_BOX(info), name_lbl);
        gtk_box_append(GTK_BOX(info), meta_lbl);
        gtk_box_append(GTK_BOX(row), info);

        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget *open_btn = as_ui_button("Open", "btn-primary");
        GtkWidget *dup_btn = as_ui_button("Duplicate", "btn-secondary");
        GtkWidget *del_btn = as_ui_button("Delete", "btn-danger");

        ProjectActionData *ad = g_new0(ProjectActionData, 1);
        ad->state = state;
        ad->project_id = g_strdup(ps->id);
        ad->project_name = g_strdup(ps->name);

        g_signal_connect_data(open_btn, "clicked", G_CALLBACK(on_open_clicked), ad, NULL, 0);
        g_signal_connect_data(dup_btn, "clicked", G_CALLBACK(on_duplicate_clicked), ad, NULL, 0);
        g_signal_connect_data(del_btn, "clicked", G_CALLBACK(on_delete_clicked), ad, (GClosureNotify)free_action_data, 0);

        gtk_box_append(GTK_BOX(actions), open_btn);
        gtk_box_append(GTK_BOX(actions), dup_btn);
        gtk_box_append(GTK_BOX(actions), del_btn);
        gtk_box_append(GTK_BOX(row), actions);

        gtk_box_append(GTK_BOX(card), row);
        gtk_box_append(GTK_BOX(state->list_box), card);
    }

    g_ptr_array_unref(projects);
}

static void on_search_changed(GtkEditable *editable, gpointer user_data)
{
    (void)editable;
    LibraryState *state = user_data;
    refresh_library(state);
}

static void on_library_refresh(gpointer page)
{
    LibraryState *state = g_object_get_data(G_OBJECT(page), "library-state");
    if (state)
        refresh_library(state);
}

GtkWidget *as_project_library_new(AsAppContext *ctx, AsNavigator *nav)
{
    LibraryState *state = g_new0(LibraryState, 1);
    state->ctx = ctx;
    state->nav = nav;

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(main_box, 32);
    gtk_widget_set_margin_end(main_box, 32);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 32);

    GtkWidget *header = as_ui_section_title("Project Library", "Manage, reopen and duplicate your AI video projects");
    gtk_box_append(GTK_BOX(main_box), header);

    /* Search bar */
    state->search_entry = gtk_search_entry_new();
    gtk_widget_set_hexpand(state->search_entry, TRUE);
    g_signal_connect(state->search_entry, "search-changed", G_CALLBACK(on_search_changed), state);
    gtk_box_append(GTK_BOX(main_box), state->search_entry);

    /* List Container */
    state->list_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(main_box), state->list_box);

    GtkWidget *scrolled = as_ui_scrolled(main_box);
    state->root = scrolled;
    g_object_set_data_full(G_OBJECT(scrolled), "library-state", state, g_free);
    as_page_set_refresh(scrolled, on_library_refresh, state);

    refresh_library(state);
    return scrolled;
}
