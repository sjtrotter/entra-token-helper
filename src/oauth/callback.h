/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_CALLBACK_H
#define ENTRA_OAUTH_CALLBACK_H

#include <glib.h>

#include "transaction.h"

typedef enum
{
	ENTRA_CALLBACK_CODE,
	ENTRA_CALLBACK_ERROR,
	ENTRA_CALLBACK_UNRELATED,
	ENTRA_CALLBACK_INVALID
} EntraCallbackResult;

/** The caller owns and must scrub @code; @oauth_error receives only the server error code. */
EntraCallbackResult entra_callback_classify(EntraTransaction* transaction, const char* url,
                                            char** code, char** oauth_error);

const char* entra_callback_result_str(EntraCallbackResult result);

#endif
