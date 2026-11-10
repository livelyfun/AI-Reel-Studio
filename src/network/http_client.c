/* http_client.c — one bounded libcurl client for all provider traffic. */
#include "as_http.h"
#include "as_core.h"

#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib/gstdio.h>

struct _AsHttpClient {
    gsize max_body;
    gint timeout_ms;
    gboolean allow_insecure;
};

typedef struct {
    GByteArray *bytes;
    GByteArray *headers;
    gsize max;
    gboolean too_large;
    gchar *content_type;
    gchar *retry_after;
} ResponseBuffer;

typedef struct {
    gint fd;
    gsize written;
    gsize max;
    gboolean too_large;
} DownloadBuffer;

static void curl_global_setup(void)
{
    static gsize once = 0;
    if (g_once_init_enter(&once)) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        g_once_init_leave(&once, 1);
    }
}

AsHttpClient *as_http_client_new(gsize max_body, gint timeout_ms,
                                 gboolean allow_insecure)
{
    curl_global_setup();
    AsHttpClient *client = g_new0(AsHttpClient, 1);
    client->max_body = max_body > 0 ? max_body : 32 * 1024 * 1024;
    client->timeout_ms = timeout_ms > 0 ? timeout_ms : 60000;
    client->allow_insecure = allow_insecure;
    return client;
}

void as_http_client_free(AsHttpClient *client)
{
    g_free(client);
}

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ResponseBuffer *buffer = userdata;
    gsize bytes = size * nmemb;
    if (buffer->bytes->len + bytes > buffer->max) {
        buffer->too_large = TRUE;
        return 0;
    }
    g_byte_array_append(buffer->bytes, (const guint8 *) ptr, (guint) bytes);
    return bytes;
}

static size_t download_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    DownloadBuffer *buffer = userdata;
    gsize bytes = size * nmemb;
    if (buffer->written + bytes > buffer->max) {
        buffer->too_large = TRUE;
        return 0;
    }
    gsize offset = 0;
    while (offset < bytes) {
        ssize_t n = write(buffer->fd, ptr + offset, bytes - offset);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        offset += (gsize) n;
    }
    buffer->written += bytes;
    return bytes;
}

static gchar *trim_header_value(const gchar *value, gsize len)
{
    while (len && (*value == ' ' || *value == '\t')) {
        value++;
        len--;
    }
    while (len && (value[len - 1] == '\r' || value[len - 1] == '\n' ||
                   value[len - 1] == ' ' || value[len - 1] == '\t'))
        len--;
    return g_strndup(value, len);
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata)
{
    ResponseBuffer *response = userdata;
    gsize len = size * nitems;
    const gchar *line = buffer;
    if (len > 5 && g_ascii_strncasecmp(line, "HTTP/", 5) == 0) {
        /* New response (redirect): reset per-response metadata. */
        g_clear_pointer(&response->content_type, g_free);
        g_clear_pointer(&response->retry_after, g_free);
        return len;
    }
    if (len > 13 && g_ascii_strncasecmp(line, "content-type:", 13) == 0) {
        g_free(response->content_type);
        response->content_type = trim_header_value(line + 13, len - 13);
    } else if (len > 12 && g_ascii_strncasecmp(line, "retry-after:", 12) == 0) {
        g_free(response->retry_after);
        response->retry_after = trim_header_value(line + 12, len - 12);
    }
    return len;
}

static int progress_cb(void *clientp, curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow)
{
    (void) dltotal;
    (void) dlnow;
    (void) ultotal;
    (void) ulnow;
    GCancellable *cancellable = clientp;
    return cancellable && g_cancellable_is_cancelled(cancellable) ? 1 : 0;
}

static GError *transport_error(CURLcode code, const gchar *error_buffer,
                               gboolean too_large, GCancellable *cancellable)
{
    if (cancellable && g_cancellable_is_cancelled(cancellable))
        return as_error_new(AS_ERROR_CANCELLED, "The request was cancelled.");
    if (too_large)
        return as_error_new(AS_ERROR_TOO_LARGE,
                            "The server response exceeded the allowed size.");
    switch (code) {
    case CURLE_OPERATION_TIMEDOUT:
        return as_error_new(AS_ERROR_TIMEOUT, "The request timed out.");
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_CACERT_BADFILE:
        return as_error_new(AS_ERROR_TLS,
                            "TLS verification failed: %s",
                            error_buffer && *error_buffer ? error_buffer : "certificate error");
    case CURLE_COULDNT_RESOLVE_HOST:
        return as_error_new(AS_ERROR_NETWORK,
                            "Could not resolve the server host name.");
    case CURLE_COULDNT_CONNECT:
        return as_error_new(AS_ERROR_NETWORK, "Could not connect to the server.");
    case CURLE_WRITE_ERROR:
        return as_error_new(AS_ERROR_IO, "Could not write the response.");
    case CURLE_UNSUPPORTED_PROTOCOL:
        return as_error_new(AS_ERROR_UNSUPPORTED, "Unsupported URL protocol.");
    default:
        return as_error_new(AS_ERROR_NETWORK, "Network error: %s",
                            error_buffer && *error_buffer ? error_buffer : curl_easy_strerror(code));
    }
}

static gboolean prepare_easy(CURL *curl, const gchar *method, const gchar *url,
                             const gchar *const *headers, const gchar *body,
                             gsize body_len, AsHttpClient *client,
                             struct curl_slist **slist_out, GCancellable *cancellable,
                             char *error_buffer, GError **error)
{
    if (!as_url_validate(url, client->allow_insecure, error))
        return FALSE;

    struct curl_slist *slist = NULL;
    if (headers) {
        for (guint i = 0; headers[i]; i++)
            slist = curl_slist_append(slist, headers[i]);
    }
    *slist_out = slist;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "AIReelStudio/1.0 (native)");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long) client->timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS,
                     (long) MIN(client->timeout_ms, 10000));
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buffer);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancellable);
    if (slist)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);

    if (g_ascii_strcasecmp(method, "POST") == 0) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body ? body : "");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) body_len);
    } else if (g_ascii_strcasecmp(method, "GET") == 0) {
        curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    }
    return TRUE;
}

gboolean as_http_request(AsHttpClient *client, const gchar *method,
                         const gchar *url, const gchar *const *headers,
                         const gchar *body, gsize body_len,
                         GCancellable *cancellable, AsHttpResponse **response,
                         GError **error)
{
    g_return_val_if_fail(client != NULL, FALSE);
    g_return_val_if_fail(url != NULL, FALSE);
    g_return_val_if_fail(response != NULL, FALSE);

    CURL *curl = curl_easy_init();
    if (!curl) {
        g_set_error(error, AS_ERROR, AS_ERROR_UNKNOWN,
                    "Could not initialize the HTTP client.");
        return FALSE;
    }

    ResponseBuffer buffer = { 0 };
    buffer.bytes = g_byte_array_new();
    buffer.headers = g_byte_array_new();
    buffer.max = client->max_body;

    char error_buffer[CURL_ERROR_SIZE] = { 0 };
    struct curl_slist *slist = NULL;
    if (!prepare_easy(curl, method, url, headers, body, body_len, client, &slist,
                      cancellable, error_buffer, error)) {
        curl_easy_cleanup(curl);
        g_byte_array_unref(buffer.bytes);
        g_byte_array_unref(buffer.headers);
        return FALSE;
    }

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &buffer);

    CURLcode code = curl_easy_perform(curl);
    glong status = 0;
    if (code == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    char *effective = NULL;
    if (code == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);

    curl_slist_free_all(slist);
    curl_easy_cleanup(curl);

    if (code != CURLE_OK) {
        g_propagate_error(error, transport_error(code, error_buffer,
                                                 buffer.too_large, cancellable));
        g_byte_array_unref(buffer.bytes);
        g_byte_array_unref(buffer.headers);
        g_free(buffer.content_type);
        g_free(buffer.retry_after);
        return FALSE;
    }

    AsHttpResponse *result = g_new0(AsHttpResponse, 1);
    result->status = status;
    result->body = g_byte_array_free_to_bytes(buffer.bytes);
    result->content_type = buffer.content_type;
    result->retry_after = buffer.retry_after;
    result->effective_url = effective ? g_strdup(effective) : g_strdup(url);
    g_byte_array_unref(buffer.headers);
    *response = result;
    return TRUE;
}

gboolean as_http_get(AsHttpClient *client, const gchar *url,
                     const gchar *const *headers, GCancellable *cancellable,
                     AsHttpResponse **response, GError **error)
{
    return as_http_request(client, "GET", url, headers, NULL, 0, cancellable,
                           response, error);
}

gboolean as_http_post(AsHttpClient *client, const gchar *url,
                      const gchar *const *headers, const gchar *body,
                      gsize body_len, GCancellable *cancellable,
                      AsHttpResponse **response, GError **error)
{
    return as_http_request(client, "POST", url, headers, body, body_len,
                           cancellable, response, error);
}

gboolean as_http_download_to_file(AsHttpClient *client, const gchar *url,
                                  const gchar *const *headers,
                                  const gchar *dest_path, gsize max_bytes,
                                  gchar **content_type_out, gsize *size_out,
                                  GCancellable *cancellable, GError **error)
{
    g_return_val_if_fail(client != NULL, FALSE);
    g_return_val_if_fail(dest_path != NULL, FALSE);

    gchar *part_path = g_strconcat(dest_path, ".part", NULL);
    g_unlink(part_path);
    int fd = g_open(part_path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "Could not create download file '%s': %s", part_path,
                    g_strerror(errno));
        g_free(part_path);
        return FALSE;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        close(fd);
        g_unlink(part_path);
        g_free(part_path);
        g_set_error(error, AS_ERROR, AS_ERROR_UNKNOWN,
                    "Could not initialize the HTTP client.");
        return FALSE;
    }

    ResponseBuffer meta = { 0 };
    meta.headers = g_byte_array_new();
    meta.max = max_bytes;
    DownloadBuffer buffer = { .fd = fd, .written = 0, .max = max_bytes };

    char error_buffer[CURL_ERROR_SIZE] = { 0 };
    struct curl_slist *slist = NULL;
    if (!prepare_easy(curl, "GET", url, headers, NULL, 0, client, &slist,
                      cancellable, error_buffer, error)) {
        close(fd);
        g_unlink(part_path);
        g_free(part_path);
        g_byte_array_unref(meta.headers);
        curl_easy_cleanup(curl);
        return FALSE;
    }
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &meta);

    CURLcode code = curl_easy_perform(curl);
    glong status = 0;
    if (code == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(slist);
    curl_easy_cleanup(curl);

    if (fsync(fd) != 0) {
        /* Non-fatal: rename still provides atomic visibility. */
    }
    close(fd);

    gboolean ok = TRUE;
    if (code != CURLE_OK) {
        g_propagate_error(error, transport_error(code, error_buffer,
                                                 buffer.too_large, cancellable));
        ok = FALSE;
    } else if (status < 200 || status >= 300) {
        g_set_error(error, AS_ERROR, AS_ERROR_HTTP_STATUS,
                    "Download failed with HTTP %ld.", status);
        ok = FALSE;
    } else if (buffer.written == 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "The downloaded file is empty.");
        ok = FALSE;
    }

    if (!ok) {
        g_unlink(part_path);
        g_free(part_path);
        g_clear_pointer(&meta.content_type, g_free);
        g_clear_pointer(&meta.retry_after, g_free);
        g_byte_array_unref(meta.headers);
        return FALSE;
    }

    if (g_rename(part_path, dest_path) != 0) {
        g_set_error(error, AS_ERROR, AS_ERROR_IO,
                    "Could not finalize download '%s': %s", dest_path,
                    g_strerror(errno));
        g_unlink(part_path);
        g_free(part_path);
        g_clear_pointer(&meta.content_type, g_free);
        g_clear_pointer(&meta.retry_after, g_free);
        g_byte_array_unref(meta.headers);
        return FALSE;
    }

    g_free(part_path);
    if (content_type_out)
        *content_type_out = g_strdup(meta.content_type);
    if (size_out)
        *size_out = buffer.written;
    g_clear_pointer(&meta.content_type, g_free);
    g_clear_pointer(&meta.retry_after, g_free);
    g_byte_array_unref(meta.headers);
    return TRUE;
}

void as_http_response_free(AsHttpResponse *response)
{
    if (!response)
        return;
    g_clear_pointer(&response->body, g_bytes_unref);
    g_free(response->content_type);
    g_free(response->retry_after);
    g_free(response->effective_url);
    g_free(response);
}

gchar *as_http_retry_after_to_seconds(const gchar *value)
{
    if (!value || !*value)
        return NULL;
    gchar *end = NULL;
    gint64 numeric = g_ascii_strtoll(value, &end, 10);
    if (end && *end == '\0' && numeric >= 0)
        return g_strdup_printf("%" G_GINT64_FORMAT, MIN(numeric, (gint64) 3600));
    time_t parsed = curl_getdate(value, NULL);
    if (parsed < 0)
        return NULL;
    gint64 now = (gint64) time(NULL);
    gint64 delta = (gint64) parsed - now;
    if (delta < 0)
        delta = 0;
    if (delta > 3600)
        delta = 3600;
    return g_strdup_printf("%" G_GINT64_FORMAT, delta);
}

gboolean as_http_status_is_retryable(glong status)
{
    switch (status) {
    case 408:
    case 425:
    case 429:
    case 500:
    case 502:
    case 503:
    case 504:
        return TRUE;
    default:
        return FALSE;
    }
}
