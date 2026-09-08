/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */
#ifndef ENTRA_WEBAUTH_CLIENT_H
#define ENTRA_WEBAUTH_CLIENT_H

#include <gio/gio.h>
#include <glib.h>

/** @file
 *  Calling the web authentication portal: the client's only interactive
 *  dependency.
 *
 *      bus name      org.freedesktop.portal.Desktop
 *      object path   /org/freedesktop/portal/desktop/experimental
 *      interface     org.freedesktop.portal.WebAuthentication.X1
 *      method        Start(s parent_window, s start_uri, s completion_uri,
 *                          a{sv} options) -> o request_handle
 *      result        org.freedesktop.portal.Request::Response(u, a{sv})
 *      request path  /org/freedesktop/portal/desktop/request/<sender>/<token>
 *
 *  Everything interactive this client does is one Start call and one Response.
 *  It hands over a URI to open and the exact URI whose navigation ends the flow,
 *  and gets back the URI the flow ended at. The portal knows nothing about
 *  OAuth, and this client owns no windows, no web view, no certificate chooser
 *  and no PIN prompt.
 *
 *  IT TALKS TO xdg-desktop-portal AND TO NOTHING ELSE. The portal routes to a
 *  backend, and none of that is visible here: this client never names a backend,
 *  never reads a .portal file, never calls org.freedesktop.impl.portal.* - which
 *  it could not be permitted to do anyway - and cannot tell which backend served
 *  it. A machine that installs a different backend changes nothing in this file.
 *
 *  THE INTERFACE IS EXPERIMENTAL, AND ITS ABSENCE IS THE COMMON FAILURE.
 *  org.freedesktop.portal.WebAuthentication.X1 lives on the experimental object
 *  path and is not exported unless xdg-desktop-portal found a backend configured
 *  for it. With none configured the interface is absent from introspection
 *  entirely - Properties.Get on it fails, and Start() gets UnknownMethod. That is
 *  not an error to report as a failure: it is ENTRA_WEBAUTH_UNAVAILABLE, and the
 *  CLI maps it to exit 40.
 *
 *  The same "absent interface" also means: no session bus, nothing owning
 *  org.freedesktop.portal.Desktop, or a portal with no backend configured for the
 *  interface - the frontend returns early when it finds no impl, so it exports
 *  nothing. Those cases are indistinguishable from each other, by design, and all
 *  of them are "unavailable" so that a dispatcher can fall through to another
 *  provider such as FreeRDP's terminal paste flow.
 *
 *  The completion URI this client passes is always the one from its own cloud
 *  table. It is never taken from the client's caller. See
 *  docs/PUBLIC-INTERFACE.md.
 *
 *  tools/webauth-e2e.py makes exactly this call in python and is what the
 *  backend's end-to-end tests drive the portal with; this is the same call in C.
 */

#define ENTRA_PORTAL_BUS_NAME "org.freedesktop.portal.Desktop"
/** Experimental portals are exported here, not on /org/freedesktop/portal/desktop. */
#define ENTRA_PORTAL_OBJECT_PATH "/org/freedesktop/portal/desktop/experimental"
#define ENTRA_PORTAL_INTERFACE "org.freedesktop.portal.WebAuthentication.X1"
#define ENTRA_PORTAL_REQUEST_INTERFACE "org.freedesktop.portal.Request"

typedef enum
{
	ENTRA_WEBAUTH_COMPLETED = 0,  /**< @completion_uri is set */
	ENTRA_WEBAUTH_CANCELLED = 1,  /**< the user closed the window, or we called Close() */
	ENTRA_WEBAUTH_OTHER = 2,      /**< timeout, or the transaction ended some other way */
	ENTRA_WEBAUTH_UNAVAILABLE     /**< the interface is not exported: no bus, no portal,
	                                *  or no backend configured for the interface.
	                                *  The CLI maps this to exit 40. */
} EntraWebAuthResult;

/** Whether the interface is exported, without starting a transaction. Checked by
 *  reading its `version` property, which fails cleanly when it is absent. */
gboolean entra_webauth_available(GError** error);

/** Run one interactive transaction and return the completion URI. Subscribes to
 *  Response on the handle derived from a fresh handle_token BEFORE calling Start,
 *  so a fast completion cannot race the subscription. Blocks until the portal
 *  responds, the timeout expires, or @cancellable fires - in which case it calls
 *  Close() and waits for the response it is still owed. */
EntraWebAuthResult entra_webauth_start(const char* parent_window, const char* start_uri,
                                       const char* completion_uri, const char* session_mode,
                                       const char* title, guint timeout_seconds,
                                       GCancellable* cancellable, char** completion_uri_out,
                                       char** reason_out, GError** error);

/** The stable symbol for @result, for logs. */
const char* entra_webauth_result_str(EntraWebAuthResult result);

#endif /* ENTRA_WEBAUTH_CLIENT_H */
