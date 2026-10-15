/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_CONFIG_H
#define ENTRA_CONFIG_H

#include <glib.h>

typedef struct _EntraConfig EntraConfig;

/** Load @path, ENTRA_TOKEN_HELPER_CONFIG, or the XDG config path; a missing file yields empty config. */
EntraConfig* entra_config_load(const char* path, GError** error);

/* Only explicit config file entries extend allowlists; wildcards are unsupported. */
gboolean entra_config_allows_authority(EntraConfig* self, const char* host);
gboolean entra_config_allows_client_id(EntraConfig* self, const char* client_id);

/** The configured PEM trust store, or NULL to use the system store. */
const char* entra_config_trust_certificate(EntraConfig* self);

void entra_config_free(EntraConfig* self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EntraConfig, entra_config_free)

#endif
