# Entra client CLI contract

`entra-token-helper` provides Entra ID / AVD tokens to RDP clients and other programs. See
[README.md](../README.md) for status and [Testing](TESTING.md) for mock and live evidence.
The client calls the public portal on `org.freedesktop.portal.Desktop`; it does not select a
backend. The portal's
[public interface](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/PUBLIC-INTERFACE.md)
and
[frontend/backend architecture](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/decisions/0008-build-to-the-upstream-shape.md)
are independent of CLI verbs, exit codes and fields.

JSON objects carry `"schema": 1`; see the [compatibility promise](#compatibility-promise).

## Verbs

| Verb | Purpose | Interactive? |
|---|---|---|
| `login` | Sign in and store an account and refresh token. Prints no access token. | Always |
| `token` | Acquire a bearer or proof-of-possession access token. | Depends on `--prompt` and cache state |
| `accounts` | List stored accounts. | Never |
| `logout` | Remove one account or all accounts and their keyring credentials. | Never |

A token-provider callback can invoke `token`; the other verbs allow account management outside a
connection attempt.

## Options

| Option | Applies to | Meaning |
|---|---|---|
| `--authority <a>` | `login`, `token` | Authority host, normally `login.microsoftonline.com` or `login.microsoftonline.us`. Also accepts `https://<host>/<tenant>`; only the host and tenant are used. Query, fragment, userinfo, non-HTTPS scheme or a deeper path is a usage error. The host must be allowlisted. Required unless `--cloud` supplies it. |
| `--tenant <id>` | `login`, `token` | Tenant identifier or `common`/`organizations`. Bounded ASCII alphanumerics, `-` and `.`. Defaults to the authority URL's tenant or `common`. A conflicting tenant in `--authority` is a usage error. |
| `--cloud <name>` | `login`, `token` | `commercial` or `usgov`. Supplies the authority host and, without `--scope`, the AVD resource scope plus `openid`, `profile` and `offline_access`. An explicit authority must agree with the cloud. |
| `--client-id <guid>` | `login`, `token` | Public client id. Defaults to AVD's `a85cf173-4192-42f8-81fa-777a763e6e2c`. Additional ids require configuration-file entries. |
| `--scope <scope>` | `login`, `token` | Repeatable, decoded scopes. Each occurrence can contain a space-separated list. The cache key uses the sorted, deduplicated set. `token` requires scopes or `--cloud`. |
| `--req-cnf <b64url>` | `token` | Caller-supplied base64url JSON confirmation object, such as `{"kid":"<key-id>"}`. Requests a token bound to the caller's key. Omission requests a bearer token. |
| `--account <id>` | `token`, `logout` | Select a stored account. Omission on `token` selects the sole match for authority, tenant and client; no match or ambiguity gives exit `30` on a silent request. Omission on `logout` removes all accounts. |
| `--prompt {auto,always,never}` | `login`, `token` | `auto` defaults to silent acquisition followed by interaction when needed. `always` forces sign-in for bearer requests; see [acquisition](#acquisition) for PoP behaviour. `never` forbids a window and gives exit `10` if interaction is required, or `30` without an account. `login --prompt never` gives exit `64`. |
| `--parent-window <handle>` | `login`, `token` | Advisory window handle passed to the portal's `Start`. It grants no authorization. |
| `--session-mode {shared,ephemeral}` | `login`, `token` | Passed to the portal. The portal restricts unidentified callers to `ephemeral`. |
| `--timeout <seconds>` | `login`, `token` | Portal window timeout, default 300. Passed as the portal's `timeout` option. The local response deadline allows another 15 seconds for a positive timeout; zero uses a 300-second local deadline. |
| `--json` | all | JSON output: success on stdout; operational errors on stderr. |
| `--format {raw,json}` | all | `json` is equivalent to `--json`; `raw` is the default. |
| `--config <path>` | all | Configuration path, overriding `ENTRA_TOKEN_HELPER_CONFIG`. |
| `--verbose` | all | Allow debug diagnostics on stderr with the same redaction rules; see [output](#output) for GLib's environment requirement. |
| `--help`, `--version` | all | Print help or version and exit `0`. |

## Output

Successful results use stdout; operational errors and all enabled log levels use stderr.
Debug logging requires both
`--verbose` and GLib's `entra` debug domain enabled, for example with `G_MESSAGES_DEBUG=entra`
or `G_MESSAGES_DEBUG=all`. `--verbose` alone does not enable GLib's debug output. Debug and
informational messages retain GLib's domain filtering. Logs leave raw token and JSON output
on stdout unchanged, including on a non-zero exit.

### Plain form

`token` prints the access token followed by a newline:

```text
eyJ0eXAiOiJKV1Qi...
```

`login` prints `Signed in as <account>`. `accounts` prints account id, authority host and tenant,
tab separated, one account per line. `logout` prints `Removed <account>` for each removed account.

### JSON form

A successful `token --json` prints one object on stdout:

```json
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

- `status`: `"ok"`, `"interaction_required"`, `"cancelled"`, `"no_account"`, `"unavailable"`,
  `"server_error"`, `"usage"` or `"internal"`.
- `token` and `access_token`: aliases containing the same access token, present only for a
  successful `token` request.
- `token_type`: `"Bearer"` or `"pop"`, as returned by the authority.
- `expires_in`: seconds remaining when the response is written.
- `scope`: granted scopes, falling back to requested scopes if the authority omits them.
- `account`: the account associated with the token.
- `error`: stable machine-readable symbol on failure.
- `message`: redacted human-readable failure text, excluding credentials and the authority's
  `error_description`.

Successful `accounts --json`, `login --json` and `logout --json` responses contain an `accounts`
array in place of token fields. Each entry has `account`, `authority` (host), `tenant` and
`client_id` fields.

Operational failures with `--json` print an error object on stderr. Argument parsing and usage
validation can print plain stderr text even with `--json`.

```json
{
  "schema": 1,
  "status": "interaction_required",
  "error": "interaction_required",
  "message": "a sign-in window is required and --prompt never was given"
}
```

Responses exclude refresh tokens, authorization codes, PKCE verifiers, `state`, PINs and raw
`error_description` values.

## Exit codes

| Code | Name | Meaning |
|---|---|---|
| `0` | success | Request succeeded. `token` writes its result to stdout. |
| `10` | interaction required | A window is required and `--prompt never` forbids it. Retry with `--prompt auto` if interaction is allowed. |
| `20` | cancelled | Portal `Response` is `1`: the user closed the sign-in window. Avoid an immediate interactive retry. |
| `30` | no such account | A silent request has no matching account, or account selection is ambiguous. Use `login` or select an account explicitly. |
| `40` | provider unavailable | Portal interface, session bus, keyring or authority is unavailable. The portal diagnostic names its interface, path, frontend opt-in and backend requirement. A dispatcher should try another provider. |
| `50` | authorization server error | The authority refused the request for a reason requiring more than sign-in, such as a bad client or server fault. Diagnostics include redacted OAuth error codes. |
| `64` | usage | Invalid arguments; `EX_USAGE` from `sysexits.h`. |
| `70` | internal | Unexpected client failure, invalid callback, or portal `Response` `2` for a reason such as timeout or `backend_completion_mismatch`; `EX_SOFTWARE`. |

Portal failure reasons `no_backend`, `backend_disappeared`, `credential_unavailable` and
`no_display` map to `40`. Cancelling the certificate chooser returns `Response` `2` with
`credential_unavailable` and exits `40`. Cancelling the PIN prompt during signing fails the TLS
handshake; the page load then usually ends as `load_failed`, which exits `70`. Other `Response` `2` reasons map to `70`. Dispatchers
should preserve the difference between unavailability (`40`), cancellation (`20`) and server
refusal (`50`).

`invalid_grant`, `interaction_required`, `consent_required` and `login_required` in a refresh
response's `error` or `suberror` trigger interactive fallback when permitted, or exit `10` when
interaction is forbidden. Authorization-code exchange failures propagate directly without
another interactive attempt.

## Acquisition

### Login

1. Validate authority, tenant and client id against the parsing rules and allowlists. Derive
   endpoints from the cloud table. OpenID discovery runs when an endpoint is needed; a failed
   discovery fetch logs a debug event and leaves the derived endpoints in use.
2. Build an authorization URL with `response_type=code`, PKCE `S256`, a fresh 256-bit `state` and
   the `nativeclient` redirect. Use `prompt=select_account`, or `prompt=login` with `--prompt always`.
3. Subscribe to `Response` before calling
   `org.freedesktop.portal.experimental.WebAuthentication1.Start`. Supply the redirect as
   `completion_uri`; the authorization URL sent to the portal includes `state`.
4. Classify the returned URI and exchange its code with the PKCE verifier.
5. Store the refresh token, ID token and issued access token in the keyring.
6. Print the account label. Claim preference is `preferred_username`, `upn`, `unique_name`,
   `email`, then `sub`; absent a usable claim, use `unknown@<authority-host>`.

ID-token handling is documented under [secrets](SECURITY.md#secrets).

### Token

Bearer requests with `--prompt always` skip both cache lookup and refresh. Other bearer requests
check the cache: a token more than five minutes from expiry is returned without network access,
including discovery. On a cache miss, attempt a refresh grant if the account has a refresh token.
Sign in interactively when no refresh token is available or refresh requires interaction, subject
to the [failure handling](#exit-codes) above. Without an account, a silent request exits `30`;
a stored account requiring interaction gives `10`.

For proof-of-possession requests, send `token_type=pop` and `req_cnf` on each grant and never cache
the returned token. Try the refresh grant first, including with `--prompt always`. If interaction
is required, authorize for the requested scope without a `prompt` parameter,
then exchange the code with the same confirmation object. An interactive exchange stores a
rotated refresh token for an existing account. The successful PoP refresh path does not persist
refresh-token rotation.

The RDS-AAD scope can require a remote-desktop trust interstitial; see the
[dated observation](TESTING.md#live-evidence).

### Callback classifier

The portal checks completion-URI matching. The client validates the OAuth response against its
transaction. Acceptance requires:

- Equal scheme, host, port and path; an empty path and `/` are equivalent.
- No userinfo or fragment.
- Exactly one `state`, compared in constant time with the transaction's value.
- Exactly one of `code` or `error`, occurring once. A valueless parameter counts, so
  `?code=good&code` is rejected.
- Well-formed percent escapes and no decoded NUL (`%00`) in `state`, `code` and `error` values.
  Unrecognized query fields are skipped before decoding; `ignored=%00` is not rejected.

A transaction accepts one terminal response. Further responses are rejected as replays or
unrelated requests.

## Storage

One Secret Service item stores an account, using schema `io.github.sjtrotter.entra-token-helper`.

| Attribute | Value |
|---|---|
| `authority` | Full `https://<host>/<tenant>` base; tenants have separate items |
| `client_id` | Public client id |
| `account` | Account label, normally the UPN |

The [security model](SECURITY.md#secrets) describes stored tokens and persistence; see the
[reporting policy](../SECURITY.md) for findings.

## Request

The client accepts command-line arguments. This schema describes equivalent inputs for a future
socket or D-Bus transport:

```json
{
  "schema": 1,
  "verb": "token",
  "authority": "login.microsoftonline.us",
  "tenant": "<tenant-id>",
  "client_id": "a85cf173-4192-42f8-81fa-777a763e6e2c",
  "scopes": [
    "https://www.wvd.azure.us/.default",
    "openid",
    "profile",
    "offline_access"
  ],
  "token_kind": "pop",
  "req_cnf": "<base64url JSON confirmation object>",
  "account": "<user>@<tenant-domain>",
  "prompt": "auto",
  "parent_window": "wayland:<handle>"
}
```

`token_kind` is `bearer` or `pop`; `pop` requires `req_cnf`. Scopes are decoded. The advisory
`parent_window` passes through the frontend to the backend for parenting and grants no
authorization.

Endpoint and redirect constraints are defined under [access control](SECURITY.md#access-control).
Report violations under the [security policy](../SECURITY.md).

## Environment

The client uses the desktop session's `DBUS_SESSION_BUS_ADDRESS` to reach the portal and keyring.
A display is required by the backend. An unavailable display maps to exit `40`.

| Variable | Meaning |
|---|---|
| `ENTRA_TOKEN_HELPER_CONFIG` | Optional configuration path. Overrides the default under `$XDG_CONFIG_HOME`; `--config` takes precedence. |

Allowlist and certificate-trust settings are documented under [configuration file](#configuration-file).

## Development frontend

Use the [`experimental/integration` branch of `sjtrotter/xdg-desktop-portal`](https://github.com/sjtrotter/xdg-desktop-portal/tree/experimental/integration)
with configured backends. The public bus name is `org.freedesktop.portal.Desktop`; public and
backend portal interfaces use `/org/freedesktop/portal/desktop`.

The frontend derives the app id, validates both URIs, filters options and returns one `Response`.
The backend owns the window's security chrome, storage partition, navigation interception and TLS
client-certificate handling.

| Portal | Public interface | Backend interface |
|---|---|---|
| Web authentication | `org.freedesktop.portal.experimental.WebAuthentication1` | `org.freedesktop.impl.portal.experimental.WebAuthentication1` |
| Certificate | `org.freedesktop.portal.experimental.Certificate1` | `org.freedesktop.impl.portal.experimental.Certificate1` |

For a portal to be exported, its `<Name>1` must appear in the frontend's
`XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL` environment at startup and its backend must be configured.
For smart-card sign-in through both portals, include `WebAuthentication1,Certificate1`, comma
separated without spaces. Preserve existing entries and append missing names. Setting this only
in the client's environment does not enable an interface in a running frontend.

Request and Session handles use `/request/...` and `/session/...` under
`/org/freedesktop/portal/desktop`. An interactive request without WebAuthentication exits `40`;
cached token retrieval and account management do not require the portal.

From a checkout of `xdg-desktop-portal-webauth`, start a development frontend on the session bus:

```bash
tools/dev-stack.sh --live --keep --no-e2e
```

This replaces the session frontend, preserves installed `.portal` entries and effective
`portals.conf` settings, routes the WebAuthentication backend interface to webauth, and adds
`WebAuthentication1` to the frontend opt-in. For both portals, run this command from that same
webauth checkout:

```bash
tools/portal-stack.sh --live --no-e2e --keep
```

Invoke the client's `login --cloud usgov --tenant <tenant-id>` from another terminal on the same
session bus. This repository's private mock stack is documented under
[end-to-end tests](TESTING.md#end-to-end-tests).

## Configuration file

The GKeyFile path defaults to `$XDG_CONFIG_HOME/entra-token-helper/config` (normally
`~/.config/entra-token-helper/config`), with overrides listed under [environment](#environment).
A missing file uses the built-in defaults.

```ini
[allow]
authorities = login.example.invalid;
client_ids  = 00000000-0000-0000-0000-000000000000;

[testing]
trust_certificate = /path/to/fixture.pem
```

`[allow]` adds individual authorities or client ids, without wildcards. `[testing]
trust_certificate` replaces system trust with a PEM file for the mock authority in
`tools/entra-e2e.sh`. The backend separately uses `--debug-trust-certificate` for its web view.
There are no direct environment overrides for these settings. The file's trust boundary is
documented under [access control](SECURITY.md#access-control).

## FreeRDP integration

[FreeRDP #13340](https://github.com/FreeRDP/FreeRDP/pull/13340) defines an external web-view helper:
JSON-RPC over pipes, with `navigate` carrying a URL, redirect URI prefix and timeout. The helper
returns the redirect URL verbatim; FreeRDP owns OAuth. Selection uses
`/azure:auth-helper:<path|autodetect>`. The opening date and review evidence are in
[Testing](TESTING.md#live-evidence).

Integration requires a helper implementing that protocol and forwarding `navigate` to the portal.
That helper is unimplemented here; it would not need this client's OAuth implementation. The
`client/entra-token-helper` fork branch is a proof of concept; see the live evidence.

`sso-mib` is the sibling provider for Intune-enrolled devices. It calls Microsoft's Linux
Identity Broker over D-Bus. The broker holds a device-bound primary refresh token (PRT), which
this browser-based client cannot mint.

## Compatibility promise

Within `"schema": 1`:

- Verb names, option names and meanings remain stable.
- Exit codes retain their meanings and are never reused for different conditions.
- Plain `token` output remains the token followed by a newline, including with debug logging.
- Optional options and JSON fields may be added. Consumers must ignore unknown fields.
- New exit codes may describe additional conditions. Treat unknown non-zero codes as hard errors.
- `status` and `error` symbols are stable. `message` text can change and must not be parsed.

An incompatible change requires `"schema": 2` and a documented migration.

The portal interface version is independent of the CLI schema. The client uses
[`org.freedesktop.portal.experimental.WebAuthentication1`](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/PUBLIC-INTERFACE.md).
Portal versioning follows [ADR 0004](decisions/0004-merged-experimental-portal-convention.md).
The backend interface version is independent.
