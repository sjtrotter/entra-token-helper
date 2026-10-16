/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_TOKEN_H
#define ENTRA_OAUTH_TOKEN_H

#include <gio/gio.h>
#include <glib.h>

#include "http.h"

typedef struct
{
	char* access_token;
	char* refresh_token; /**< PoP responses omit the refresh token. */
	char* id_token;
	char* token_type;
	char* scope;
	gint64 expires_in;
	gint64 expires_at; /**< Wall clock seconds. */
} EntraTokenSet;

/** OAuth error codes and suberrors classify @error; error_description is excluded. */
gboolean entra_token_set_from_json(const char* document, gsize length, EntraTokenSet* out,
                                   char** oauth_error, GError** error);

/** @req_cnf may be NULL for a bearer token. */
gboolean entra_token_by_code(EntraHttp* http, const char* token_endpoint, const char* client_id,
                             const char* code, const char* redirect_uri, const char* verifier,
                             const char* scope, const char* req_cnf, EntraTokenSet* out,
                             char** oauth_error, GCancellable* cancellable, GError** error);

/** @req_cnf may be NULL for a bearer token. */
gboolean entra_token_by_refresh(EntraHttp* http, const char* token_endpoint, const char* client_id,
                                const char* refresh_token, const char* scope, const char* req_cnf,
                                EntraTokenSet* out, char** oauth_error, GCancellable* cancellable,
                                GError** error);

void entra_token_set_clear(EntraTokenSet* set); /**< Scrubs every token. */

#endif
