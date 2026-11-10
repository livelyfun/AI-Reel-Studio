/* as_http.h — single libcurl-based HTTP layer used by every provider. */
#pragma once

#include <glib.h>
#include <gio/gio.h>

typedef struct _AsHttpClient AsHttpClient;

typedef struct {
    glong status;            /* HTTP status, 0 for transport failure */
    GBytes *body;            /* owned */
    gchar *content_type;     /* owned, may be NULL */
    gchar *retry_after;      /* owned, header value if supplied */
    gchar *effective_url;    /* owned */
} AsHttpResponse;

/* max_body caps the response body. allow_insecure permits non-loopback HTTP. */
AsHttpClient *as_http_client_new(gsize max_body, gint timeout_ms,
                                 gboolean allow_insecure);
void as_http_client_free(AsHttpClient *client);

/* headers: NULL-terminated array of "Name: value". body may be NULL. */
gboolean as_http_request(AsHttpClient *client, const gchar *method,
                         const gchar *url, const gchar *const *headers,
                         const gchar *body, gsize body_len,
                         GCancellable *cancellable,
                         AsHttpResponse **response, GError **error);

gboolean as_http_get(AsHttpClient *client, const gchar *url,
                     const gchar *const *headers, GCancellable *cancellable,
                     AsHttpResponse **response, GError **error);

gboolean as_http_post(AsHttpClient *client, const gchar *url,
                      const gchar *const *headers, const gchar *body,
                      gsize body_len, GCancellable *cancellable,
                      AsHttpResponse **response, GError **error);

/* Streams a download to dest_path, enforcing max_bytes and image/file type
 * verification by the caller. Writes atomically through dest_path.part. */
gboolean as_http_download_to_file(AsHttpClient *client, const gchar *url,
                                  const gchar *const *headers,
                                  const gchar *dest_path, gsize max_bytes,
                                  gchar **content_type_out, gsize *size_out,
                                  GCancellable *cancellable, GError **error);

void as_http_response_free(AsHttpResponse *response);

/* Small helpers shared by provider adapters. */
gchar *as_http_retry_after_to_seconds(const gchar *value);
gboolean as_http_status_is_retryable(glong status);
