/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_IPC_REQUEST_H
#define ENTRA_IPC_REQUEST_H

#include <glib.h>

#include "../entra-error.h"

#define ENTRA_SCHEMA_VERSION 1

typedef enum
{
	ENTRA_STATUS_OK,
	ENTRA_STATUS_INTERACTION_REQUIRED,
	ENTRA_STATUS_CANCELLED,
	ENTRA_STATUS_NO_ACCOUNT,
	ENTRA_STATUS_UNAVAILABLE,
	ENTRA_STATUS_SERVER_ERROR,
	ENTRA_STATUS_USAGE,
	ENTRA_STATUS_INTERNAL
} EntraStatus;

int entra_status_exit_code(EntraStatus status);

const char* entra_status_symbol(EntraStatus status);

/** Errors outside the client domain map to internal status. */
EntraStatus entra_status_from_error(const GError* error);

char* entra_response_token_json(const char* token, const char* token_type, gint64 expires_in,
                                const char* scope, const char* account);

/** Error responses must exclude credentials and server error_description. */
char* entra_response_error_json(EntraStatus status, const char* error_symbol, const char* message);

/** @records contains EntraAccountRecord entries. */
char* entra_response_accounts_json(GPtrArray* records);

#endif
