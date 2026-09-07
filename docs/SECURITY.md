# Security model

The threat model of the portal that draws the sign-in window is in the webauth backend's
[docs/SECURITY.md](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/SECURITY.md).
This document covers only this client, which [README.md](../README.md) describes; it was split out
of that document on 2026-09-07.

## Threat model

**In scope.**

- A same-UID process asking the client for a token for a different client id, a different resource,
  or against an attacker-chosen authority.
- Credential material, codes, tokens and verifiers, reaching a log, a terminal scrollback, or a bug
  report.
- A hostile parent process manipulating the client through argv or environment.
- A response that is not the response to this transaction.

**Out of scope.** As above: same-UID debuggers, a compromised keyring daemon, and the tenant's own
Conditional Access policy.

## Access control

**Same UID only**, by construction: the client is a one-shot process spawned by, and returning to, a
process of the same user. When it grows an IPC transport, the socket lives in `$XDG_RUNTIME_DIR`
with mode `0700` and peer credentials are checked on every connection.

**Allowlists by default.**

- Client id: the AVD public client `a85cf173-4192-42f8-81fa-777a763e6e2c`.
- Authority: `login.microsoftonline.com`, `login.microsoftonline.us`.
- Completion URI: `https://login.microsoftonline.com/common/oauth2/nativeclient`.

A caller may not extend these. A *user* may, through an explicit configuration-file override naming
each addition individually: never a wildcard, never an environment variable, never a command-line
flag a hostile parent could set. The reason is not that arbitrary OAuth is dangerous in itself; it
is that a client willing to sign in to anything, on behalf of anything, and print the result on
stdout, is a phishing primitive with a keyring attached.

**Never accept an endpoint from a caller.** The request carries an *authority host*; the client
derives the authorization endpoint, the token endpoint and the completion URI from its own tables,
optionally refined by OpenID discovery *against that same authority*. A request containing any URL
is a usage error, not a configuration. This is the one rule that stops a same-UID caller pointing a
credential-bearing exchange at a server it controls, and it is why the **client**, not its caller,
chooses the `completion_uri` it hands to the portal.

Two details of how that is enforced, because they are where it would rot:

- `--authority` accepts the `https://<host>/<tenant>` form as well as a bare host, because that is
  what Microsoft's documentation and an `.rdpw` file call the authority. **Only the host and the
  first path segment are read**, on the terms [CLI.md](CLI.md) sets out, so
  `--authority https://login.microsoftonline.us/t/oauth2/v2.0/token` is refused rather than obeyed
  as an endpoint.
- A **discovered** endpoint is subject to the same rule from the other side: it is used only if it
  is `https` and on the same host (and port) as the authority that served the document. A discovery
  document is not a licence to move the exchange somewhere else, and a client that took one at face
  value would have handed the redirect back to whoever answered the well-known URL.

**No environment variable widens anything.** Not the authority allowlist, not the client-id
allowlist, not certificate trust. `ENTRA_TOKEN_HELPER_CONFIG` and `--config` choose *which* file is
read and nothing else, and both allowlist additions and the single fixture trust anchor live in
that file, one entry at a time. A hostile parent process controls the environment and the command
line; it does not control a file in `$XDG_CONFIG_HOME`.

## OAuth rules

- **`state` on every authorization request**, cryptographically random, compared in **constant
  time**. `state` is the secret a response has to know, and no part of the portal ever sees it.
- **PKCE S256 on every authorization request**, verifier sent only with the token request. A public
  client cannot keep a secret; PKCE is what binds the code to the process that asked.
- **Exact redirect matching** on the returned URI: scheme, host, port and path equal to the
  transaction's redirect (empty path and `/` are the same resource). Not redundant with the
  portal's checks: the backend and the frontend each answered a question about URIs, the client
  answers a question about OAuth, using a secret no part of the portal ever held. A `strstr` for `code=`, which is what FreeRDP's
  existing fallback does, accepts a redirect to an entirely different host.
- **Reject userinfo and fragment.**
- **Exactly one of `code` or `error`, each occurring exactly once.** A parameter present without a
  value counts as an occurrence, so a second `code` cannot be smuggled in as a bare `code`.
- **Strict percent-decoding**, on the same terms as the portal's and for the same reason.
- **Single-use transactions.** One terminal result. A second response is a replay or an answer to a
  request this process did not make.
- **Bounded transactions**, with a deadline passed to the portal as `timeout` and enforced locally
  as well.

## Secrets

| Artifact | Where it lives | Why |
|---|---|---|
| **Refresh token** | Secret Service keyring only. Never on stdout, never in the JSON response, never in a log, never in a file, never returned to a caller under any option. | Months of standing access. A caller handed one has been handed the user's identity, not a token for one connection. The keyring is the only store on a Linux desktop with a plausible claim to protect it at rest. |
| **Access token** | Printed once on stdout, and cached **inside the same keyring secret** as the refresh token, under the sorted scope set. | Short-lived and scoped, and the caller needs it. The CLI is a one-shot process: an in-memory cache would live for the length of one `token` call and every connection would spend a refresh round trip. Caching it beside the refresh token puts it under the same protection at rest and adds nothing to what an attacker with the keyring already has. It is still never written to a file, a log, or the JSON error response. |
| **PoP token** | Printed once on stdout and **never stored at all**. | A PoP token bound to one `kid` is useless for another; a cache that ignored the binding would return a token the caller cannot use, and one that keyed on it would be a store of per-connection secrets for no benefit, the grant being a single round trip. |
| **ID token** | Keyring, beside the refresh token. Its `preferred_username`/`upn` claim is displayed. | It names the account so a human can tell two sign-ins apart. **Its signature is not checked and nothing is authorized by it**: it arrived over TLS from an endpoint the client chose, answering a request the client made with a verifier no one else has. That transport is the trust, not a local signature check. The claim is validated for being UTF-8 with no control characters before it is displayed, because a name that reaches a terminal is a place to hide an escape sequence. |
| **Authorization code** | In memory for the seconds between the completion and the token request. Scrubbed. | Exchangeable for a refresh token by anyone holding it plus the public client id. PKCE is what stops that, and PKCE is not a reason to be careless with the code. |
| **PKCE verifier, `state`** | In memory for the transaction. Scrubbed. | The verifier binds the code to this process; `state` binds the response to this request. |
| **PIN** | Never seen by the Entra client. Under the portal backend's `portal` provider, never seen there either, because the token declares a protected authentication path and the Certificate portal prompts. Under `pkcs11`, read from a file the operator named, held only until the transaction ends, then overwritten; never in argv, never in a URI, never logged. | A component having no path to a secret is better than a component being careful with one, which is the argument for finishing the `portal` provider. |
| **Private key on the card** | Never leaves the card. | That is the point of the card. |
| **PoP key** | Never seen by the client. FreeRDP generates and retains it; the client receives only `req_cnf`. | The client cannot leak what it never has. |
| **Account records** (account id, authority, tenant, client id) | Keyring, beside the refresh token. | Not secret in the same sense, but they name a person and a tenant. |

**No persistent cache mode.** When the Secret Service is unavailable the client does **not** fall
back to a file: it exits `40` so a dispatcher can fall through. A silent downgrade from "keyring"
to "file in the home directory" is the kind of thing nobody notices until it is in a backup. Note
what this rule is and is not about: it forbids the *client* inventing a store of its own, not the
*user* choosing which Secret Service implementation is running. The client's `tools/entra-e2e.sh`
runs against libsecret's own file backend (`SECRET_BACKEND=file`) because a private bus has no
keyring daemon;
the client neither knows nor chooses that, and asks libsecret the same question either way.

**One item per account, and the authority is part of its name.** The keyring item's attributes are
the full `https://<host>/<tenant>` authority, the client id and the UPN. Keying on the host alone
would make the same person in two tenants one item, with the second sign-in overwriting the first.

**Logout must be complete, and today it is not.** `logout` removes the refresh token and the
account record. It does **not** yet ask the portal to discard the web session that account
established, and it deliberately does not call the authority's `end_session` endpoint, because a
logout that opened a browser window would be a surprising thing for a `logout` verb to do. A logout
leaving the Entra session cookie behind has not logged anybody out of Entra; it has only stopped
*this client* from being able to act as them. The gap is real and is recorded here rather than in a
comment. Until it is closed, `--session-mode ephemeral` is what stops a session outliving a
transaction at all.

## Logging

**Never logged, at any level, redacted or not:** access tokens, refresh tokens, ID tokens, any JWT;
authorization codes; PKCE verifiers and challenges; `state` values; the completion URI or any URI
carrying a query string from the authorization server; the authorization server's
`error_description`, which routinely names the account, the tenant and the policy that failed; HTTP
response bodies from the token endpoint; cookies and `Authorization` headers.

**Structural redaction**, as on the portal side.

**What a DEBUG log may contain:** outcome symbols from the callback classifier (`CODE`, `ERROR`,
`UNRELATED`, `INVALID`); the authority host, a public constant and the single most useful field
when diagnosing a wrong-cloud failure; OAuth error *codes* without their descriptions
(`invalid_grant`, `interaction_required`, `AADSTS50011`); cache hit and miss counts; the portal's
response code; and phase timings.

**What a DEBUG log must not become:** a way to reproduce the sign-in. If a support bundle containing
a DEBUG log would let its reader connect as the user, the redaction is wrong. There is no "trace"
level that relaxes these rules, because a level that relaxes them will be enabled by somebody in
production.

**Structurally, not textually.** `entra_log_event()` takes `(name, kind, value)` triples and the
*kind* decides what is printed: an authority host prints, an OAuth error code prints, a tenant id
prints as a stable hash of itself (so two lines about one tenant can still be seen to be about one
tenant), and an account, a code, a token or an `error_description` prints as its kind, never its
value, and for the description not even its length. There is no format string a caller can slip a
token through and no "log this URL" entry point a later edit could point at a redirect.
The client's `tools/entra-e2e.sh` greps its own stderr for `code=`, `code_verifier`, `refresh_token`
and anything JWT-shaped after every run.

## Independent review

Two places where a subtle mistake is not visible in testing:

1. The callback classifier (`src/oauth/callback.c`).
2. The cache key (`src/cache/`): anything missing from it is a token returned to the wrong requester.

## Reporting

See [SECURITY.md](../SECURITY.md) at the top level.
