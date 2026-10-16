# Contributing

## Useful contributions

- Review the architecture in [docs/decisions/](docs/decisions/).
- Correct factual errors, especially the AVD public client id, registered `nativeclient`
  redirect, sovereign-cloud constants and verified `AADSTS50011` behaviour.
- Add a caller. The helper for [FreeRDP #13340](https://github.com/FreeRDP/FreeRDP/pull/13340)'s
  protocol is unimplemented; see [FreeRDP integration](docs/CLI.md#freerdp-integration) and
  [project status](README.md).

## Ground rules

- Redact tenant ids to `<tenant-id>` and account names to `<user>@<tenant-domain>` in documents,
  reports, log excerpts and commit messages; use `<host>` for hostnames. Keep real identifiers,
  credentials and certificates out of patches and reports, including log excerpts. Test fixtures
  use synthetic GUIDs and `.invalid` hostnames. The repository's real identifiers are the AVD
  public client id, Microsoft's authority, scope and redirect
  constants, and `AADSTS50011`.
- Follow the [secret-handling and logging rules](docs/SECURITY.md). A new log field needs a
  redaction test; see `src/log/redact.c` and `tests/test-redact.c`.
- Portal calls in `src/` target `org.freedesktop.portal.Desktop`. Keep backend names, impl
  interfaces and `.portal` files out of the client.
- Follow the [caller input constraints](docs/CLI.md#request) for endpoints and redirect URIs.
- Preserve `SPDX-License-Identifier: LGPL-2.1-or-later` and
  `SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>` headers in every
  source, test, tool and Meson file.
- Describe untested assumptions and the limits of live evidence.
- Disclose AI assistance in commits where it is used.

## Building and testing

[docs/TESTING.md](docs/TESTING.md) lists build dependencies and commands for unit and end-to-end tests.

## Style

Use C11, tabs and 100 columns; see [`.editorconfig`](.editorconfig). Put interface documentation
in headers. Comments should state a non-obvious invariant in one or two factual lines. Omit
comments that restate code, explain language idioms or narrate a change. Use plain wording in
documentation, diagnostics and help text. Test names state the property being tested.

## Sign-off and licence

A `Signed-off-by` trailer (`git commit -s`) is welcome but optional. Contributions are licensed
under LGPL-2.1-or-later.
