/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_LOG_REDACT_H
#define ENTRA_LOG_REDACT_H

#include <glib.h>

/* Redaction applies at every log level; see docs/SECURITY.md. */

typedef enum
{
	ENTRA_FIELD_OUTCOME,
	ENTRA_FIELD_AUTHORITY,
	ENTRA_FIELD_TENANT,     /**< Hashed because a tenant ID identifies an organisation. */
	ENTRA_FIELD_ERROR_CODE,
	ENTRA_FIELD_COUNT,
	ENTRA_FIELD_ACCOUNT,    /**< Redacted. */
	ENTRA_FIELD_CODE,       /**< Redacted. */
	ENTRA_FIELD_TOKEN,      /**< Redacted. */
	ENTRA_FIELD_DESCRIPTION /**< Redacted because error_description can identify accounts and policies. */
} EntraFieldKind;

/* Event names are stable and translation independent. */
#define ENTRA_EVENT_DISCOVERY "discovery"
#define ENTRA_EVENT_AUTHORIZE "authorize"
#define ENTRA_EVENT_PORTAL "portal"
#define ENTRA_EVENT_CALLBACK "callback"
#define ENTRA_EVENT_TOKEN_REQUEST "token-request"
#define ENTRA_EVENT_TOKEN_RESPONSE "token-response"
#define ENTRA_EVENT_CACHE "cache"
#define ENTRA_EVENT_KEYRING "keyring"
#define ENTRA_EVENT_RESULT "result"

/* Authorization state and PKCE verifiers are credentials and must never be logged. */
/** Varargs are (name, EntraFieldKind, value) triples terminated by a NULL name. */
void entra_log_event(GLogLevelFlags level, const char* event, ...) G_GNUC_NULL_TERMINATED;

char* entra_redact_field(EntraFieldKind kind, const char* value);

/** Cut transport and TLS error text before a URI or query marker. */
char* entra_redact_error_text(const char* message);

/** Overwrite and free credential material. */
void entra_scrub(char* secret);

void entra_log_init(void);
void entra_log_set_verbose(gboolean verbose);
gboolean entra_log_get_verbose(void);

#endif
