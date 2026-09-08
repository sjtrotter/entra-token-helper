# entra-token-helper

## Why this exists

A Linux RDP client connecting to Azure Virtual Desktop (AVD) must first obtain access tokens from
Entra ID, Microsoft's identity service, formerly Azure AD. On a tenant that requires a smart card
during sign-in and registers no local redirect URI, both ordinary routes are closed: the sign-in
cannot be handed to the user's own browser, and Microsoft's Intune broker, the Identity Broker
service for enrolled devices, is absent on an unenrolled machine. Each client has therefore had to
embed a web engine of its own, or ask the user to paste a redirect URL back out of a browser. This
program does the OAuth once, for any caller, and asks the desktop portal for the window.

## What it is

A Linux command-line client that acquires Entra ID / AVD access tokens. It owns the OAuth half:
PKCE, the authorization-code exchange, the refresh grant, proof-of-possession tokens (access tokens
bound to a key the caller holds, so a copy taken from the wire is useless on its own), and an
account store in the Secret Service, the freedesktop D-Bus secret-storage API that GNOME Keyring
and KWallet implement. It asks xdg-desktop-portal to draw the sign-in window through the
experimental `org.freedesktop.portal.WebAuthentication.X1` interface. The portal, not this
program, hosts the web engine; with the companion certificate portal behind it, that window answers
a smart-card TLS client-certificate challenge. This client calls the portal and nothing else, and
does not know which backend serves it.

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
| A portal exporting the interface | Required: an experimental branch of xdg-desktop-portal, with a backend configured as described below |
| Integration with FreeRDP's helper protocol ([#13340](https://github.com/FreeRDP/FreeRDP/pull/13340)) | Not implemented |
| Proposed anywhere | No. No issue and no pull request, for this client or for the portals it uses |
| Packaging, a release, an independent security review | Not done |

Two runs on real hardware, both on 2026-09-05:

- 10:42. A live Entra ID sign-in against a US Government tenant. The portal window reached Entra,
  `certauth` challenged for a client certificate, WebKit's network process resolved it through the
  certificate portal's PKCS#11 module, the shell prompted for the PIN, the card signed with RSA-PSS,
  and the completion navigation was intercepted with the authorization code captured.
- 14:21. The whole chain: an sdl-freerdp fork branch, this client, both portals, the card, and an
  Azure Virtual Desktop session. A connection needs two real tokens: one for the Azure Resource
  Manager (ARM) gateway that brokers it, one for the session host, on the Remote Desktop Services
  (RDS) scope. The ARM token came from the keyring cache. The RDS proof-of-possession token needed
  one interactive re-authentication through the portal window, demanded by Conditional Access, the
  tenant rules deciding when a fresh interactive sign-in is required.

The evidence is narrow: one PIV card, one reader, OpenSC, GNOME 50 on Wayland, one tenant. A second
card, a PIN-pad reader, card removal mid-operation, KDE and a Flatpak runtime were not exercised.

## How it works

```
  entra-token-helper                            THIS REPOSITORY
       OAuth: PKCE, the code exchange, the refresh grant, proof-of-possession,
       the account store. No window, no web engine, no card.
       |
       |  org.freedesktop.portal.WebAuthentication.X1
       v  on org.freedesktop.portal.Desktop,
       |  at /org/freedesktop/portal/desktop/experimental
  xdg-desktop-portal                            an experimental branch of that project
       derives the app id, validates both URIs, filters the options, and
       guarantees one Response.
       |
       |  org.freedesktop.impl.portal.WebAuthentication.X1
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

Experimental portals live on their own object path, and are exported only when a backend for them
is configured: unless xdg-desktop-portal found one, the interface is not exported, and a request
needing a window exits `40` saying so. Cached tokens and account management do not need the
portal.

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
  "token_type": "pop",
  "expires_in": 3599,
  "account": "<user>@<tenant-domain>"
}
```

`--req-cnf` is the binding: a base64url JSON confirmation object naming the key the caller
generated. A proof-of-possession token is never cached, so each one is a fresh grant. On the tested
tenant the RDS scope required one interactive re-authentication, answered inside the portal window.
The full contract is [docs/CLI.md](docs/CLI.md).

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

It stands up `xdg-permission-store`, the frontend, the webauth backend and
`tools/mock-token-endpoint.py`, an Entra-shaped authority, on one private bus inside one headless
X server, then drives seven runs against them. The assertions are lines the mock's event log wrote,
not inferences from exit codes. [docs/TESTING.md](docs/TESTING.md) has the rest.

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
Intune-enrolled devices: it talks over D-Bus to the Microsoft Identity Broker, the service Microsoft
ships for enrolled Linux machines, which holds a device-bound primary refresh token, a long-lived
credential tied to the device itself, that this client cannot mint.

## Why not

- A loopback redirect with `xdg-open`: the AVD public client's only registered redirect URI is a
  remote HTTPS one, so nothing local can receive the completion.
  [0002](docs/decisions/0002-no-loopback-redirect.md)
- Impersonating the Microsoft Identity Broker's D-Bus name: no stable contract to implement, and the
  name asserts enrollment semantics a browser flow cannot honour.
  [0003](docs/decisions/0003-no-broker-impersonation.md)
- Keeping this inside one RDP client, or shipping it as a library: that buries a desktop identity
  capability in one plugin, or puts refresh tokens in every caller's address space.
  [0001](docs/decisions/0001-standalone-helper.md)

## Related repositories

- [xdg-desktop-portal-webauth](https://github.com/sjtrotter/xdg-desktop-portal-webauth): the backend
  that draws the sign-in window.
- [xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate): the
  chooser, the PIN prompt, and the PKCS#11 module the webauth backend loads.
- [xdg-desktop-portal, branch `experimental/integration`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration):
  the frontend that exports both interfaces.

## Known problems and open questions

- The interface this client depends on is on a personal branch of xdg-desktop-portal, experimental,
  proposed nowhere. If it is refused, there is nothing to call.
- One certificate handshake raises two chooser dialogs and one PIN prompt, for reasons belonging to
  the certificate portal, not to this client.
- Conditional Access behaviour, the RDS interstitial, and whether a refresh token survives long
  enough to make the silent path worth having are single observations.
- Identity providers may refuse an embedded engine outright, and the portal's session store is not
  Firefox's or Chrome's, so real browser SSO is not reproduced.
- The account name printed by `login` comes from an unverified ID token claim: a label, authorizing
  nothing.

## Documents

| | |
|---|---|
| [docs/CLI.md](docs/CLI.md) | The contract: verbs, options, JSON, exit codes, configuration |
| [docs/TESTING.md](docs/TESTING.md) | The suites, the end-to-end run, and how to repeat the live runs |
| [docs/SECURITY.md](docs/SECURITY.md) | Threat model, access control, OAuth rules, secret handling |
| [docs/decisions/](docs/decisions/) | The architecture decision records |

[CONTRIBUTING.md](CONTRIBUTING.md) and [SECURITY.md](SECURITY.md) are at the top level.

## License

LGPL-2.1-or-later, matching xdg-desktop-portal and the two portal backends this client is written
against. [LICENSE](LICENSE) is the LGPL-2.1 text and
[LICENSES/LGPL-2.1-or-later.txt](LICENSES/LGPL-2.1-or-later.txt) its REUSE copy; every file in
`src/` and `tests/` carries the SPDX identifier, as does the `license:` field of `meson.build`. The binary is a separate process from any caller, reached by `fork`/`exec`
and a pipe, so no linking question arises with FreeRDP (Apache-2.0).

## AI assistance

The code and documents here were drafted with the assistance of Anthropic Claude and OpenAI Codex,
under direction and review. The architectural decisions, the verified facts about the AVD
application registration and the sovereign-cloud constants, and the judgement about what to build
are the author's.
