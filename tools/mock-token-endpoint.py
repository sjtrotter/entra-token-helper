#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
# This authority is a protocol fixture and verifies no identities.

import argparse
import base64
import hashlib
import http.server
import json
import secrets
import ssl
import sys
import threading
import time
import urllib.parse

TOKEN_LIFETIME = 3599


def b64url(data):
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode("ascii")


def jwt(payload):
    # Fixture tokens carry a dummy signature.
    header = b64url(json.dumps({"alg": "RS256", "typ": "JWT"}).encode("utf-8"))
    body = b64url(json.dumps(payload).encode("utf-8"))
    return f"{header}.{body}.{b64url(b'signature')}"


def id_token(account, tenant):
    return jwt(
        {
            "aud": "a85cf173-4192-42f8-81fa-777a763e6e2c",
            "iss": f"https://mock/{tenant}/v2.0",
            "iat": int(time.time()),
            "exp": int(time.time()) + TOKEN_LIFETIME,
            "preferred_username": account,
            "upn": account,
            "tid": tenant,
            "sub": "mock-subject",
        }
    )


class Handler(http.server.BaseHTTPRequestHandler):
    server_version = "entra-mock/1"
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    # Events exclude credential values and PKCE challenges.
    def _record(self, note):
        path = self.server.event_log
        if not path:
            return
        with open(path, "a", encoding="utf-8") as handle:
            handle.write(note + "\n")

    def _json(self, obj, status=200):
        body = json.dumps(obj).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _redirect(self, location):
        self.send_response(302)
        self.send_header("Location", location)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def _text(self, body, status=200):
        payload = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def do_GET(self):  # noqa: N802
        parsed = urllib.parse.urlsplit(self.path)
        parts = [p for p in parsed.path.split("/") if p]

        if parsed.path.endswith("/.well-known/openid-configuration"):
            tenant = parts[0] if parts else "common"
            self._record(f"GET discovery tenant={tenant}")
            base = f"https://{self.server.public_host}/{tenant}"
            self._json(
                {
                    "issuer": f"{base}/v2.0",
                    "authorization_endpoint": f"{base}/oauth2/v2.0/authorize",
                    "token_endpoint": f"{base}/oauth2/v2.0/token",
                    "response_modes_supported": ["query", "fragment", "form_post"],
                    "response_types_supported": ["code"],
                    "grant_types_supported": ["authorization_code", "refresh_token"],
                    "code_challenge_methods_supported": ["S256"],
                }
            )
            return

        if parsed.path.endswith("/oauth2/v2.0/authorize"):
            self._authorize(parts[0] if parts else "common", parsed)
            return

        self._text("<html><body>mock authority</body></html>", status=404)

    def _authorize(self, tenant, parsed):
        query = urllib.parse.parse_qs(parsed.query, keep_blank_values=True)

        def one(name):
            values = query.get(name, [])
            return values[0] if values else None

        method = one("code_challenge_method")
        challenge = one("code_challenge")
        redirect = one("redirect_uri")
        state = one("state")
        scope = one("scope") or ""

        problems = []
        if one("response_type") != "code":
            problems.append("response_type")
        if method != "S256":
            problems.append("code_challenge_method")
        if not challenge:
            problems.append("code_challenge")
        if not state:
            problems.append("state")
        if not redirect:
            problems.append("redirect_uri")

        self._record(
            "GET authorize tenant=%s prompt=%s pkce=%s scopes=%d%s"
            % (
                tenant,
                one("prompt") or "-",
                method or "-",
                len(scope.split()),
                (" bad=" + ",".join(problems)) if problems else "",
            )
        )

        if problems:
            self._text("<html><body>Invalid authorization request</body></html>", status=400)
            return

        code = secrets.token_urlsafe(24)
        self.server.codes[code] = {
            "challenge": challenge,
            "redirect": redirect,
            "scope": scope,
            "tenant": tenant,
        }

        joiner = "&" if "?" in redirect else "?"
        # The portal intercepts nativeclient navigation before the code leaves the machine.
        self._redirect(
            redirect
            + joiner
            + urllib.parse.urlencode({"code": code, "state": state})
        )

    def do_POST(self):  # noqa: N802
        parsed = urllib.parse.urlsplit(self.path)
        parts = [p for p in parsed.path.split("/") if p]

        if not parsed.path.endswith("/oauth2/v2.0/token"):
            self._json({"error": "invalid_request"}, status=404)
            return

        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length).decode("utf-8")
        form = urllib.parse.parse_qs(body, keep_blank_values=True)

        def one(name):
            values = form.get(name, [])
            return values[0] if values else None

        tenant = parts[0] if parts else "common"
        grant = one("grant_type")
        req_cnf = one("req_cnf")
        scope = one("scope") or ""

        self._record(
            "POST token tenant=%s grant=%s pop=%s token_type=%s scopes=%d"
            % (tenant, grant or "-", "yes" if req_cnf else "no", one("token_type") or "-",
               len(scope.split()))
        )

        if grant == "authorization_code":
            self._authorization_code(tenant, one, scope, req_cnf)
        elif grant == "refresh_token":
            self._refresh(tenant, one, scope, req_cnf)
        else:
            self._json({"error": "unsupported_grant_type"}, status=400)

    def _authorization_code(self, tenant, one, scope, req_cnf):
        code = one("code")
        verifier = one("code_verifier")
        entry = self.server.codes.pop(code, None)

        if entry is None:
            self._record("POST token outcome=unknown_code")
            self._json({"error": "invalid_grant", "error_description": "unknown code"}, status=400)
            return

        if not verifier:
            self._json({"error": "invalid_grant", "error_description": "no verifier"}, status=400)
            return

        digest = hashlib.sha256(verifier.encode("ascii")).digest()
        if b64url(digest) != entry["challenge"]:
            self._record("POST token outcome=pkce_mismatch")
            self._json(
                {"error": "invalid_grant", "error_description": "PKCE verification failed"},
                status=400,
            )
            return

        if one("redirect_uri") != entry["redirect"]:
            self._json({"error": "invalid_grant", "error_description": "redirect"}, status=400)
            return

        self._record("POST token outcome=code_ok pop=%s" % ("yes" if req_cnf else "no"))
        self._issue(tenant, scope, req_cnf, with_refresh=not req_cnf)

    def _refresh(self, tenant, one, scope, req_cnf):
        if one("refresh_token") != self.server.refresh_token:
            self._record("POST token outcome=unknown_refresh_token")
            self._json({"error": "invalid_grant", "error_description": "unknown"}, status=400)
            return

        if req_cnf and self.server.fail_pop_refresh > 0:
            self.server.fail_pop_refresh -= 1
            self._record("POST token outcome=pop_refresh_interaction_required")
            self._json(
                {
                    "error": "interaction_required",
                    "suberror": "basic_action",
                    "error_description": "AADSTS50076: the user must approve this connection",
                },
                status=400,
            )
            return

        if not req_cnf and self.server.fail_refresh > 0:
            self.server.fail_refresh -= 1
            self._record("POST token outcome=refresh_invalid_grant")
            self._json(
                {
                    "error": "invalid_grant",
                    "error_description": "AADSTS50173: the grant has expired",
                },
                status=400,
            )
            return

        self._record("POST token outcome=refresh_ok pop=%s" % ("yes" if req_cnf else "no"))
        self._issue(tenant, scope, req_cnf, with_refresh=not req_cnf)

    def _issue(self, tenant, scope, req_cnf, with_refresh):
        claims = {
            "aud": scope.split()[0] if scope else "mock",
            "iss": f"https://mock/{tenant}",
            "exp": int(time.time()) + TOKEN_LIFETIME,
            "upn": self.server.account,
            "scp": scope,
        }
        if req_cnf:
            claims["cnf"] = {"req_cnf": req_cnf}

        response = {
            "token_type": "pop" if req_cnf else "Bearer",
            "scope": scope,
            "expires_in": TOKEN_LIFETIME,
            "ext_expires_in": TOKEN_LIFETIME,
            "access_token": jwt(claims),
        }

        if with_refresh:
            response["refresh_token"] = self.server.refresh_token
            response["id_token"] = id_token(self.server.account, tenant)

        self._json(response)


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description="Mock Entra ID authority for protocol tests")
    parser.add_argument("--port", type=int, default=0)
    parser.add_argument("--cert", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--account", default="fixture@mock.invalid")
    parser.add_argument("--host", default="localhost", help="Host name advertised in discovery endpoints")
    parser.add_argument("--fail-pop-refresh", type=int, default=0)
    parser.add_argument("--fail-refresh", type=int, default=0)
    parser.add_argument("--event-log")
    parser.add_argument("--port-file")
    args = parser.parse_args()

    server = Server(("127.0.0.1", args.port), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(args.cert, args.key)
    server.socket = context.wrap_socket(server.socket, server_side=True)

    port = server.socket.getsockname()[1]
    server.public_host = f"{args.host}:{port}"
    server.account = args.account
    server.codes = {}
    server.refresh_token = "mock-refresh-" + secrets.token_urlsafe(16)
    server.fail_pop_refresh = args.fail_pop_refresh
    server.fail_refresh = args.fail_refresh
    server.event_log = args.event_log

    if args.port_file:
        with open(args.port_file, "w", encoding="utf-8") as handle:
            handle.write(str(port))

    sys.stderr.write("Mock authority listening: port=%d host=%s\n" % (port, server.public_host))
    sys.stderr.flush()

    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        thread.join()
    except KeyboardInterrupt:
        server.shutdown()

    return 0


if __name__ == "__main__":
    sys.exit(main())
