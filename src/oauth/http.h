/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_HTTP_H
#define ENTRA_OAUTH_HTTP_H

#include <gio/gio.h>
#include <glib.h>

#include "../entra-config.h"

typedef struct _EntraHttp EntraHttp;

EntraHttp* entra_http_new(EntraConfig* config, GError** error);

/** GET returns the body on 2xx; other status codes set @error. */
GBytes* entra_http_get(EntraHttp* self, const char* url, GCancellable* cancellable,
                       GError** error);

/** POST returns the body for every status so OAuth errors can be classified; @status_out receives the code. */
GBytes* entra_http_post_form(EntraHttp* self, const char* url, const char* body, guint* status_out,
                             GCancellable* cancellable, GError** error);

void entra_http_free(EntraHttp* self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EntraHttp, entra_http_free)

#endif
