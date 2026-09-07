# Testing

Three tiers, in the order of what they cost: unit tests that need nothing, an end-to-end run that
needs a headless X server and two other builds, and a live run against a real tenant with a real
card that cannot be automated at all.

## Unit tests

```console
$ meson setup build && ninja -C build && meson test -C build
```

No display, no session bus, no network. Six suites, 68 assertions:

| Suite | What it pins |
|---|---|
| `pkce` | The RFC 7636 `S256` vector, base64url without padding, a fresh verifier every time, an exact `state` comparison, and a transaction that answers once |
| `callback` | The classifier: exact redirect match, no userinfo, no fragment, exactly one of `code` or `error`, a parameter present without a value still counting as an occurrence, strict percent-decoding |
| `jwt` | Which ID token claim the account name comes from, and that a malformed token is refused rather than guessed at |
| `discovery` | The cloud table and the client id allowlist, authority parsing, endpoints derived without a network, a discovery document refining them, an endpoint on another host or in plaintext discarded, the authorization URL, and how a token response and its errors are read |
| `cache` | The cache key — the sorted, de-duplicated scope set, so `--scope a --scope b` and `--scope "b a"` hit one entry — the five-minute expiry margin, a record surviving JSON, and that the exit codes are the documented ones |
| `redact` | That no token, code, account or server `error_description` can reach a log line, that a tenant is logged only as a hash, and that `--verbose` is a frequency and not a permission |

## End to end, against a mock authority

Not a meson test: it needs a headless X server, a built xdg-desktop-portal from the frontend branch,
and a checkout of the portal backend.

```console
$ WEBAUTH_REPO=../xdg-desktop-portal-webauth XDP_BUILD=../xdg-desktop-portal/build \
    tools/entra-e2e.sh
```

| Variable | Default | Meaning |
|---|---|---|
| `XDP_BUILD` | `../xdg-desktop-portal/build` | A build of the frontend branch |
| `WEBAUTH_REPO` | `../xdg-desktop-portal-webauth` | A checkout of the portal backend |
| `BACKEND` | `$WEBAUTH_REPO/build/src/xdg-desktop-portal-webauth` | That checkout's built backend |
| `HELPER` | `./build/entra-token-helper` | The client under test |
| `XDP_ENV` | `./.xdp-env` | Sourced if present, for a frontend that cannot resolve its libraries |
| `XVFB` | whatever is on `PATH` | The headless X server |

`--keep` leaves the stack up and prints the configuration file and the authority it wrote;
`--verbose` adds the client's own redacted breadcrumbs.

It puts `xdg-permission-store`, the frontend with
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=web-authentication`, the backend with **no certificate
adapter**, and `tools/mock-token-endpoint.py` on one private bus inside one headless X server, and
runs the client against them as an ordinary application calling the public interface. The keyring is
libsecret's file backend, so no daemon and no unlock prompt are involved.

The mock serves what a tenant serves — the OpenID configuration, an `/authorize` that records the
`code_challenge`, a `/token` that verifies `S256` against it — authenticates nobody, and mints
strings. It writes an event log carrying method, path, grant type and whether the request was a PoP
request, and **never** a code, a verifier, a token or a challenge. That log is what the run asserts
on: "a PoP grant reached the authority" is a line the server wrote, and "the cache was used" is the
absence of one.

The seven runs:

| | |
|---|---|
| 1 | `login`: a window, PKCE `S256`, `prompt=select_account`, a code, an exchange, an account stored |
| 2 | `accounts`: the stored account is listed |
| 3 | `token --prompt never`: the authority is not called **at all**, not even for discovery |
| 4 | `token --req-cnf`: the mock refuses the PoP refresh with `interaction_required`, the client opens the window again with no `prompt` parameter, exchanges a fresh code with `token_type=pop` and `req_cnf`, and the token comes back carrying the caller's confirmation blob |
| 5 | `token --req-cnf` again: the authority is called again, because a PoP token is never cached |
| 6 | `logout`: nothing is listed afterwards, and a silent request exits `30` rather than opening a window |
| 7 | No portal at all: exit `40`, with the message naming the environment variable |

A final check greps the client's stderr for anything that must never be written down.

## Against a real tenant, with a real card

Not scriptable and not to be run unattended: a real session bus, the real card in the real reader, a
real tenant, and a real PIN retry counter to spend. Stand the two portals up on the desktop as
[xdg-desktop-portal-webauth's docs/TESTING.md](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/TESTING.md)
describes, then drive `login` and two `token` requests — the ARM gateway bearer token, and the
session-host proof-of-possession token with a confirmation blob — from another terminal on that bus.

Two runs on 2026-09-05 are what this client has been proven by:

- **10:42.** A live sign-in against a US Government tenant. The portal window reached Entra,
  `certauth` challenged for a client certificate, WebKit's network process resolved it through the
  certificate portal's PKCS#11 module, the shell prompted for the PIN, the card signed with RSA-PSS,
  and the completion navigation was intercepted with the authorization code captured. Expect two
  certificate choosers and one PIN prompt; a third chooser is a regression.
- **14:21.** The whole chain: an sdl-freerdp fork branch, this client, both portals, the card, and
  an Azure Virtual Desktop session. Real tokens were exchanged. The ARM gateway token came from the
  keyring cache — `cache outcome=hit`, and no `token-request` line at all. The RDS
  proof-of-possession token needed one interactive re-authentication through the portal window,
  which the tenant's Conditional Access policy explains: the interstitial run 4 forces with the
  mock, happening for real.

What to watch in `--verbose`: `authorize authority=... tenant=<hash> pop=no` (the tenant is logged
as a hash, never the URL), `portal outcome=COMPLETED`, `callback outcome=CODE` — `UNRELATED` or
`INVALID` here is the interesting failure — `token-request grant=authorization_code`, then
`keyring outcome=stored`.

As of those runs, the correct description of this client is "it works against a mock, and against a
real Entra ID tenant through a proof-of-concept FreeRDP fork". Still unanswered: a second card, a
PIN-pad reader, card removal mid-operation, KDE, a Flatpak runtime, a second tenant, and whether a
refresh token survives long enough to make the silent path worth having.
