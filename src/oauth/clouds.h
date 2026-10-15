/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_CLOUDS_H
#define ENTRA_OAUTH_CLOUDS_H

#include <glib.h>

#include "../entra-config.h"

#define ENTRA_AVD_CLIENT_ID "a85cf173-4192-42f8-81fa-777a763e6e2c"
#define ENTRA_NATIVECLIENT_REDIRECT "https://login.microsoftonline.com/common/oauth2/nativeclient"

typedef struct
{
	const char* name;
	const char* authority;
	const char* avd_scope;
	const char* redirect;  /**< Both clouds require ENTRA_NATIVECLIENT_REDIRECT. */
} EntraCloud;

const EntraCloud* entra_cloud_for_authority(const char* authority);

const EntraCloud* entra_cloud_by_name(const char* name);

const EntraCloud* entra_cloud_nth(gsize index);

gboolean entra_cloud_client_id_allowed(const char* client_id, EntraConfig* config);

typedef struct
{
	char* host;
	char* tenant;
	char* base;   /**< Derived https://<host>/<tenant> base. */
} EntraAuthority;

/** Accept a bare host or https://<host>/<tenant>, with an optional separate @tenant. */
gboolean entra_authority_parse(const char* authority, const char* tenant, EntraAuthority* out,
                               GError** error);

/* Authorization and token requests must use the same authority allowlist. */
gboolean entra_authority_allowed(const EntraAuthority* self, EntraConfig* config);

void entra_authority_clear(EntraAuthority* self);

#endif
