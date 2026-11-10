/* main.c — AI Reel Studio native Linux application entry point. */
#define _GNU_SOURCE
#include <gtk/gtk.h>
#include <gst/gst.h>

#include "as_app.h"
#include "as_ui.h"
#include "as_core.h"

int main(int argc, char **argv)
{
    /* Initialize GStreamer multimedia framework */
    gst_init(&argc, &argv);

    GError *error = NULL;
    AsAppContext *ctx = as_app_context_new(&error);
    if (!ctx) {
        g_printerr("Fatal: Failed to initialize application context: %s\n",
                   error ? error->message : "Unknown error");
        if (error)
            g_error_free(error);
        return 1;
    }

    GtkApplication *app = as_application_new(ctx);
    int status = g_application_run(G_APPLICATION(app), argc, argv);

    g_object_unref(app);
    as_app_context_free(ctx);

    return status;
}

