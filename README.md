# entra-token-helper

A Linux command-line client that acquires Entra ID / Azure Virtual Desktop access tokens. It owns
the OAuth half: PKCE, the authorization-code exchange, the refresh grant, proof-of-possession
tokens bound to a key its caller generated, and a Secret Service account store. It asks
xdg-desktop-portal to draw the sign-in window through the experimental
`org.freedesktop.portal.experimental.WebAuthentication` interface. The portal, not this program,
hosts the web engine; with the companion certificate portal behind it, that window answers a
smart-card TLS client-certificate challenge. This client calls the portal and nothing else, and does
not know which backend serves it.

## Status

Last checked 2026-09-07.

| | State |
|---|---|
| `login`, `token`, `accounts`, `logout`; bearer and proof-of-possession requests | Implemented |
| PKCE `S256`, code exchange, `refresh_token` grant, lazy OpenID discovery | Implemented |
| The callback classifier: exact redirect match, constant-time `state`, one of `code` or `error` | Implemented (`src/oauth/callback.c`) |
| Secret Service account store; no token, code or `state` in any output | Implemented (`src/cache/`, `src/log/`) |
| Interactive sign-in through the portal | Implemented (`src/webauth_client.c`) |
| Sign-in against a real identity provider, with a smart card | Proven 2026-09-05 (below) |
| The full FreeRDP-to-AVD chain | Proven 2026-09-05, through a fork-only proof of concept |
| A portal exporting the interface | Required: an experimental branch of xdg-desktop-portal, behind a gate |
| Integration with FreeRDP's helper protocol ([#13340](https://github.com/FreeRDP/FreeRDP/pull/13340)) | Not implemented |
| Proposed anywhere | No. No issue and no pull request, for this client or for the portals it uses |
| Packaging, a release, an independent security review | Not done |

Two runs on real hardware, both on 2026-09-05:

- 10:42. A live Entra ID sign-in against a US Government tenant. The portal window reached Entra,
  `certauth` challenged for a client certificate, WebKit's network process resolved it through the
  certificate portal's PKCS#11 module, the shell prompted for the PIN, the card signed with RSA-PSS,
  and the completion navigation was intercepted with the authorization code captured.
- 14:21. The whole chain: an sdl-freerdp fork branch, this client, both portals, the card, and an
  Azure Virtual Desktop session. Real tokens were exchanged. The ARM gateway token came from the
  keyring cache. The RDS proof-of-possession token needed one interactive re-authentication through
  the portal window, which the tenant's Conditional Access policy explains.

The evidence is narrow: one PIV card, one reader, OpenSC, GNOME 50 on Wayland, one tenant. A second
card, a PIN-pad reader, card removal mid-operation, KDE and a Flatpak runtime were not exercised.

## How it works

```
  entra-token-helper                            THIS REPOSITORY
       OAuth: PKCE, the code exchange, the refresh grant, proof-of-possession,
       the account store. No window, no web engine, no card.
       |
       |  org.freedesktop.portal.experimental.WebAuthentication      [gated]
       v  on org.freedesktop.portal.Desktop
  xdg-desktop-portal                            an experimental branch of that project
       derives the app id, validates both URIs, filters the options, and
       guarantees one Response.
       |
       |  org.freedesktop.impl.portal.experimental.WebAuthentication
       v
  xdg-desktop-portal-webauth                    SEPARATE REPOSITORY
       the GTK4 + WebKitGTK window, the security chrome, the storage partition,
       the navigation interception, the TLS client certificate.
       |
       |  PKCS#11, through p11-kit
       v
  xdg-desktop-portal-certificate                SEPARATE REPOSITORY, optional
       the certificate chooser, the PIN prompt, the signing key on the card.
```

The interface is not exported unless xdg-desktop-portal was started with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication` (or `all`), off by default. With the
gate off, a request needing a window exits `40` and names the variable; cached tokens and account
management do not need the portal.

## The CLI

| Verb | Purpose |
|---|---|
| `login` | Sign in through the portal and store the account and its refresh token. Always interactive. |
| `token` | Acquire an access token, bearer or proof-of-possession. Silent when the cache allows it. |
| `accounts` | List stored accounts. |
| `logout` | Remove one account or all of them. |

stdout carries only the result; everything else goes to stderr. Exit codes are contractual: `10`
interaction required, `20` cancelled, `30` no such account, `40` provider unavailable, `50`
authorization server error, `64` usage, `70` internal. A dispatcher should treat `40` as a decline
and fall through.

```console
$ entra-token-helper login --cloud usgov --tenant <tenant-id>
Signed in as <user>@<tenant-domain>

$ entra-token-helper token --cloud usgov --tenant <tenant-id> \
    --account <user>@<tenant-domain> --req-cnf eyJraWQiOiI8a2V5LWlkPiJ9 --json
{
  "schema": 1,
  "status": "ok",
  "token": "eyJ0eXAiOiJhdCtqd3Qi...",
  "access_token": "eyJ0eXAiOiJhdCtqd3Qi...",
  "token_type": "pop",
  "expires_in": 3599,
  "scope": "ms-device-service://termsrv.wvd.microsoft.com/name/<host>/user_impersonation",
  "account": "<user>@<tenant-domain>"
}
```

A proof-of-possession token is never cached, so each one is a fresh grant. On the tested tenant the
RDS scope required one interactive re-authentication, answered inside the portal window. The full
contract is [docs/CLI.md](docs/CLI.md).

## Try it

A standalone meson project needing GLib, GIO, libsoup-3, json-glib and libsecret, all required.

```console
$ meson setup build && ninja -C build && meson test -C build
```

Six unit suites (`pkce`, `callback`, `jwt`, `discovery`, `cache`, `redact`) with 68 assertions,
needing no display and no bus.

The end-to-end run is not a meson test: it needs a headless X server, a built frontend and a
checkout of the portal backend.

```console
$ WEBAUTH_REPO=../xdg-desktop-portal-webauth XDP_BUILD=../xdg-desktop-portal/build \
    tools/entra-e2e.sh
```

It stands up `xdg-permission-store`, the frontend with the gate on, the webauth backend, and
`tools/mock-token-endpoint.py`, an Entra-shaped authority, on one private bus inside one headless
X server, then drives seven runs covering the four verbs, the cache, the interstitial path a
proof-of-possession token takes, and a machine with no portal at all. The assertions are lines the
mock's event log wrote, not inferences from exit codes. [docs/TESTING.md](docs/TESTING.md) has the
rest.

## Relationship to FreeRDP

Upstream pull request [FreeRDP/FreeRDP#13340](https://github.com/FreeRDP/FreeRDP/pull/13340), opened
2026-09-04 and reviewed favourably for 3.32, moves FreeRDP's AAD web view out of process: FreeRDP
speaks JSON-RPC over pipes to a helper it starts, `navigate` carries a URL, a redirect URI prefix
and a timeout, and the helper answers with the redirect URL verbatim. OAuth stays inside FreeRDP,
and the helper is selected with `/azure:auth-helper:<path|autodetect>`.

That seam is the integration path, and it is not this program: it is a small helper speaking
#13340's protocol that forwards `navigate` to the portal. Under that split FreeRDP needs none of
this client's OAuth code. The `client/entra-token-helper` branch used in the 2026-09-05 chain is a
fork-only proof of concept, not proposed upstream. `sso-mib` remains the sibling provider for
Intune-enrolled devices, which hold a device-bound primary refresh token this client cannot mint.

## Why not

- A loopback redirect with `xdg-open`: the only redirect URI registered for the AVD public client is
  a remote HTTPS one, so nothing local can receive the completion.
  [0002](docs/decisions/0002-no-loopback-redirect.md)
- Impersonating the Microsoft Identity Broker's D-Bus name: no stable contract to implement, and the
  name asserts enrollment semantics a browser flow cannot honour.
  [0003](docs/decisions/0003-no-broker-impersonation.md)
- Keeping this inside one RDP client, or shipping it as a library: that buries a desktop identity
  capability in one plugin, or puts refresh tokens in every caller's address space.
  [0001](docs/decisions/0001-standalone-helper.md)

## Related repositories

- [xdg-desktop-portal-webauth](https://github.com/sjtrotter/xdg-desktop-portal-webauth): the portal
  backend that draws the sign-in window this client asks for.
- [xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate): the
  certificate portal backend: the chooser, the PIN prompt, and the PKCS#11 module the webauth
  backend loads.
- [xdg-desktop-portal, branch `experimental/certificate-webauthentication`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/certificate-webauthentication):
  the frontend that exports both interfaces.

## Known problems and open questions

- Nothing here is proposed anywhere. The interface this client depends on lives on a personal branch
  of xdg-desktop-portal behind an experimental gate. If it is refused, there is nothing to call.
- The FreeRDP integration is not written. Until the #13340 helper exists, the only demonstrated
  caller is a fork-only proof of concept.
- One certificate handshake raises two chooser dialogs and one PIN prompt, for reasons belonging to
  the certificate portal, not to this client.
- One tenant, one card, one desktop. Conditional Access behaviour, the RDS interstitial, and whether
  a refresh token survives long enough to make the silent path worth having are single observations.
- Identity providers may refuse an embedded engine outright, and the portal's session store is not
  Firefox's or Chrome's, so real browser SSO is not reproduced.
- The account name printed by `login` comes from an unverified ID token claim: a label, authorizing
  nothing.

## Documents

| | |
|---|---|
| [docs/CLI.md](docs/CLI.md) | The contract: verbs, options, JSON, exit codes, configuration |
| [docs/TESTING.md](docs/TESTING.md) | The suites, the end-to-end run, and what the live runs proved |
| [docs/SECURITY.md](docs/SECURITY.md) | Threat model, access control, OAuth rules, secret handling |
| [docs/decisions/](docs/decisions/) | The architecture decision records |

[CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md) are at the top level.

## License

LGPL-2.1-or-later. [LICENSE](LICENSE) is the LGPL-2.1 text,
[LICENSES/LGPL-2.1-or-later.txt](LICENSES/LGPL-2.1-or-later.txt) its REUSE copy; every file in
`src/` and `tests/` carries `SPDX-License-Identifier: LGPL-2.1-or-later`, and so does the
`license:` field of `meson.build`. They agree. This matches xdg-desktop-portal and the two portal
backends this client is written against. The binary is a separate process from any caller, reached
by `fork`/`exec` and a pipe, so no linking question arises with FreeRDP (Apache-2.0).

## AI assistance

The code and documents here were drafted with the assistance of Anthropic Claude and OpenAI Codex,
under direction and review. The architectural decisions, the verified facts about the AVD
application registration and the sovereign-cloud constants, and the judgement about what to build
are the author's.
