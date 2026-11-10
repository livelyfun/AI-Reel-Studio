/* application.c — GTK4 application, main window, navigation and stack. */
#define _GNU_SOURCE
#include "as_app.h"
#include "as_ui.h"
#include "as_core.h"

typedef struct {
    AsAppContext *ctx;
    GtkApplication *app;
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *sidebar;
    GtkWidget *workspace_page;
    GHashTable *nav_buttons;   /* page_name -> GtkButton* */
    AsNavigator navigator;
} AppUiState;

static void nav_show_page(gpointer user_data, const gchar *name);
static void nav_open_project(gpointer user_data, const gchar *project_id);
static void nav_back_to_projects(gpointer user_data);
static void nav_notify(gpointer user_data, const gchar *message);
static void nav_refresh(gpointer user_data);

static void on_sidebar_btn_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    const gchar *page_name = g_object_get_data(G_OBJECT(btn), "page-name");
    AppUiState *ui = user_data;
    nav_show_page(ui, page_name);
}

static void update_sidebar_selection(AppUiState *ui, const gchar *active_page)
{
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init(&iter, ui->nav_buttons);
    while (g_hash_table_iter_next(&iter, &key, &value)) {
        GtkWidget *btn = value;
        if (g_strcmp0(key, active_page) == 0) {
            gtk_widget_add_css_class(btn, "active");
        } else {
            gtk_widget_remove_css_class(btn, "active");
        }
    }
}

static void nav_show_page(gpointer user_data, const gchar *name)
{
    AppUiState *ui = user_data;
    if (!name || !ui->stack) return;

    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), name);
    update_sidebar_selection(ui, name);

    /* Trigger page refresh if set */
    GtkWidget *child = gtk_stack_get_child_by_name(GTK_STACK(ui->stack), name);
    if (child) {
        gpointer prd_ptr = g_object_get_data(G_OBJECT(child), "as-refresh-func");
        if (prd_ptr) {
            struct { GtkWidget *p; AsPageRefreshFunc r; gpointer d; } *prd = prd_ptr;
            if (prd->r) prd->r(child);
        }
    }
}

static void nav_open_project(gpointer user_data, const gchar *project_id)
{
    AppUiState *ui = user_data;
    if (!project_id) return;

    if (ui->workspace_page) {
        gtk_stack_remove(GTK_STACK(ui->stack), ui->workspace_page);
        ui->workspace_page = NULL;
    }

    ui->workspace_page = as_workspace_new(ui->ctx, &ui->navigator, project_id);
    gtk_stack_add_named(GTK_STACK(ui->stack), ui->workspace_page, "workspace");
    gtk_stack_set_visible_child_name(GTK_STACK(ui->stack), "workspace");
    update_sidebar_selection(ui, "workspace");
}

static void nav_back_to_projects(gpointer user_data)
{
    AppUiState *ui = user_data;
    nav_show_page(ui, "projects");
}

static void nav_notify(gpointer user_data, const gchar *message)
{
    AppUiState *ui = user_data;
    as_ui_toast(ui->window, message);
}

static void nav_refresh(gpointer user_data)
{
    AppUiState *ui = user_data;
    const gchar *cur = gtk_stack_get_visible_child_name(GTK_STACK(ui->stack));
    if (cur) nav_show_page(ui, cur);
}

static void on_app_activate(GApplication *app, gpointer user_data)
{
    AsAppContext *ctx = user_data;
    AppUiState *ui = g_new0(AppUiState, 1);
    ui->ctx = ctx;
    ui->app = GTK_APPLICATION(app);
    ui->nav_buttons = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

    ui->navigator.user_data = ui;
    ui->navigator.show_page = nav_show_page;
    ui->navigator.open_project = nav_open_project;
    ui->navigator.back_to_projects = nav_back_to_projects;
    ui->navigator.notify = nav_notify;
    ui->navigator.refresh = nav_refresh;

    as_ui_apply_css();

    ui->window = gtk_application_window_new(GTK_APPLICATION(app));
    gtk_window_set_title(GTK_WINDOW(ui->window), "AI Reel Studio");

    gint win_w = (ctx->config && ctx->config->window_width > 600) ? ctx->config->window_width : 1280;
    gint win_h = (ctx->config && ctx->config->window_height > 400) ? ctx->config->window_height : 820;
    gtk_window_set_default_size(GTK_WINDOW(ui->window), win_w, win_h);

    GtkWidget *layout = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

    /* Sidebar Navigation */
    ui->sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(ui->sidebar, "sidebar");
    gtk_widget_set_size_request(ui->sidebar, 240, -1);
    gtk_widget_set_margin_top(ui->sidebar, 16);
    gtk_widget_set_margin_bottom(ui->sidebar, 16);

    /* Studio Brand Header */
    GtkWidget *brand_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    gtk_widget_set_margin_start(brand_box, 16);
    gtk_widget_set_margin_bottom(brand_box, 20);
    GtkWidget *brand_lbl = as_ui_label("AI Reel Studio", "section-title");
    GtkWidget *sub_lbl = as_ui_label("Native C17 Studio", "badge-primary");
    gtk_box_append(GTK_BOX(brand_box), brand_lbl);
    gtk_box_append(GTK_BOX(brand_box), sub_lbl);
    gtk_box_append(GTK_BOX(ui->sidebar), brand_box);

    struct {
        const gchar *id;
        const gchar *label;
    } pages[] = {
        { "dashboard", "📊 Dashboard" },
        { "create", "⚡ Create Reel" },
        { "projects", "📁 Projects" },
        { "api", "🔌 API Manager" },
        { "settings", "⚙ Settings" }
    };

    for (gsize i = 0; i < G_N_ELEMENTS(pages); i++) {
        GtkWidget *btn = gtk_button_new_with_label(pages[i].label);
        gtk_widget_add_css_class(btn, "sidebar-btn");
        g_object_set_data_full(G_OBJECT(btn), "page-name", g_strdup(pages[i].id), g_free);
        g_signal_connect(btn, "clicked", G_CALLBACK(on_sidebar_btn_clicked), ui);
        gtk_box_append(GTK_BOX(ui->sidebar), btn);
        g_hash_table_insert(ui->nav_buttons, g_strdup(pages[i].id), btn);
    }

    gtk_box_append(GTK_BOX(layout), ui->sidebar);

    /* Main Stack for content views */
    ui->stack = gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(ui->stack), GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_stack_set_transition_duration(GTK_STACK(ui->stack), 150);
    gtk_widget_set_hexpand(ui->stack, TRUE);
    gtk_widget_set_vexpand(ui->stack, TRUE);

    GtkWidget *dash_page = as_dashboard_new(ctx, &ui->navigator);
    GtkWidget *create_page = as_reel_creator_new(ctx, &ui->navigator);
    GtkWidget *library_page = as_project_library_new(ctx, &ui->navigator);
    GtkWidget *api_page = as_api_manager_new(ctx, &ui->navigator);
    GtkWidget *settings_page = as_settings_new(ctx, &ui->navigator);

    gtk_stack_add_named(GTK_STACK(ui->stack), dash_page, "dashboard");
    gtk_stack_add_named(GTK_STACK(ui->stack), create_page, "create");
    gtk_stack_add_named(GTK_STACK(ui->stack), library_page, "projects");
    gtk_stack_add_named(GTK_STACK(ui->stack), api_page, "api");
    gtk_stack_add_named(GTK_STACK(ui->stack), settings_page, "settings");

    gtk_box_append(GTK_BOX(layout), ui->stack);

    gtk_window_set_child(GTK_WINDOW(ui->window), layout);
    nav_show_page(ui, "dashboard");

    gtk_window_present(GTK_WINDOW(ui->window));
}

GtkApplication *as_application_new(AsAppContext *ctx)
{
    GtkApplication *app = gtk_application_new("org.aireelstudio.AiReelStudio",
                                              G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_app_activate), ctx);
    return app;
}

