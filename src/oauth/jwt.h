/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_JWT_H
#define ENTRA_OAUTH_JWT_H

#include <glib.h>

/** Returns the decoded payload of a three-segment token, or NULL on decoding failure. */
char* entra_jwt_payload(const char* jwt);

/* Claims label accounts only; signature verification and authorization are outside this parser. */
char* entra_jwt_claim(const char* jwt, const char* claim);

/** Account label claim order: preferred_username, upn, unique_name, email, sub. */
char* entra_jwt_account_name(const char* id_token);

#endif
