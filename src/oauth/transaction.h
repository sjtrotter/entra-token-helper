/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_OAUTH_TRANSACTION_H
#define ENTRA_OAUTH_TRANSACTION_H

#include <glib.h>

typedef struct EntraTransaction EntraTransaction;

/** S256 is unpadded base64url(sha256(verifier)), per RFC 7636. */
char* entra_pkce_challenge_for_verifier(const char* verifier);

/* The acquisition owns the transaction and its fresh random state and PKCE verifier. */
EntraTransaction* entra_transaction_new(const char* redirect_uri, guint timeout_seconds,
                                        GError** error);

const char* entra_transaction_state(EntraTransaction* self);
const char* entra_transaction_challenge(EntraTransaction* self);
const char* entra_transaction_verifier(EntraTransaction* self);  /**< The verifier is sent only to the token endpoint. */
const char* entra_transaction_redirect_uri(EntraTransaction* self);
guint entra_transaction_timeout(EntraTransaction* self);

/** State comparison must check all bytes and length differences without an early return. */
gboolean entra_transaction_state_equal(EntraTransaction* self, const char* candidate);

/** Returns FALSE for an already consumed transaction to reject replay. */
gboolean entra_transaction_consume(EntraTransaction* self);

/** Scrub state and verifier before freeing the transaction. */
void entra_transaction_free(EntraTransaction* self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(EntraTransaction, entra_transaction_free)

#endif
