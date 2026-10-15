/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_ERROR_H
#define ENTRA_ERROR_H

#include <glib.h>

/* Error messages must exclude credentials and server error_description; see docs/SECURITY.md. */

#define ENTRA_ERROR (entra_error_quark())

typedef enum
{
	ENTRA_ERROR_INTERACTION_REQUIRED, /**< Exit 10. */
	ENTRA_ERROR_CANCELLED,            /**< Exit 20. */
	ENTRA_ERROR_NO_ACCOUNT,           /**< Exit 30. */
	ENTRA_ERROR_UNAVAILABLE,          /**< Exit 40. */
	ENTRA_ERROR_SERVER,               /**< Exit 50. */
	ENTRA_ERROR_USAGE,                /**< Exit 64. */
	ENTRA_ERROR_INTERNAL              /**< Exit 70. */
} EntraError;

GQuark entra_error_quark(void);

#endif
