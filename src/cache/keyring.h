/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_CACHE_KEYRING_H
#define ENTRA_CACHE_KEYRING_H

#include <glib.h>

#define ENTRA_KEYRING_SCHEMA_NAME "io.github.sjtrotter.entra-token-helper"

/* The expiry margin allows time to establish the connection. */
#define ENTRA_TOKEN_EXPIRY_MARGIN_SECONDS 300

typedef struct
{
	char* access_token;
	char* token_type;
	char* scope;
	gint64 expires_at; /**< Wall clock seconds. */
} EntraCachedToken;

typedef struct
{
	char* account;
	char* authority; /**< Full https://<host>/<tenant> base isolates tenants. */
	char* host;
	char* tenant;
	char* client_id;
	char* refresh_token;
	char* id_token;
	GHashTable* tokens; /**< Scope key to owned EntraCachedToken. */
} EntraAccountRecord;

/** Scope keys are sorted, deduplicated and space joined; scope order is insignificant. */
char* entra_cache_scope_key(const char* const* scopes);

GStrv entra_scopes_split(const char* const* scopes);

EntraAccountRecord* entra_account_record_new(void);
void entra_account_record_free(EntraAccountRecord* record);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(EntraAccountRecord, entra_account_record_free)

/** Returns a borrowed token, or NULL if missing or within the expiry margin. */
const EntraCachedToken* entra_account_record_lookup(EntraAccountRecord* record,
                                                    const char* scope_key);

void entra_account_record_store_token(EntraAccountRecord* record, const char* scope_key,
                                      const char* access_token, const char* token_type,
                                      const char* scope, gint64 expires_at);

/** This JSON contains credentials and belongs only in the keyring secret. */
char* entra_account_record_to_json(const EntraAccountRecord* record);
EntraAccountRecord* entra_account_record_from_json(const char* json, GError** error);

gboolean entra_keyring_available(GError** error);

gboolean entra_keyring_store(const EntraAccountRecord* record, GError** error);

/** With no @account, exactly one record must match @authority and @client_id. */
EntraAccountRecord* entra_keyring_load(const char* authority, const char* client_id,
                                       const char* account, GError** error);

GPtrArray* entra_keyring_list(GError** error);

/** A NULL field matches every value; @removed receives the number of deleted items. */
gboolean entra_keyring_forget(const char* authority, const char* client_id, const char* account,
                              guint* removed, GError** error);

#endif
