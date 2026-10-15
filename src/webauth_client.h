/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_WEBAUTH_CLIENT_H
#define ENTRA_WEBAUTH_CLIENT_H

#include <gio/gio.h>
#include <glib.h>

#define ENTRA_PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
#define ENTRA_PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop"
#define ENTRA_PORTAL_INTERFACE "org.freedesktop.portal.experimental.WebAuthentication1"
#define ENTRA_PORTAL_REQUEST_INTERFACE "org.freedesktop.portal.Request"

typedef enum
{
	ENTRA_WEBAUTH_COMPLETED = 0,
	ENTRA_WEBAUTH_CANCELLED = 1,  /**< The user closed the window or the request was closed. */
	ENTRA_WEBAUTH_OTHER = 2,      /**< Timeout or another transaction failure. */
	ENTRA_WEBAUTH_UNAVAILABLE     /**< Exit 40 when the portal interface is unavailable. */
} EntraWebAuthResult;

gboolean entra_webauth_available(GError** error);

/** Subscribe before Start to avoid a fast Response racing the subscription. */
EntraWebAuthResult entra_webauth_start(const char* parent_window, const char* start_uri,
                                       const char* completion_uri, const char* session_mode,
                                       const char* title, guint timeout_seconds,
                                       GCancellable* cancellable, char** completion_uri_out,
                                       char** reason_out, GError** error);

const char* entra_webauth_result_str(EntraWebAuthResult result);

#endif
