# 4. Merged experimental portal convention

Date: 2026-10-15
Status: accepted

## Context

xdg-desktop-portal merged experimental portal support in PR #2129, corrected by
#2173. The integration frontend follows this convention.

## Decision

Call `org.freedesktop.portal.experimental.WebAuthentication1` on
`/org/freedesktop/portal/desktop`. The backend interface is
`org.freedesktop.impl.portal.experimental.WebAuthentication1`.
The certificate interfaces use `org.freedesktop.portal.experimental.Certificate1`
and `org.freedesktop.impl.portal.experimental.Certificate1`.

Start the frontend with `XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` containing
`WebAuthentication1` and `Certificate1`, comma separated without spaces, and
configure the corresponding backends. Append only missing names to an existing value.

Breaking changes increment the trailing interface number; non-breaking changes
increment the `version` property.

## Consequences

The main portal object uses the standard path. Request and Session handles use
`/request/...` and `/session/...` under that path. An unavailable web authentication interface
produces exit `40`; its diagnostic names the interface and the required frontend opt-in and backend.

This supersedes ADR 0003's web authentication interface name.
