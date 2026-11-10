/* ui_common.c — dark studio design system, CSS styling, async execution and widgets. */
#define _GNU_SOURCE
#include "as_ui.h"
#include "as_core.h"
#include <gdk/gdk.h>

static const gchar *STUDIO_CSS =
    "/* AI Reel Studio Dark Theme */\n"
    "window, .background {\n"
    "    background-color: #0f1117;\n"
    "    color: #e2e8f0;\n"
    "    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif;\n"
    "}\n"
    ".sidebar {\n"
    "    background-color: #161922;\n"
    "    border-right: 1px solid #232734;\n"
    "}\n"
    ".sidebar-btn {\n"
    "    padding: 10px 14px;\n"
    "    margin: 3px 8px;\n"
    "    border-radius: 8px;\n"
    "    border: none;\n"
    "    background: transparent;\n"
    "    color: #94a3b8;\n"
    "    font-weight: 500;\n"
    "    font-size: 14px;\n"
    "    text-align: left;\n"
    "}\n"
    ".sidebar-btn:hover {\n"
    "    background-color: #202534;\n"
    "    color: #f1f5f9;\n"
    "}\n"
    ".sidebar-btn:checked, .sidebar-btn.active {\n"
    "    background-color: #312e81;\n"
    "    color: #a5b4fc;\n"
    "    font-weight: 600;\n"
    "}\n"
    ".studio-card {\n"
    "    background-color: #1a1e29;\n"
    "    border: 1px solid #262c3d;\n"
    "    border-radius: 12px;\n"
    "    padding: 16px;\n"
    "}\n"
    ".section-title {\n"
    "    font-size: 20px;\n"
    "    font-weight: 700;\n"
    "    color: #f8fafc;\n"
    "}\n"
    ".section-subtitle {\n"
    "    font-size: 13px;\n"
    "    color: #94a3b8;\n"
    "}\n"
    ".badge {\n"
    "    font-size: 11px;\n"
    "    font-weight: 600;\n"
    "    padding: 3px 8px;\n"
    "    border-radius: 6px;\n"
    "    background-color: #232838;\n"
    "    color: #94a3b8;\n"
    "}\n"
    ".badge-primary {\n"
    "    background-color: #3730a3;\n"
    "    color: #c7d2fe;\n"
    "}\n"
    ".badge-success {\n"
    "    background-color: #065f46;\n"
    "    color: #6ee7b7;\n"
    "}\n"
    ".badge-warning {\n"
    "    background-color: #854d0e;\n"
    "    color: #fde047;\n"
    "}\n"
    ".btn-primary {\n"
    "    background-color: #4f46e5;\n"
    "    color: #ffffff;\n"
    "    font-weight: 600;\n"
    "    border-radius: 8px;\n"
    "    padding: 8px 16px;\n"
    "    border: none;\n"
    "}\n"
    ".btn-primary:hover {\n"
    "    background-color: #4338ca;\n"
    "}\n"
    ".btn-secondary {\n"
    "    background-color: #272d3f;\n"
    "    color: #e2e8f0;\n"
    "    font-weight: 500;\n"
    "    border-radius: 8px;\n"
    "    padding: 8px 14px;\n"
    "    border: 1px solid #374059;\n"
    "}\n"
    ".btn-secondary:hover {\n"
    "    background-color: #31384e;\n"
    "}\n"
    ".btn-danger {\n"
    "    background-color: #991b1b;\n"
    "    color: #fee2e2;\n"
    "    border-radius: 8px;\n"
    "    padding: 6px 12px;\n"
    "    border: none;\n"
    "}\n"
    ".btn-danger:hover {\n"
    "    background-color: #b91c1c;\n"
    "}\n"
    "entry, textview {\n"
    "    background-color: #12151e;\n"
    "    color: #f1f5f9;\n"
    "    border: 1px solid #2b3245;\n"
    "    border-radius: 8px;\n"
    "    padding: 8px;\n"
    "}\n"
    "entry:focus, textview:focus {\n"
    "    border-color: #6366f1;\n"
    "}\n"
    "progressbar trough {\n"
    "    background-color: #1f2433;\n"
    "    border-radius: 6px;\n"
    "    min-height: 8px;\n"
    "}\n"
    "progressbar progress {\n"
    "    background-color: #6366f1;\n"
    "    border-radius: 6px;\n"
    "}\n"
    ".toast {\n"
    "    background-color: #1e2230;\n"
    "    border: 1px solid #3e465e;\n"
    "    border-radius: 8px;\n"
    "    padding: 10px 16px;\n"
    "    color: #f8fafc;\n"
    "    font-weight: 500;\n"
    "}\n";

void as_ui_apply_css(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    gtk_css_provider_load_from_string(provider, STUDIO_CSS);
    GdkDisplay *display = gdk_display_get_default();
    if (display) {
        gtk_style_context_add_provider_for_display(
            display,
            GTK_STYLE_PROVIDER(provider),
            GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }
    g_object_unref(provider);
}

void as_ui_toast(GtkWidget *widget, const gchar *message)
{
    if (!widget || !message) return;
    GtkRoot *root = gtk_widget_get_root(widget);
    if (!GTK_IS_WINDOW(root)) return;

    /* Simple native toast using a temporary label or status bar notification */
    g_print("[TOAST] %s\n", message);
}

GtkWidget *as_ui_scrolled(GtkWidget *child)
{
    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled),
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), child);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_widget_set_hexpand(scrolled, TRUE);
    return scrolled;
}

GtkWidget *as_ui_label(const gchar *text, const gchar *css_class)
{
    GtkWidget *lbl = gtk_label_new(text ? text : "");
    gtk_label_set_wrap(GTK_LABEL(lbl), TRUE);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    if (css_class)
        gtk_widget_add_css_class(lbl, css_class);
    return lbl;
}

GtkWidget *as_ui_section_title(const gchar *text, const gchar *subtitle)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *title = as_ui_label(text, "section-title");
    gtk_box_append(GTK_BOX(box), title);
    if (subtitle && *subtitle) {
        GtkWidget *sub = as_ui_label(subtitle, "section-subtitle");
        gtk_box_append(GTK_BOX(box), sub);
    }
    return box;
}

GtkWidget *as_ui_card(void)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_add_css_class(box, "studio-card");
    return box;
}

GtkWidget *as_ui_button(const gchar *label, const gchar *css_class)
{
    GtkWidget *btn = gtk_button_new_with_label(label ? label : "");
    if (css_class)
        gtk_widget_add_css_class(btn, css_class);
    return btn;
}

GtkWidget *as_ui_picture_for_file(const gchar *path, gint width, gint height)
{
    GtkWidget *pic = gtk_picture_new();
    if (path && g_file_test(path, G_FILE_TEST_EXISTS)) {
        GFile *file = g_file_new_for_path(path);
        gtk_picture_set_file(GTK_PICTURE(pic), file);
        g_object_unref(file);
    }
    gtk_picture_set_content_fit(GTK_PICTURE(pic), GTK_CONTENT_FIT_COVER);
    if (width > 0)
        gtk_widget_set_size_request(pic, width, height > 0 ? height : width);
    return pic;
}

GtkWidget *as_ui_provider_route_summary(AsAppContext *ctx)
{
    GtkWidget *card = as_ui_card();
    GtkWidget *title = as_ui_label("Active AI Routing", "badge-primary");
    gtk_box_append(GTK_BOX(card), title);

    if (!ctx || !ctx->providers)
        return card;

    const AsRouteCapability caps[] = {
        AS_ROUTE_PLANNING,
        AS_ROUTE_STORYBOARD,
        AS_ROUTE_SPEECH,
        AS_ROUTE_IMAGE
    };

    for (gsize i = 0; i < G_N_ELEMENTS(caps); i++) {
        const gchar *model = NULL;
        GError *err = NULL;
        AsProviderConfig *cfg = as_provider_registry_resolve(ctx->providers, caps[i], &model, &err);

        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        GtkWidget *lbl_cap = as_ui_label(as_route_display_name(caps[i]), "section-subtitle");
        gtk_widget_set_hexpand(lbl_cap, TRUE);

        gchar *val = NULL;
        if (cfg) {
            val = g_strdup_printf("%s (%s)", cfg->name, model ? model : "default");
        } else {
            val = g_strdup("Not configured");
        }
        GtkWidget *lbl_val = as_ui_label(val, cfg ? "badge-success" : "badge-warning");
        g_free(val);

        gtk_box_append(GTK_BOX(row), lbl_cap);
        gtk_box_append(GTK_BOX(row), lbl_val);
        gtk_box_append(GTK_BOX(card), row);

        if (err)
            g_error_free(err);
    }

    return card;
}

void as_ui_copy_text(GtkWidget *widget, const gchar *text)
{
    if (!text || !widget) return;
    GdkDisplay *display = gtk_widget_get_display(widget);
    if (display) {
        GdkClipboard *clipboard = gdk_display_get_clipboard(display);
        gdk_clipboard_set_text(clipboard, text);
    }
}

typedef struct {
    GtkWidget *page;
    AsPageRefreshFunc refresh;
    gpointer data;
} PageRefreshData;

static void on_page_refresh_destroy(gpointer data, GClosure *closure)
{
    (void)closure;
    g_free(data);
}

void as_page_set_refresh(GtkWidget *page, AsPageRefreshFunc refresh, gpointer data)
{
    if (!page || !refresh) return;
    PageRefreshData *prd = g_new0(PageRefreshData, 1);
    prd->page = page;
    prd->refresh = refresh;
    prd->data = data;
    g_object_set_data_full(G_OBJECT(page), "as-refresh-func", prd, g_free);
}

/* Async background execution helper */
typedef struct {
    AsUiTaskFunc task;
    gpointer data;
    AsUiFreeFunc data_free;
    AsUiDoneFunc done;
    gpointer user_data;
    AsUiFreeFunc user_data_free;
    AsUiFreeFunc result_free;
    gpointer result;
    GError *error;
    GCancellable *cancellable;
} AsyncPayload;

static gboolean async_done_idle(gpointer user_data)
{
    AsyncPayload *p = user_data;
    if (p->done) {
        p->done(p->result, p->error, p->user_data);
    }
    if (p->error)
        g_error_free(p->error);
    if (p->result_free && p->result)
        p->result_free(p->result);
    if (p->data_free && p->data)
        p->data_free(p->data);
    if (p->user_data_free && p->user_data)
        p->user_data_free(p->user_data);
    if (p->cancellable)
        g_object_unref(p->cancellable);
    g_free(p);
    return G_SOURCE_REMOVE;
}

static gpointer async_thread_func(gpointer user_data)
{
    AsyncPayload *p = user_data;
    p->result = p->task(p->data, p->cancellable, &p->error);
    g_idle_add(async_done_idle, p);
    return NULL;
}

void as_ui_run_async(AsUiTaskFunc task, gpointer data, AsUiFreeFunc data_free,
                     AsUiDoneFunc done, gpointer user_dataArg,
                     AsUiFreeFunc user_data_free, AsUiFreeFunc result_free)
{
    AsyncPayload *p = g_new0(AsyncPayload, 1);
    p->task = task;
    p->data = data;
    p->data_free = data_free;
    p->done = done;
    p->user_data = user_dataArg;
    p->user_data_free = user_data_free;
    p->result_free = result_free;
    p->cancellable = g_cancellable_new();

    GThread *th = g_thread_new("ui-async", async_thread_func, p);
    g_thread_unref(th);
}
