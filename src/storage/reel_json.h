/* reel_json.h — internal JSON document codec for AsReel. */
#pragma once

#include <glib.h>
#include "as_types.h"

#define AS_REEL_JSON_VERSION 1

gchar *as_reel_to_json(const AsReel *reel);
AsReel *as_reel_from_json(const gchar *json, GError **error);
