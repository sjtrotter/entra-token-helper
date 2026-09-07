# Security

This is `entra-token-helper`, an Entra ID / Azure Virtual Desktop token client. It holds refresh
tokens, so what it gets wrong is worth reporting.

**Nothing is released, and there is no deployment to report a vulnerability against.** The client is
implemented and has been run against a real Entra ID tenant and a real card
([docs/TESTING.md](docs/TESTING.md)), but it is not packaged and has no users. Please open an issue
at <https://github.com/sjtrotter/entra-token-helper>. If a finding is one you would rather not file
in public, say so in an issue with no detail in it and a contact will be arranged. When there is
something running, this file will name a contact and a disclosure window.

**Never put a real tenant id, account name, token, refresh token or authorization code in an issue,
a log excerpt or a patch.** Redact them to `<tenant-id>` and `<user>@<tenant-domain>`. The client
redacts its own output (`src/log/redact.c`), including at `--verbose`; if you find something that
escapes it, that is itself the bug worth reporting.

The interesting parts:

- **The callback classifier**, `src/oauth/callback.c`. The portal guarantees only that a URI matched
  the completion URI it was given; whether that URI is *this transaction's* authorization response
  is decided here — exact scheme, host, port and path, no userinfo, no fragment, `state` compared in
  constant time, exactly one of `code` or `error`, strict percent-decoding.
- **The account store**, `src/cache/keyring.c`. One Secret Service item per authority, client id and
  account. The refresh token is the thing in it that is months of standing access.
- **The redaction rules**, `src/log/redact.c`.
- **What a caller may set.** No URL of any kind may come from a caller or from the environment; the
  authority and client id allowlists widen only from a user-owned configuration file, one entry at a
  time. [docs/CLI.md](docs/CLI.md) is the contract.

The window this client asks for is drawn by another process. The threat model for the portal — the
phishing surface an interactive sign-in API creates, the certificate grant lifetime, the app id the
frontend derives — is in
[xdg-desktop-portal-webauth's docs/SECURITY.md](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/SECURITY.md).
