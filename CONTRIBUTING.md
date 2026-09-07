# Contributing

The client is implemented. It signed in against a real Entra ID tenant and ran the full
FreeRDP-to-AVD chain on 2026-09-05; read [docs/TESTING.md](docs/TESTING.md) before changing
anything, because most of the rules here have a test.

## What is most wanted

- **Reasons this is wrong.** The three decision records in [docs/decisions/](docs/decisions/) are
  the argument for this shape. A fourth reason it is the wrong one is worth more than a feature.
- **Corrections to facts.** The AVD public client id, the registered `nativeclient` redirect, the
  sovereign-cloud constants and the `AADSTS50011` behaviour were verified by hand. If any of them
  has changed, that matters more than anything else here.
- **A second caller.** The FreeRDP integration is a helper speaking
  [#13340](https://github.com/FreeRDP/FreeRDP/pull/13340)'s protocol, and it is not written.

## Ground rules

- **Never commit a real tenant id, hostname, account name, token, refresh token, authorization code
  or certificate.** Use `<tenant-id>`, `<user>@<tenant-domain>`, `<host>` and similar placeholders,
  in documents and in commit messages alike. Test fixtures use synthetic GUIDs and `.invalid`
  hostnames. The only real identifiers in this repository are the AVD public client id, Microsoft's
  authority, scope and redirect constants, and the error code `AADSTS50011`.
- **Nothing secret is ever written outside the keyring.** Not to a file, not to a log, not to the
  JSON response, not at `--verbose`. `src/log/redact.c` and `tests/test-redact.c` are how that is
  enforced; a new field on a log line needs a test that says it stays out.
- **This client knows no backend.** It calls `org.freedesktop.portal.Desktop` and nothing else. No
  `.portal` file, no impl interface, no backend name may appear in `src/`. That is what makes "the
  portal is protocol-independent" a testable claim rather than a slogan.
- **A caller may not supply a URL.** Not a token endpoint, not an authorization endpoint, not a
  redirect URI, not a completion URI. Every URL is derived from the authority and the client's own
  cloud table.
- **Every source, test, tool and meson file carries `SPDX-License-Identifier: LGPL-2.1-or-later` and
  `SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>`.**
- **Say what is unproven.** One tenant, one card, one reader, one desktop. Mark what is still an
  assumption rather than smoothing it over.
- **Disclose AI assistance** in commits where it was used, as the existing commits do.

## Building and testing

```console
$ meson setup build && ninja -C build && meson test -C build
```

GLib, GIO, libsoup-3, json-glib and libsecret, all required. `meson test` is the whole no-display
suite: six suites, no bus and no network. The end-to-end run needs a headless X server, a built
frontend and a checkout of the portal backend — see [docs/TESTING.md](docs/TESTING.md).

## Style

C11, tabs, 100 columns, an SPDX header — see [`.editorconfig`](.editorconfig). Headers carry the
doc comments: a header explaining *why* an interface is shaped the way it is is worth more than one
restating its own signatures, and a rule that has a reason should be readable next to the code that
enforces it.

## Sign-off and licence

A `Signed-off-by` trailer (`git commit -s`) is welcome but not required. By sending a change you
agree it is licensed under this project's licence, LGPL-2.1-or-later.
