/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_DISCOVERY_H
#define ENTRA_OAUTH_DISCOVERY_H

#include <gio/gio.h>
#include <glib.h>

#include "clouds.h"
#include "http.h"

typedef struct
{
	char* authorization_endpoint;
	char* token_endpoint;
} EntraEndpoints;

/** Derive endpoints locally from the parsed authority without I/O. */
gboolean entra_endpoints_derive(const EntraAuthority* authority, EntraEndpoints* out,
                                GError** error);

/** Returns <base>/v2.0/.well-known/openid-configuration. */
char* entra_discovery_url(const EntraAuthority* authority);

gboolean entra_endpoints_parse_document(const char* document, gsize length,
                                        const char* expected_host, EntraEndpoints* out,
                                        GError** error);

/** Discovery failure leaves the derived endpoints intact and sets @error. */
gboolean entra_endpoints_discover(EntraHttp* http, const EntraAuthority* authority,
                                  EntraEndpoints* out, GCancellable* cancellable, GError** error);

/** @prompt may be NULL, "select_account", "login" or "consent". */
char* entra_build_authorize_url(const EntraEndpoints* endpoints, const char* client_id,
                                const char* scope, const char* state, const char* challenge,
                                const char* redirect_uri, const char* prompt,
                                const char* login_hint);

void entra_endpoints_clear(EntraEndpoints* endpoints);

#endif
