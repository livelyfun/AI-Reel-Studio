/* as_ui.h — GTK4 interface entry points. */
#pragma once

#include <gtk/gtk.h>
#include "as_app.h"

typedef struct _AsNavigator AsNavigator;
struct _AsNavigator {
    gpointer user_data;
    void (*show_page)(gpointer user_data, const gchar *name);
    void (*open_project)(gpointer user_data, const gchar *project_id);
    void (*back_to_projects)(gpointer user_data);
    void (*notify)(gpointer user_data, const gchar *message);
    void (*refresh)(gpointer user_data);
};

GtkApplication *as_application_new(AsAppContext *ctx);

GtkWidget *as_dashboard_new(AsAppContext *ctx, AsNavigator *nav);
GtkWidget *as_reel_creator_new(AsAppContext *ctx, AsNavigator *nav);
GtkWidget *as_project_library_new(AsAppContext *ctx, AsNavigator *nav);
GtkWidget *as_api_manager_new(AsAppContext *ctx, AsNavigator *nav);
GtkWidget *as_settings_new(AsAppContext *ctx, AsNavigator *nav);
GtkWidget *as_workspace_new(AsAppContext *ctx, AsNavigator *nav,
                            const gchar *project_id);

/* Refresh hooks invoked when a page becomes visible. */
typedef void (*AsPageRefreshFunc)(gpointer page);
void as_page_set_refresh(GtkWidget *page, AsPageRefreshFunc refresh, gpointer data);

/* UI utilities */
void as_ui_toast(GtkWidget *widget, const gchar *message);
void as_ui_apply_css(void);
GtkWidget *as_ui_scrolled(GtkWidget *child);
GtkWidget *as_ui_label(const gchar *text, const gchar *css_class);
GtkWidget *as_ui_section_title(const gchar *text, const gchar *subtitle);
GtkWidget *as_ui_card(void);
GtkWidget *as_ui_button(const gchar *label, const gchar *css_class);
GtkWidget *as_ui_picture_for_file(const gchar *path, gint width, gint height);
GtkWidget *as_ui_provider_route_summary(AsAppContext *ctx);

typedef gpointer (*AsUiTaskFunc)(gpointer data, GCancellable *cancellable,
                                 GError **error);
typedef void (*AsUiDoneFunc)(gpointer result, GError *error, gpointer user_data);
typedef void (*AsUiFreeFunc)(gpointer data);

/* Runs task on a worker thread, then done on the GTK main thread. `data` is
 * passed to task and freed with data_free after done. result is freed with
 * result_free after done (either may be NULL). */
void as_ui_run_async(AsUiTaskFunc task, gpointer data, AsUiFreeFunc data_free,
                     AsUiDoneFunc done, gpointer user_dataArg,
                     AsUiFreeFunc user_data_free, AsUiFreeFunc result_free);

void as_ui_copy_text(GtkWidget *widget, const gchar *text);
