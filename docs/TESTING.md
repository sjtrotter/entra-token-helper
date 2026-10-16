# Testing

The unit suite runs without a display, session bus or network. End-to-end tests use a private bus,
a headless X server and a mock authority. Live runs require a real desktop session, tenant and card.
See [README.md](../README.md) for project status.

## Unit tests

Build dependencies are GLib, GObject and GIO 2.76 or later, libsoup-3 3.0 or later, json-glib 1.6
or later, and libsecret 0.20 or later. Meson requires version 0.60.0 or later; Ninja builds the C11
project.

`meson.build` declares GLib, GObject and GIO 2.66, but `src/cache/keyring.c` uses
`g_ptr_array_sort_values`, available since GLib 2.76. Meson's minimum is underdeclared;
[issue #4](https://github.com/sjtrotter/entra-token-helper/issues/4) tracks the discrepancy.

```console
$ meson setup build && ninja -C build && meson test -C build
```

`tests/meson.build` registers 68 cases across six executables in the `unit` suite with TAP output:

| Test | Coverage |
|---|---|
| `pkce` | RFC 7636 `S256` vector, unpadded base64url, fresh verifiers, exact `state` comparison, single-use transactions |
| `callback` | Exact redirect matching, userinfo and fragment rejection, unique `code` or `error`, valueless parameters, strict percent-decoding |
| `jwt` | Account-claim selection, malformed token rejection, control-character rejection |
| `discovery` | Cloud table, client-id allowlist, authority parsing, derived endpoints, discovery refinement, HTTPS and host restrictions, authorization URLs, token responses and error classification |
| `cache` | Sorted and deduplicated scope keys, equivalent scope spellings, five-minute expiry margin, JSON round trips, exit codes |
| `redact` | Credential and description redaction, tenant hashing, unchanged redaction with verbose logging |

## End-to-end tests

The end-to-end script runs separately from Meson. It requires Python 3, OpenSSL, `dbus-run-session`,
Xvfb, the built client and webauth backend, and a built frontend from
[`experimental/integration`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration).
The frontend build must contain `desktop-portal/xdg-desktop-portal` and
`document-portal/xdg-permission-store`.

```console
$ WEBAUTH_REPO=../xdg-desktop-portal-webauth XDP_BUILD=../xdg-desktop-portal/build \
    tools/entra-e2e.sh
```

| Variable | Default | Meaning |
|---|---|---|
| `XDP_BUILD` | `$REPO/../xdg-desktop-portal/build` | Frontend build |
| `WEBAUTH_REPO` | `$REPO/../xdg-desktop-portal-webauth` | Webauth backend checkout |
| `BACKEND` | `$WEBAUTH_REPO/build/src/xdg-desktop-portal-webauth` | Built backend |
| `HELPER` | `$REPO/build/entra-token-helper` | Client under test |
| `XDP_ENV` | `$REPO/.xdp-env` | Shell file sourced if present for frontend library paths |
| `XVFB` | `Xvfb` from `PATH` | Headless X server |

`REPO` is the script's repository root. `--keep` leaves the stack running and prints the generated
configuration path and authority. The script sets `G_MESSAGES_DEBUG="webauth entra"`;
`--verbose` enables client debug output, subject to the [stdout limitation](CLI.md#output).
`--help` prints usage.

The script starts `xdg-permission-store`, the frontend, the webauth backend and
`tools/mock-token-endpoint.py` on a private bus under Xvfb. The mock requests no client certificate,
so no certificate adapter is configured. The test keyring uses the
[libsecret file backend](SECURITY.md#secrets) without an unlock prompt.

The frontend opt-in and backend configuration follow [Development frontend](CLI.md#development-frontend).
The script appends missing `WebAuthentication1` and `Certificate1` names, preserves existing
opt-ins and configures the webauth backend. It does not start a certificate backend.

The mock serves OpenID configuration, `/authorize` and `/token`. It records the authorization
challenge and verifies PKCE `S256` at exchange. It authenticates no users and issues synthetic token
strings. Its event log records method, path, grant type and PoP status while excluding codes,
verifiers, tokens and challenges. Assertions use that log to check grants and the absence of
network calls for cache hits.

| Run | Checks |
|---|---|
| 1 | `login` opens a window, uses PKCE `S256` and `prompt=select_account`, exchanges a code and stores an account. |
| 2 | `accounts` lists the account. |
| 3 | `token --prompt never` serves the cached token with no authority request, including discovery. |
| 4 | `token --req-cnf` receives `interaction_required` on refresh, falls back to interactive authorization without a `prompt` parameter, and exchanges a code with `token_type=pop` and `req_cnf`. The returned token carries the confirmation object. |
| 5 | A second `token --req-cnf` performs another grant because PoP tokens are uncached. The second response is checked as JSON. |
| 6 | `logout` removes the account; `accounts` is empty and a silent token request exits `30`. |
| 7 | With `DBUS_SESSION_BUS_ADDRESS` unset, an interactive request exits `40`; its diagnostic names the interface and frontend opt-in. |

The final check scans captured client stderr for `code=`, `code_verifier`, `refresh_token` and
JWT-shaped strings.

## Live runs

Live runs require attendance: they use a real card's PIN retry counter. Set up both portals as
[the webauth backend's testing documentation](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/TESTING.md)
describes, using the [development frontend convention](CLI.md#development-frontend). On the same
session bus, run `login`, an ARM gateway bearer-token request and a session-host proof-of-possession
request with the caller's confirmation object. ARM is Azure Resource Manager; RDS is Remote Desktop
Services.

Expect two certificate choosers and one PIN prompt in a run matching the [live evidence](#live-evidence).
A third chooser indicates a regression. A cached ARM request should log `cache outcome=hit` and
no `token-request`. The RDS request may need an interactive trust interstitial, as recorded in the
[live evidence](#live-evidence).

With [debug logging enabled](CLI.md#output), look for `authorize authority=... tenant=<hash> pop=no`,
`portal outcome=COMPLETED`, `callback outcome=CODE`, `token-request grant=authorization_code`
and `keyring outcome=stored`. [Logging](SECURITY.md#logging) defines field redaction.
`UNRELATED` or `INVALID` indicates a callback-classification failure.

## Live evidence

- 2026-09-03: hardware observation of FreeRDP requesting a second authorization for
  `ms-device-service://termsrv.wvd.microsoft.com/name/<host>/user_impersonation`, followed by an
  authorization-code exchange with `req_cnf`. Entra displayed a remote-desktop trust interstitial
  inside the sign-in window.
- 2026-09-04: [FreeRDP #13340](https://github.com/FreeRDP/FreeRDP/pull/13340) opened for the external
  web-view helper protocol. The recorded review was favourable for 3.32. See
  [FreeRDP integration](CLI.md#freerdp-integration) for the contract.
- 2026-09-05, 10:42: live Entra ID sign-in against a US Government tenant. The portal window
  reached Entra, `certauth` requested a client certificate, and WebKit's network process resolved
  it through the Certificate portal's PKCS#11 module. The shell prompted for the PIN, the card
  signed with RSA-PSS, and completion navigation was intercepted with the authorization code
  captured.
- 2026-09-05, 14:21: the `client/entra-token-helper` sdl-freerdp fork, this client, both portals
  and a card established an Azure Virtual Desktop session. The ARM gateway token came from the
  keyring cache. The RDS session-host PoP token required an interactive re-authentication through
  the portal under Conditional Access, the tenant's policy for fresh sign-in. This fork-only
  proof of concept had no upstream proposal.

Coverage is limited to one PIV card, one reader, OpenSC, GNOME 50 on Wayland and one tenant. A second
card, PIN-pad reader, card removal during an operation, KDE, a Flatpak runtime and a second tenant
are untested. The [known-problems list](../README.md#known-problems-and-open-questions) records
other limits.
