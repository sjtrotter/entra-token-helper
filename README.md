# entra-token-helper

`entra-token-helper` is a Linux command-line client for programs that need Entra ID access tokens
for Azure Virtual Desktop (AVD), including RDP clients on unenrolled machines. It handles OAuth,
bearer and proof-of-possession tokens, and account storage in the Secret Service keyring.
Interactive sign-in uses the experimental WebAuthentication portal, with an optional Certificate
portal for smart-card authentication.

## Try it

Build dependencies are GLib/GIO 2.76 or newer, libsoup 3, json-glib and libsecret. `meson.build`
still declares GLib 2.66 ([#4](https://github.com/sjtrotter/entra-token-helper/issues/4)).

```console
$ meson setup build && ninja -C build && meson test -C build
```

For interactive sign-in, use the
[`experimental/integration` frontend branch](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration)
with a configured WebAuthentication backend and the frontend opt-in described in
[Development frontend](docs/CLI.md#development-frontend).

```console
$ build/entra-token-helper login --cloud usgov --tenant <tenant-id>
Signed in as <user>@<tenant-domain>

$ build/entra-token-helper token --cloud usgov --tenant <tenant-id> \
    --account <user>@<tenant-domain> --req-cnf eyJraWQiOiI8a2V5LWlkPiJ9 --json
```

Replace the placeholders and confirmation object with your tenant, account and caller's key
binding. See the [CLI contract](docs/CLI.md) for options, output and exit codes, and
[Testing](docs/TESTING.md) for the mock end-to-end command and live-run evidence.

## Status

| Feature | State |
|---|---|
| `login`, `token`, `accounts`, `logout`; bearer and proof-of-possession requests | Implemented |
| PKCE `S256`, code exchange, `refresh_token` grant, lazy OpenID discovery | Implemented |
| Exact callback redirect match, constant-time `state`, one of `code` or `error` | Implemented (`src/oauth/callback.c`) |
| Secret Service account store; credential redaction in diagnostics | Implemented (`src/cache/`, `src/log/`) |
| Interactive sign-in through the portal | Implemented (`src/webauth_client.c`) |
| Sign-in against a real identity provider, with a smart card | Proven 2026-09-05; see [evidence](docs/TESTING.md#live-evidence) |
| FreeRDP-to-AVD chain | Proven 2026-09-05 through a fork-only proof of concept |
| A portal exporting the interface | Required; see [frontend setup](docs/CLI.md#development-frontend) |
| FreeRDP helper protocol ([#13340](https://github.com/FreeRDP/FreeRDP/pull/13340)) | Integration not implemented |
| Upstream proposal of the portal interfaces | Not started |
| Packaging, a release, an independent security review | Not done |

## Architecture

The client calls `org.freedesktop.portal.Desktop`. The frontend validates requests and routes them
to a configured backend. The webauth backend hosts the GTK4 and WebKitGTK sign-in window. For
smart-card sign-in, it uses the certificate portal's PKCS#11 module through p11-kit. The client
has no web engine or card handling and does not select a backend.

The [CLI contract](docs/CLI.md#development-frontend) lists the experimental interfaces and opt-in.
The [security model](docs/SECURITY.md) describes the OAuth checks and credential boundaries.

The architecture decisions cover a [standalone helper](docs/decisions/0001-standalone-helper.md),
the AVD application's [redirect constraint](docs/decisions/0002-no-loopback-redirect.md),
[broker impersonation](docs/decisions/0003-no-broker-impersonation.md), and the
[experimental portal convention](docs/decisions/0004-merged-experimental-portal-convention.md).
The [FreeRDP integration contract](docs/CLI.md#freerdp-integration) describes the proposed helper
and the sibling `sso-mib` provider for Intune-enrolled devices.

## Related repositories

- [xdg-desktop-portal-webauth](https://github.com/sjtrotter/xdg-desktop-portal-webauth): the sign-in
  window backend.
- [xdg-desktop-portal-certificate](https://github.com/sjtrotter/xdg-desktop-portal-certificate): the
  certificate chooser, PIN prompt and PKCS#11 module.
- [xdg-desktop-portal, branch `experimental/integration`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration):
  the frontend exporting both interfaces.

## Known problems and open questions

- The portal interfaces depend on a personal frontend branch. Their availability depends on
  acceptance and maintenance of those interfaces.
- One certificate handshake raises two chooser dialogs and one PIN prompt in the certificate
  portal.
- Conditional Access behaviour, the RDS interstitial and refresh-token longevity have only been
  observed in one tenant. Hardware and desktop coverage is listed in [Testing](docs/TESTING.md#live-evidence).
- Identity providers may refuse an embedded engine. The portal's session store is separate from
  Firefox's and Chrome's, so browser SSO is unavailable.
- The account name printed by `login` comes from an unverified ID token claim and grants no
  authorization.
- With `G_MESSAGES_DEBUG=entra` and `--verbose`, debug lines go to stdout and mix with the
  token output ([#4](https://github.com/sjtrotter/entra-token-helper/issues/4)).

## Documents

| Document | Contents |
|---|---|
| [docs/CLI.md](docs/CLI.md) | Verbs, options, JSON, exit codes, configuration |
| [docs/TESTING.md](docs/TESTING.md) | Unit suites, end-to-end tests, live runs |
| [docs/SECURITY.md](docs/SECURITY.md) | Threat model, access control, OAuth, secret handling |
| [docs/decisions/](docs/decisions/) | Architecture decision records |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contribution guidelines |
| [SECURITY.md](SECURITY.md) | Reporting security issues |

## License

LGPL-2.1-or-later, matching xdg-desktop-portal and both portal backends.
[LICENSE](LICENSE) contains the LGPL-2.1 text;
[LICENSES/LGPL-2.1-or-later.txt](LICENSES/LGPL-2.1-or-later.txt) is its REUSE copy. Files in `src/`
and `tests/` carry the SPDX identifier, and `meson.build` declares the license. Callers, FreeRDP (Apache-2.0) included, run it as a
separate process.

## AI assistance

The code and documentation were written with AI assistance (Anthropic Claude, OpenAI Codex)
under the author's direction. The author is responsible for their content, including the
verified AVD registration and sovereign-cloud facts.
