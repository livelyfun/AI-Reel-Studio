/* dashboard.c — studio overview, stats, recent projects, routing status. */
#define _GNU_SOURCE
#include "as_ui.h"
#include "as_core.h"
#include "as_storage.h"

typedef struct {
    AsAppContext *ctx;
    AsNavigator *nav;
    GtkWidget *root;
    GtkWidget *stats_box;
    GtkWidget *recent_box;
    GtkWidget *route_box;
} DashboardState;

static void on_create_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    DashboardState *state = user_data;
    if (state->nav && state->nav->show_page)
        state->nav->show_page(state->nav->user_data, "create");
}

static void on_project_row_clicked(GtkGestureClick *gesture, gint n_press, gdouble x, gdouble y, gpointer user_data)
{
    (void)gesture; (void)n_press; (void)x; (void)y;
    gchar *proj_id = user_data;
    /* Handled via action callback */
}

typedef struct {
    DashboardState *state;
    gchar *proj_id;
} OpenProjectData;

static void on_open_project_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    OpenProjectData *opd = user_data;
    if (opd->state->nav && opd->state->nav->open_project) {
        opd->state->nav->open_project(opd->state->nav->user_data, opd->proj_id);
    }
}

static void free_opd(gpointer data)
{
    OpenProjectData *opd = data;
    g_free(opd->proj_id);
    g_free(opd);
}

static void refresh_dashboard(DashboardState *state)
{
    /* Clear and reload stats */
    GtkWidget *child = NULL;
    while ((child = gtk_widget_get_first_child(state->stats_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->stats_box), child);

    guint total_projects = 0;
    guint total_exports = 0;
    gsize storage_bytes = 0;

    if (state->ctx->paths) {
        storage_bytes += as_dir_size(state->ctx->paths->projects_dir);
        storage_bytes += as_dir_size(state->ctx->paths->cache_dir);
    }

    GPtrArray *projects = as_project_list(state->ctx->db, NULL, NULL);
    if (projects) {
        total_projects = projects->len;
    }

    /* Metric Cards */
    struct {
        const gchar *label;
        gchar *value;
        const gchar *badge_class;
    } metrics[] = {
        { "Total Projects", g_strdup_printf("%u", total_projects), "badge-primary" },
        { "Active Providers", g_strdup_printf("%u", state->ctx->providers ? as_provider_registry_list(state->ctx->providers)->len : 0), "badge-success" },
        { "Storage Used", g_strdup_printf("%.1f MB", (gdouble)storage_bytes / (1024.0 * 1024.0)), "badge" }
    };

    for (gsize i = 0; i < G_N_ELEMENTS(metrics); i++) {
        GtkWidget *card = as_ui_card();
        GtkWidget *val_lbl = as_ui_label(metrics[i].value, "section-title");
        GtkWidget *desc_lbl = as_ui_label(metrics[i].label, metrics[i].badge_class);
        gtk_box_append(GTK_BOX(card), val_lbl);
        gtk_box_append(GTK_BOX(card), desc_lbl);
        gtk_widget_set_hexpand(card, TRUE);
        gtk_box_append(GTK_BOX(state->stats_box), card);
        g_free(metrics[i].value);
    }

    /* Refresh Route Summary */
    while ((child = gtk_widget_get_first_child(state->route_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->route_box), child);
    GtkWidget *route_summary = as_ui_provider_route_summary(state->ctx);
    gtk_box_append(GTK_BOX(state->route_box), route_summary);

    /* Refresh Recent Projects */
    while ((child = gtk_widget_get_first_child(state->recent_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->recent_box), child);

    if (!projects || projects->len == 0) {
        GtkWidget *empty_card = as_ui_card();
        GtkWidget *empty_lbl = as_ui_label("No video reels generated yet. Click 'Create Reel' to start!", "section-subtitle");
        gtk_box_append(GTK_BOX(empty_card), empty_lbl);
        gtk_box_append(GTK_BOX(state->recent_box), empty_card);
    } else {
        guint count = MIN(projects->len, 5);
        for (guint i = 0; i < count; i++) {
            AsProjectSummary *ps = g_ptr_array_index(projects, i);
            GtkWidget *row_card = as_ui_card();
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

            /* Thumbnail */
            GtkWidget *thumb = as_ui_picture_for_file(ps->thumbnail_path, 80, 80);
            gtk_box_append(GTK_BOX(row), thumb);

            /* Info */
            GtkWidget *info_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_set_hexpand(info_box, TRUE);
            GtkWidget *name_lbl = as_ui_label(ps->name, "section-title");
            gchar *sub_text = g_strdup_printf("Status: %s  •  Duration: %.0fs  •  Updated: %s",
                                              ps->status ? ps->status : "draft",
                                              ps->total_duration,
                                              ps->updated_at ? ps->updated_at : "");
            GtkWidget *sub_lbl = as_ui_label(sub_text, "section-subtitle");
            g_free(sub_text);

            gtk_box_append(GTK_BOX(info_box), name_lbl);
            gtk_box_append(GTK_BOX(info_box), sub_lbl);
            gtk_box_append(GTK_BOX(row), info_box);

            /* Open Button */
            GtkWidget *open_btn = as_ui_button("Open Project", "btn-secondary");
            OpenProjectData *opd = g_new0(OpenProjectData, 1);
            opd->state = state;
            opd->proj_id = g_strdup(ps->id);
            g_signal_connect_data(open_btn, "clicked", G_CALLBACK(on_open_project_clicked), opd, (GClosureNotify)free_opd, 0);

            gtk_box_append(GTK_BOX(row), open_btn);
            gtk_box_append(GTK_BOX(row_card), row);
            gtk_box_append(GTK_BOX(state->recent_box), row_card);
        }
    }

    if (projects)
        g_ptr_array_unref(projects);
}

static void on_dashboard_refresh(gpointer page)
{
    DashboardState *state = g_object_get_data(G_OBJECT(page), "dashboard-state");
    if (state)
        refresh_dashboard(state);
}

GtkWidget *as_dashboard_new(AsAppContext *ctx, AsNavigator *nav)
{
    DashboardState *state = g_new0(DashboardState, 1);
    state->ctx = ctx;
    state->nav = nav;

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(main_box, 32);
    gtk_widget_set_margin_end(main_box, 32);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 32);

    /* Header & Quick Create */
    GtkWidget *header_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *title_box = as_ui_section_title("Studio Dashboard", "Native Linux AI Short-Form Video Production");
    gtk_widget_set_hexpand(title_box, TRUE);
    GtkWidget *create_btn = as_ui_button("+ Create New Reel", "btn-primary");
    g_signal_connect(create_btn, "clicked", G_CALLBACK(on_create_clicked), state);

    gtk_box_append(GTK_BOX(header_row), title_box);
    gtk_box_append(GTK_BOX(header_row), create_btn);
    gtk_box_append(GTK_BOX(main_box), header_row);

    /* Metrics Row */
    state->stats_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    gtk_box_append(GTK_BOX(main_box), state->stats_box);

    /* Content 2-Column: Left = Recent Projects, Right = AI Routing & Health */
    GtkWidget *columns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 24);
    gtk_widget_set_hexpand(columns, TRUE);

    GtkWidget *left_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_hexpand(left_col, TRUE);
    GtkWidget *recent_hdr = as_ui_label("Recent Projects", "section-title");
    gtk_box_append(GTK_BOX(left_col), recent_hdr);
    state->recent_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(left_col), state->recent_box);

    GtkWidget *right_col = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_size_request(right_col, 320, -1);
    GtkWidget *routes_hdr = as_ui_label("System & AI Status", "section-title");
    gtk_box_append(GTK_BOX(right_col), routes_hdr);
    state->route_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(right_col), state->route_box);

    gtk_box_append(GTK_BOX(columns), left_col);
    gtk_box_append(GTK_BOX(columns), right_col);
    gtk_box_append(GTK_BOX(main_box), columns);

    GtkWidget *scrolled = as_ui_scrolled(main_box);
    state->root = scrolled;

    g_object_set_data_full(G_OBJECT(scrolled), "dashboard-state", state, g_free);
    as_page_set_refresh(scrolled, on_dashboard_refresh, state);

    refresh_dashboard(state);
    return scrolled;
}
