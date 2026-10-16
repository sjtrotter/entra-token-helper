/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_ACQUIRE_H
#define ENTRA_ACQUIRE_H

#include <glib.h>

#include "cache/keyring.h"
#include "entra-config.h"
#include "oauth/clouds.h"
#include "oauth/discovery.h"
#include "oauth/http.h"

typedef enum
{
	ENTRA_PROMPT_AUTO,
	ENTRA_PROMPT_ALWAYS,
	ENTRA_PROMPT_NEVER
} EntraPrompt;

typedef struct
{
	EntraAuthority authority;
	const char* client_id;
	GStrv scopes;       /**< Scopes retain the caller's order. */
	char* scope_request;
	char* scope_key;     /**< Sorted, deduplicated scopes. */
	const char* account;
	const char* req_cnf;
	const char* parent_window;
	const char* session_mode;
	EntraPrompt prompt;
	guint timeout_seconds;

	EntraConfig* config;
	EntraHttp* http;
	EntraEndpoints endpoints;
	gboolean discovered;
	const char* redirect;
} EntraAcquire;

typedef struct
{
	char* access_token;
	char* token_type;
	gint64 expires_in;
	char* scope;
	char* account;
} EntraTokenResult;

void entra_token_result_clear(EntraTokenResult* result);

/** Preparation derives endpoints and fixes the redirect URI without network access; callers cannot override these URLs. */
gboolean entra_acquire_prepare(EntraAcquire* self, GError** error);

void entra_acquire_clear(EntraAcquire* self);

/** @account_out receives an owned ID-token account label, or unknown@<authority-host> when no usable name is present. */
gboolean entra_acquire_login(EntraAcquire* self, char** account_out, GError** error);

gboolean entra_acquire_token(EntraAcquire* self, EntraTokenResult* result, GError** error);

#endif
