/* provider_adapters.h — internal adapter entry points.
 *
 * provider_registry.c dispatches the public as_provider_* API based on the
 * configured provider type. Adapters are intentionally not auto-fallback:
 * a failure in one provider never invokes another.
 */
#pragma once

#include "as_provider.h"

gboolean as_gemini_generate_text(const AsProviderConfig *cfg, const gchar *api_key,
                                 const gchar *model, const gchar *system_prompt,
                                 const gchar *user_prompt, gboolean structured,
                                 gdouble temperature, AsTextResult *result,
                                 GCancellable *cancellable, GError **error);
gboolean as_gemini_generate_speech(const AsProviderConfig *cfg,
                                   const gchar *api_key, const gchar *model,
                                   const gchar *voice, const gchar *text,
                                   AsSpeechResult *result,
                                   GCancellable *cancellable, GError **error);
gboolean as_gemini_generate_image(const AsProviderConfig *cfg,
                                  const gchar *api_key, const gchar *model,
                                  const gchar *prompt, const gchar *size,
                                  AsImageResult *result,
                                  GCancellable *cancellable, GError **error);
GPtrArray *as_gemini_list_models(const AsProviderConfig *cfg,
                                 const gchar *api_key,
                                 GCancellable *cancellable, GError **error);
gboolean as_gemini_test_connection(const AsProviderConfig *cfg,
                                   const gchar *api_key, gchar **message,
                                   GCancellable *cancellable, GError **error);

/* Raw-body helpers shared by binary speech/image endpoints. */
GBytes *as_provider_http_post_bytes(const AsProviderConfig *cfg,
                                    const gchar *api_key, const gchar *url,
                                    const gchar *body, const gchar *accept,
                                    gchar **content_type,
                                    GCancellable *cancellable, GError **error);
GBytes *as_provider_http_get_bytes(const AsProviderConfig *cfg,
                                   const gchar *api_key, const gchar *url,
                                   const gchar *accept, gchar **content_type,
                                   GCancellable *cancellable, GError **error);

gboolean as_openrouter_generate_text(const AsProviderConfig *cfg,
                                     const gchar *api_key, const gchar *model,
                                     const gchar *system_prompt,
                                     const gchar *user_prompt, gboolean structured,
                                     gdouble temperature, AsTextResult *result,
                                     GCancellable *cancellable, GError **error);
GPtrArray *as_openrouter_list_models(const AsProviderConfig *cfg,
                                     const gchar *api_key,
                                     GCancellable *cancellable, GError **error);
gboolean as_openrouter_test_connection(const AsProviderConfig *cfg,
                                       const gchar *api_key, gchar **message,
                                       GCancellable *cancellable, GError **error);

gboolean as_openai_generate_text(const AsProviderConfig *cfg,
                                 const gchar *api_key, const gchar *model,
                                 const gchar *system_prompt,
                                 const gchar *user_prompt, gboolean structured,
                                 gdouble temperature, AsTextResult *result,
                                 GCancellable *cancellable, GError **error);
gboolean as_openai_generate_speech(const AsProviderConfig *cfg,
                                   const gchar *api_key, const gchar *model,
                                   const gchar *voice, const gchar *text,
                                   AsSpeechResult *result,
                                   GCancellable *cancellable, GError **error);
gboolean as_openai_generate_image(const AsProviderConfig *cfg,
                                  const gchar *api_key, const gchar *model,
                                  const gchar *prompt, const gchar *size,
                                  AsImageResult *result,
                                  GCancellable *cancellable, GError **error);
GPtrArray *as_openai_list_models(const AsProviderConfig *cfg,
                                 const gchar *api_key,
                                 GCancellable *cancellable, GError **error);
gboolean as_openai_test_connection(const AsProviderConfig *cfg,
                                   const gchar *api_key, gchar **message,
                                   GCancellable *cancellable, GError **error);
