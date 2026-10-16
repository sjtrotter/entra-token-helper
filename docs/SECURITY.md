# Security model

## Threat model

- Requests for a different client id, resource or attacker-chosen authority from a same-UID caller.
- Credential material reaching diagnostics, terminal scrollback or bug reports.
- A parent process manipulating arguments or environment.
- A response belonging to another transaction.

Same-UID debuggers, a compromised keyring daemon and tenant Conditional Access policy are out of
scope. The client does not isolate credentials from arbitrary code running as the same user.
Project status is in [README.md](../README.md); the sign-in window's threat model is in the
[webauth backend's security documentation](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/SECURITY.md).

## Access control

The CLI is a one-shot process returning results to its caller under the same UID. A future IPC
transport would require a private directory with mode `0700` under `$XDG_RUNTIME_DIR` and peer
credential checks on every connection; no such transport is implemented.

| Built-in setting | Value |
|---|---|
| Client id | AVD public client `a85cf173-4192-42f8-81fa-777a763e6e2c` |
| Authorities | `login.microsoftonline.com`, `login.microsoftonline.us` |
| Completion URI | `https://login.microsoftonline.com/common/oauth2/nativeclient`, for both clouds |

The [configuration file](CLI.md#configuration-file) can extend the client-id and authority
allowlists. The completion URI is fixed.

The client derives authorization and token endpoints and redirect URIs from the validated
authority and cloud table. Callers cannot supply these endpoints, redirects or completion URIs.
The `--authority` host or HTTPS URL supplies only the host and tenant under the
[CLI parsing rules](CLI.md#options). For example,
`--authority https://login.microsoftonline.us/t/oauth2/v2.0/token` is rejected for its deeper path.
Discovered endpoints must use HTTPS and match the authority's host and port.

The selected configuration file is trusted input; the client does not check ownership or prevent
a parent from selecting another file. Selection precedence and settings are documented under
[environment](CLI.md#environment) and [configuration file](CLI.md#configuration-file).

## OAuth rules

PKCE `S256` binds the authorization code to the verifier sent only to the token endpoint.
The client applies [callback validation](CLI.md#callback-classifier) independently of the portal's
URI matching. Percent-decoding and NUL rejection cover decoded `state`, `code` and `error`
values. [Acquisition](CLI.md#acquisition) defines state generation and exchange;
[options](CLI.md#options) defines the portal timeout and local deadline's grace period.

## Secrets

| Artifact | Storage and handling |
|---|---|
| Refresh token | Keyring only; excluded from caller output, logs and client-managed files. It can provide long-lived access. |
| Bearer access token | Returned on successful `token` stdout, raw or JSON. Cached in the keyring under the sorted scope set for reuse across CLI invocations. Excluded from logs, files and JSON errors. |
| PoP access token | Returned on stdout and never cached. Bound to the caller's key through `req_cnf`; a copied token requires that key to be usable. |
| ID token | Received over TLS from the selected endpoint and stored in the keyring. Claims supply an account label; signature validation is absent and the label grants no authorization. Names must be UTF-8 without control characters before display. |
| Authorization code | Held in memory between completion and exchange; the extracted code is scrubbed after exchange. PKCE also requires the verifier for redemption. |
| PKCE verifier and `state` | Held in transaction memory and scrubbed at transaction teardown. |
| PIN | Never received by this client. The webauth `portal` provider uses a protected authentication path and the Certificate portal prompts. The webauth `pkcs11` provider reads an operator-selected file, holds its PIN until transaction end and overwrites it on release; it excludes PINs from logs. It still accepts `pin-value` in `--client-key-uri`, which exposes the PIN in argv and the URI ([webauth #6](https://github.com/sjtrotter/xdg-desktop-portal-webauth/issues/6)). |
| Card private key | Remains on the card. |
| PoP key | Generated and retained by the caller, such as FreeRDP. This client receives only `req_cnf`. |
| Account records | Stored beside the refresh token in the keyring; account id, authority, tenant and client id identify a person and tenant. |

The client exits `40` when keyring storage is unavailable; it has no file-cache fallback. Tests
select libsecret's file backend with `SECRET_BACKEND=file` on a private bus without a keyring
daemon. That is a libsecret test configuration, outside the client's storage selection.

Each item's JSON secret contains the refresh token, ID token and bearer tokens indexed by sorted
scope set. [Storage](CLI.md#storage) defines the item schema and account attributes.

`logout` removes the account record and its stored tokens. It does not discard the portal's web
session or call the authority's `end_session` endpoint. Entra cookies can outlive a local logout.
Use `--session-mode ephemeral` to restrict the web session to one transaction.

## Logging

Diagnostics must exclude raw access tokens, refresh tokens, ID tokens and JWTs; authorization
codes; PKCE verifiers and challenges; `state`; completion URIs and authorization-server query
strings; `error_description`; token-endpoint response bodies; cookies; and `Authorization`
headers. The same restrictions apply with `--verbose`. The [CLI output contract](CLI.md#output)
defines successful token output, debug enablement and the stdout limitation.

Debug diagnostics may contain callback outcome symbols (`CODE`, `ERROR`, `UNRELATED`, `INVALID`),
authority hosts, OAuth error codes such as `invalid_grant`, `interaction_required` and
`AADSTS50011`, cache hit or miss counts, portal response codes and phase timings. Logs must not
contain enough credential material to reproduce a sign-in.

`entra_log_event()` accepts `(name, kind, value)` triples. The field kind controls rendering:
authority hosts and OAuth error codes print directly; tenant ids print as stable hashes; accounts,
codes and tokens print as kind and length; descriptions print only as their kind. The API has no
arbitrary URL logging entry point. The [end-to-end checks](TESTING.md#end-to-end-tests) scan
captured stderr for credentials.

## Independent review

Review the callback classifier (`src/oauth/callback.c`) for response validation, the account store
(`src/cache/keyring.c`) and cache key (`src/cache/`) for request separation, and structural
redaction (`src/log/redact.c`) for credential handling. Unit tests cannot establish their full
security.

## Reporting

See [SECURITY.md](../SECURITY.md) for reporting instructions.
