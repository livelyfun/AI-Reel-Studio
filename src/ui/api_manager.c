/* api_manager.c — provider registry management, credentials and independent capability routing. */
#define _GNU_SOURCE
#include "as_ui.h"
#include "as_core.h"
#include "as_storage.h"
#include "as_security.h"
#include "as_provider.h"
#include "as_http.h"

typedef struct {
    AsAppContext *ctx;
    AsNavigator *nav;
    GtkWidget *root;
    GtkWidget *providers_box;
    GtkWidget *routing_box;
} ApiManagerState;

typedef struct {
    ApiManagerState *state;
    gchar *provider_id;
} ProviderActionData;

static void free_prov_data(gpointer data)
{
    ProviderActionData *pad = data;
    g_free(pad->provider_id);
    g_free(pad);
}

static void refresh_api_manager(ApiManagerState *state);

/* Connection test worker */
typedef struct {
    AsProviderConfig cfg;
    gchar *api_key;
    gboolean success;
    gchar *status_message;
    gint latency_ms;
} TestConnData;

static gpointer test_conn_task(gpointer user_data, GCancellable *cancellable, GError **error)
{
    TestConnData *tcd = user_data;
    gint64 start_time = g_get_monotonic_time();

    AsTextResult res;
    memset(&res, 0, sizeof(res));

    gboolean ok = as_provider_generate_text(&tcd->cfg, tcd->api_key, tcd->cfg.text_model,
                                            "You are a connection test.",
                                            "Ping", FALSE, 0.1, &res, cancellable, error);
    gint64 end_time = g_get_monotonic_time();
    tcd->latency_ms = (gint)((end_time - start_time) / 1000);
    tcd->success = ok;

    if (ok) {
        tcd->status_message = g_strdup_printf("Connected successfully (%d ms latency)", tcd->latency_ms);
        as_text_result_clear(&res);
    } else {
        tcd->status_message = error && *error ? g_strdup((*error)->message) : g_strdup("Connection failed");
    }
    return tcd;
}

static void test_conn_done(gpointer result, GError *error, gpointer user_data)
{
    (void)error;
    TestConnData *tcd = result;
    ApiManagerState *state = user_data;
    as_ui_toast(state->root, tcd->status_message);
    refresh_api_manager(state);
}

static void free_test_conn_data(gpointer data)
{
    TestConnData *tcd = data;
    as_provider_config_clear(&tcd->cfg);
    if (tcd->api_key) {
        as_secure_clear(tcd->api_key);
        g_free(tcd->api_key);
    }
    g_free(tcd->status_message);
    g_free(tcd);
}

static void on_test_connection_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ProviderActionData *pad = user_data;
    AsProviderConfig *cfg = as_provider_registry_find(pad->state->ctx->providers, pad->provider_id);
    if (!cfg) return;

    gchar *key = as_credential_manager_get(pad->state->ctx->credentials, cfg->id, NULL);

    TestConnData *tcd = g_new0(TestConnData, 1);
    AsProviderConfig *copied = as_provider_config_copy(cfg);
    tcd->cfg = *copied;
    g_free(copied);
    tcd->api_key = key;

    as_ui_toast(pad->state->root, "Testing connection...");
    as_ui_run_async(test_conn_task, tcd, NULL,
                    test_conn_done, pad->state, NULL,
                    free_test_conn_data);
}

static void on_delete_provider_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ProviderActionData *pad = user_data;
    GError *error = NULL;

    /* Delete from database */
    as_provider_store_delete(pad->state->ctx->db, pad->provider_id, &error);
    /* Delete secret */
    as_credential_manager_remove(pad->state->ctx->credentials, pad->provider_id, NULL);
    /* Reload registry */
    as_app_context_reload_providers(pad->state->ctx, NULL);

    as_ui_toast(pad->state->root, "Provider removed.");
    refresh_api_manager(pad->state);
}

/* Dialog for adding/editing provider */
typedef struct {
    ApiManagerState *state;
    GtkWidget *dialog;
    GtkWidget *name_entry;
    GtkWidget *type_combo;
    GtkWidget *url_entry;
    GtkWidget *key_entry;
    GtkWidget *model_entry;
    GtkWidget *timeout_entry;
    gboolean is_edit;
    gchar *edit_id;
} ProviderDialogData;

static void on_provider_dialog_save(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ProviderDialogData *dd = user_data;
    const gchar *name = gtk_editable_get_text(GTK_EDITABLE(dd->name_entry));
    const gchar *url = gtk_editable_get_text(GTK_EDITABLE(dd->url_entry));
    const gchar *key = gtk_editable_get_text(GTK_EDITABLE(dd->key_entry));
    const gchar *model = gtk_editable_get_text(GTK_EDITABLE(dd->model_entry));
    guint type_idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(dd->type_combo));

    if (!name || strlen(name) == 0 || !url || strlen(url) == 0) {
        as_ui_toast(dd->state->root, "Please enter provider name and valid Base URL.");
        return;
    }

    AsProviderConfig cfg;
    as_provider_config_init(&cfg);
    cfg.id = dd->is_edit ? g_strdup(dd->edit_id) : as_uuid_random();
    cfg.name = g_strdup(name);
    cfg.base_url = g_strdup(url);
    cfg.text_model = g_strdup(model && *model ? model : "default");
    cfg.timeout_ms = 60000;
    cfg.enabled = TRUE;

    if (type_idx == 0) {
        cfg.type = AS_PROVIDER_GEMINI;
        cfg.auth_type = AS_AUTH_QUERY_KEY;
        cfg.capabilities = AS_CAP_TEXT | AS_CAP_STRUCTURED | AS_CAP_SPEECH;
    } else if (type_idx == 1) {
        cfg.type = AS_PROVIDER_OPENROUTER;
        cfg.auth_type = AS_AUTH_BEARER;
        cfg.capabilities = AS_CAP_TEXT | AS_CAP_STRUCTURED;
    } else {
        cfg.type = AS_PROVIDER_OPENAI_COMPATIBLE;
        cfg.auth_type = AS_AUTH_BEARER;
        cfg.capabilities = AS_CAP_TEXT | AS_CAP_STRUCTURED;
    }

    /* Save to DB */
    GError *error = NULL;
    as_provider_store_save(dd->state->ctx->db, &cfg, &error);

    /* Save credential securely if provided */
    if (key && *key && g_strcmp0(key, "••••••••") != 0) {
        as_credential_manager_set(dd->state->ctx->credentials, cfg.id, key, TRUE, NULL);
    }

    as_provider_config_clear(&cfg);
    as_app_context_reload_providers(dd->state->ctx, NULL);

    gtk_window_destroy(GTK_WINDOW(dd->dialog));
    as_ui_toast(dd->state->root, "Provider saved successfully.");
    refresh_api_manager(dd->state);
}

static void on_add_provider_clicked(GtkButton *btn, gpointer user_data)
{
    (void)btn;
    ApiManagerState *state = user_data;

    ProviderDialogData *dd = g_new0(ProviderDialogData, 1);
    dd->state = state;

    GtkRoot *root = gtk_widget_get_root(state->root);
    dd->dialog = gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dd->dialog), "Add AI Provider");
    gtk_window_set_modal(GTK_WINDOW(dd->dialog), TRUE);
    if (GTK_IS_WINDOW(root))
        gtk_window_set_transient_for(GTK_WINDOW(dd->dialog), GTK_WINDOW(root));
    gtk_window_set_default_size(GTK_WINDOW(dd->dialog), 480, 420);

    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_start(box, 24);
    gtk_widget_set_margin_end(box, 24);
    gtk_widget_set_margin_top(box, 24);
    gtk_widget_set_margin_bottom(box, 24);

    GtkWidget *title = as_ui_section_title("Configure AI Provider", "Add API credentials for Gemini, OpenRouter, or OpenAI-compatible local models");
    gtk_box_append(GTK_BOX(box), title);

    /* Name */
    gtk_box_append(GTK_BOX(box), as_ui_label("Provider Name", "section-subtitle"));
    dd->name_entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(dd->name_entry), "e.g. My Gemini Pro or Local Ollama");
    gtk_box_append(GTK_BOX(box), dd->name_entry);

    /* Type */
    gtk_box_append(GTK_BOX(box), as_ui_label("Provider Type", "section-subtitle"));
    const gchar *types[] = { "Google Gemini", "OpenRouter", "OpenAI-Compatible (Ollama, vLLM, etc.)", NULL };
    dd->type_combo = gtk_drop_down_new_from_strings(types);
    gtk_box_append(GTK_BOX(box), dd->type_combo);

    /* URL */
    gtk_box_append(GTK_BOX(box), as_ui_label("Base URL", "section-subtitle"));
    dd->url_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(dd->url_entry), "https://generativelanguage.googleapis.com");
    gtk_box_append(GTK_BOX(box), dd->url_entry);

    /* API Key */
    gtk_box_append(GTK_BOX(box), as_ui_label("API Key (Stored securely in Linux Secret Service)", "section-subtitle"));
    dd->key_entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(dd->key_entry), TRUE);
    gtk_box_append(GTK_BOX(box), dd->key_entry);

    /* Model ID */
    gtk_box_append(GTK_BOX(box), as_ui_label("Model ID", "section-subtitle"));
    dd->model_entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(dd->model_entry), "gemini-2.5-flash");
    gtk_box_append(GTK_BOX(box), dd->model_entry);

    /* Action Buttons */
    GtkWidget *btn_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_margin_top(btn_row, 12);
    GtkWidget *save_btn = as_ui_button("Save Provider", "btn-primary");
    g_signal_connect(save_btn, "clicked", G_CALLBACK(on_provider_dialog_save), dd);
    gtk_box_append(GTK_BOX(btn_row), save_btn);
    gtk_box_append(GTK_BOX(box), btn_row);

    gtk_window_set_child(GTK_WINDOW(dd->dialog), box);
    gtk_window_present(GTK_WINDOW(dd->dialog));
}

typedef struct {
    ApiManagerState *state;
    AsRouteCapability cap;
    GtkWidget *combo;
    GPtrArray *provider_ids;
} RouteChangeData;

static void on_route_combo_changed(GObject *gobject, GParamSpec *pspec, gpointer user_data)
{
    (void)gobject; (void)pspec;
    RouteChangeData *rcd = user_data;
    guint idx = gtk_drop_down_get_selected(GTK_DROP_DOWN(rcd->combo));
    if (idx >= rcd->provider_ids->len) return;

    const gchar *pid = g_ptr_array_index(rcd->provider_ids, idx);
    AsProviderConfig *cfg = as_provider_registry_find(rcd->state->ctx->providers, pid);
    const gchar *model = cfg ? cfg->text_model : NULL;

    GError *err = NULL;
    as_provider_registry_set_route(rcd->state->ctx->providers, rcd->cap, pid, model, &err);
    if (err) {
        as_ui_toast(rcd->state->root, err->message);
        g_error_free(err);
    } else {
        as_ui_toast(rcd->state->root, "Routing updated.");
    }
}

static void free_route_change_data(gpointer data)
{
    RouteChangeData *rcd = data;
    g_ptr_array_unref(rcd->provider_ids);
    g_free(rcd);
}

static void refresh_api_manager(ApiManagerState *state)
{
    /* 1. Refresh Provider List */
    GtkWidget *child = NULL;
    while ((child = gtk_widget_get_first_child(state->providers_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->providers_box), child);

    GPtrArray *provs = as_provider_registry_list(state->ctx->providers);
    if (!provs || provs->len == 0) {
        GtkWidget *card = as_ui_card();
        GtkWidget *lbl = as_ui_label("No AI API providers configured. Click '+ Add Provider' to configure your Gemini, OpenRouter, or local Ollama endpoints.", "section-subtitle");
        gtk_box_append(GTK_BOX(card), lbl);
        gtk_box_append(GTK_BOX(state->providers_box), card);
    } else {
        for (guint i = 0; i < provs->len; i++) {
            AsProviderConfig *p = g_ptr_array_index(provs, i);
            GtkWidget *card = as_ui_card();
            GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);

            GtkWidget *info = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
            gtk_widget_set_hexpand(info, TRUE);

            GtkWidget *hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *name_lbl = as_ui_label(p->name, "section-title");
            GtkWidget *type_badge = as_ui_label(as_provider_type_display(p->type), "badge-primary");
            gtk_box_append(GTK_BOX(hdr), name_lbl);
            gtk_box_append(GTK_BOX(hdr), type_badge);

            gchar *sub = g_strdup_printf("Base URL: %s  •  Model: %s", p->base_url, p->text_model ? p->text_model : "default");
            GtkWidget *sub_lbl = as_ui_label(sub, "section-subtitle");
            g_free(sub);

            gtk_box_append(GTK_BOX(info), hdr);
            gtk_box_append(GTK_BOX(info), sub_lbl);
            gtk_box_append(GTK_BOX(row), info);

            GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
            GtkWidget *test_btn = as_ui_button("Test Connection", "btn-secondary");
            GtkWidget *del_btn = as_ui_button("Remove", "btn-danger");

            ProviderActionData *pad = g_new0(ProviderActionData, 1);
            pad->state = state;
            pad->provider_id = g_strdup(p->id);

            g_signal_connect_data(test_btn, "clicked", G_CALLBACK(on_test_connection_clicked), pad, NULL, 0);
            g_signal_connect_data(del_btn, "clicked", G_CALLBACK(on_delete_provider_clicked), pad, (GClosureNotify)free_prov_data, 0);

            gtk_box_append(GTK_BOX(actions), test_btn);
            gtk_box_append(GTK_BOX(actions), del_btn);
            gtk_box_append(GTK_BOX(row), actions);

            gtk_box_append(GTK_BOX(card), row);
            gtk_box_append(GTK_BOX(state->providers_box), card);
        }
    }

    /* 2. Refresh Capability Routing Section */
    while ((child = gtk_widget_get_first_child(state->routing_box)) != NULL)
        gtk_box_remove(GTK_BOX(state->routing_box), child);

    GtkWidget *route_card = as_ui_card();
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(grid), 24);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 12);

    const AsRouteCapability caps[] = {
        AS_ROUTE_PLANNING,
        AS_ROUTE_STORYBOARD,
        AS_ROUTE_SPEECH,
        AS_ROUTE_IMAGE
    };

    GPtrArray *names = g_ptr_array_new_with_free_func(g_free);
    GPtrArray *pids = g_ptr_array_new_with_free_func(g_free);
    if (provs) {
        for (guint i = 0; i < provs->len; i++) {
            AsProviderConfig *p = g_ptr_array_index(provs, i);
            g_ptr_array_add(names, g_strdup(p->name));
            g_ptr_array_add(pids, g_strdup(p->id));
        }
    }
    g_ptr_array_add(names, NULL);

    for (gint r = 0; r < (gint)G_N_ELEMENTS(caps); r++) {
        GtkWidget *cap_lbl = as_ui_label(as_route_display_name(caps[r]), "section-subtitle");
        gtk_grid_attach(GTK_GRID(grid), cap_lbl, 0, r, 1, 1);

        if (provs && provs->len > 0) {
            GtkWidget *combo = gtk_drop_down_new_from_strings((const char *const *)names->pdata);
            AsCapabilityRoute *cur = as_provider_registry_get_route(state->ctx->providers, caps[r]);
            if (cur && cur->provider_id) {
                for (guint i = 0; i < pids->len; i++) {
                    if (g_strcmp0(g_ptr_array_index(pids, i), cur->provider_id) == 0) {
                        gtk_drop_down_set_selected(GTK_DROP_DOWN(combo), i);
                        break;
                    }
                }
            }
            RouteChangeData *rcd = g_new0(RouteChangeData, 1);
            rcd->state = state;
            rcd->cap = caps[r];
            rcd->combo = combo;
            rcd->provider_ids = g_ptr_array_ref(pids);
            g_signal_connect_data(combo, "notify::selected", G_CALLBACK(on_route_combo_changed), rcd, (GClosureNotify)free_route_change_data, 0);
            gtk_grid_attach(GTK_GRID(grid), combo, 1, r, 1, 1);
        } else {
            GtkWidget *no_prov = as_ui_label("No providers available", "badge-warning");
            gtk_grid_attach(GTK_GRID(grid), no_prov, 1, r, 1, 1);
        }
    }

    g_ptr_array_unref(names);
    g_ptr_array_unref(pids);

    gtk_box_append(GTK_BOX(route_card), grid);
    gtk_box_append(GTK_BOX(state->routing_box), route_card);
}

static void on_api_manager_refresh(gpointer page)
{
    ApiManagerState *state = g_object_get_data(G_OBJECT(page), "api-manager-state");
    if (state)
        refresh_api_manager(state);
}

GtkWidget *as_api_manager_new(AsAppContext *ctx, AsNavigator *nav)
{
    ApiManagerState *state = g_new0(ApiManagerState, 1);
    state->ctx = ctx;
    state->nav = nav;

    GtkWidget *main_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_start(main_box, 32);
    gtk_widget_set_margin_end(main_box, 32);
    gtk_widget_set_margin_top(main_box, 24);
    gtk_widget_set_margin_bottom(main_box, 32);

    /* Header & Add button */
    GtkWidget *hdr_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
    GtkWidget *title = as_ui_section_title("API Provider Manager", "Configure multiple AI APIs with independent capability routing");
    gtk_widget_set_hexpand(title, TRUE);
    GtkWidget *add_btn = as_ui_button("+ Add Provider", "btn-primary");
    g_signal_connect(add_btn, "clicked", G_CALLBACK(on_add_provider_clicked), state);

    gtk_box_append(GTK_BOX(hdr_row), title);
    gtk_box_append(GTK_BOX(hdr_row), add_btn);
    gtk_box_append(GTK_BOX(main_box), hdr_row);

    /* Provider List */
    GtkWidget *prov_hdr = as_ui_label("Configured Providers", "section-title");
    gtk_box_append(GTK_BOX(main_box), prov_hdr);
    state->providers_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(main_box), state->providers_box);

    /* Independent Capability Routing */
    GtkWidget *route_hdr = as_ui_label("Independent AI Capability Routing", "section-title");
    GtkWidget *route_desc = as_ui_label("Assign different configured providers to distinct creation tasks. Changes apply immediately to new jobs.", "section-subtitle");
    gtk_box_append(GTK_BOX(main_box), route_hdr);
    gtk_box_append(GTK_BOX(main_box), route_desc);
    state->routing_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_box_append(GTK_BOX(main_box), state->routing_box);

    GtkWidget *scrolled = as_ui_scrolled(main_box);
    state->root = scrolled;
    g_object_set_data_full(G_OBJECT(scrolled), "api-manager-state", state, g_free);
    as_page_set_refresh(scrolled, on_api_manager_refresh, state);

    refresh_api_manager(state);
    return scrolled;
}
