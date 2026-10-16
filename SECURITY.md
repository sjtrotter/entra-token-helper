# Security

`entra-token-helper` has no packaged release or supported deployment; see [README.md](README.md)
for status and [testing evidence](docs/TESTING.md#live-evidence) for live coverage.

Report findings at <https://github.com/sjtrotter/entra-token-helper>. For a private finding, open
an issue requesting a contact without including vulnerability details. A release should define
a security contact and disclosure window.

Follow the [identifier and credential rules](CONTRIBUTING.md#ground-rules) in reports and patches.
Report any credential escaping the client's [logging redaction](docs/SECURITY.md#logging),
including with `--verbose`.

The [CLI contract](docs/CLI.md) defines caller inputs and configuration. The
[security model](docs/SECURITY.md) covers access control, OAuth checks, secret handling and
review areas.

The sign-in window runs in a separate process. Its phishing surface, certificate grant lifetime
and frontend-derived app identity are covered by the
[webauth backend's security model](https://github.com/sjtrotter/xdg-desktop-portal-webauth/blob/main/docs/SECURITY.md).
