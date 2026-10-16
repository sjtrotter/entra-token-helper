#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

set -u

here() { cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd; }
REPO="$(here)"

XDP_BUILD="${XDP_BUILD:-$REPO/../xdg-desktop-portal/build}"
WEBAUTH_REPO="${WEBAUTH_REPO:-$REPO/../xdg-desktop-portal-webauth}"
BACKEND="${BACKEND:-$WEBAUTH_REPO/build/src/xdg-desktop-portal-webauth}"
HELPER="${HELPER:-$REPO/build/entra-token-helper}"
XDP_ENV="${XDP_ENV:-$REPO/.xdp-env}"
XVFB="${XVFB:-$(command -v Xvfb || true)}"
SCREEN="${SCREEN:-:94}"

TENANT="${TENANT:-8331b18d-2d87-48ef-a35f-ac8818ebf9b4}"
ACCOUNT="${ACCOUNT:-fixture@mock.invalid}"
SCOPE="${SCOPE:-https://www.wvd.azure.us/.default}"
POP_SCOPE="${POP_SCOPE:-ms-device-service://termsrv.wvd.microsoft.com/name/avd-host-1/user_impersonation}"
# FreeRDP supplies an opaque base64url req_cnf that must appear in the returned token.
REQ_CNF="${REQ_CNF:-eyJraWQiOiJPc3FYSmNKSUczRWxROUYxUEN4YXduYnJKYXk0NTVvSWNNQlNJVERnNVdBIn0}"

KEEP=0
VERBOSE=""

die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

# shellcheck source=tools/lib.sh
. "$REPO/tools/lib.sh"

usage() {
	cat <<'EOF'
Usage: tools/entra-e2e.sh [--keep] [--verbose] [-h | --help]

Run the client against a mock authority on a private D-Bus session under Xvfb.

Options:
  --keep       keep the test stack running after the checks
  --verbose    enable client debug logs
  -h, --help   show usage

Requires Python 3, OpenSSL, dbus-run-session, Xvfb and built portal components.
Environment: XDP_BUILD selects the frontend build; WEBAUTH_REPO selects the backend
checkout. BACKEND and HELPER override executable paths; XDP_ENV selects an optional
environment file. XVFB selects the X server; LOGDIR selects a private fixture directory.
See docs/TESTING.md for setup.
EOF
	exit 0
}

while [ $# -gt 0 ]; do
	case "$1" in
	--keep) KEEP=1 ;;
	--verbose) VERBOSE="--verbose" ;;
	-h | --help) usage ;;
	*) die "Unknown option '$1'; use --help" ;;
	esac
	shift
done

if [ -n "${LOGDIR:-}" ]; then
	fixture_make "$LOGDIR" entra-e2e
else
	LOGDIR="$(fixture_mktemp xdp-webauth-entra-e2e entra-e2e)"
fi

# shellcheck disable=SC1090
[ -n "$XDP_ENV" ] && [ -f "$XDP_ENV" ] && . "$XDP_ENV"
xdp_enable_experimental

command -v python3 >/dev/null || die "python3 not found"
command -v openssl >/dev/null || die "openssl not found"
command -v dbus-run-session >/dev/null || die "dbus-run-session not found"
[ -n "$XVFB" ] || die "Xvfb not found; set \$XVFB"
[ -x "$BACKEND" ] || die "Backend executable missing at $BACKEND; build xdg-desktop-portal-webauth or set BACKEND or WEBAUTH_REPO"
[ -x "$HELPER" ] || die "Client executable missing at $HELPER; run 'meson setup build && ninja -C build' or set HELPER"
[ -x "$XDP_BUILD/desktop-portal/xdg-desktop-portal" ] || die "Frontend executable missing; set XDP_BUILD"
[ -x "$XDP_BUILD/document-portal/xdg-permission-store" ] || die "Permission store executable missing; set XDP_BUILD"

if ldd "$XDP_BUILD/desktop-portal/xdg-desktop-portal" 2>/dev/null | grep -q 'not found'; then
	ldd "$XDP_BUILD/desktop-portal/xdg-desktop-portal" | grep 'not found' >&2
	die "Frontend libraries are unresolved; set XDP_ENV (see docs/TESTING.md)"
fi

DEVDIR="$LOGDIR/portals"
(umask 077 && mkdir -p "$DEVDIR")
xdp_write_portal_dir "$DEVDIR" "$WEBAUTH_REPO" private

# The fixture certificate is trusted explicitly by the backend flag and the client testing config.
openssl req -x509 -newkey rsa:2048 -nodes -days 1 -subj /CN=localhost \
	-addext "subjectAltName=DNS:localhost,IP:127.0.0.1" \
	-keyout "$LOGDIR/mock.key" -out "$LOGDIR/mock.pem" >"$LOGDIR/openssl.log" 2>&1 ||
	die "Fixture certificate creation failed; see $LOGDIR/openssl.log"

pass=0
fail=0

check() {
	local label="$1" ok="$2"

	if [ "$ok" = 0 ]; then
		printf '  ok    %s\n' "$label"
		pass=$((pass + 1))
	else
		printf '  FAIL  %s\n' "$label"
		fail=$((fail + 1))
	fi
}

check_grep() {
	local label="$1" file="$2" pattern="$3"

	if grep -qE -- "$pattern" "$file" 2>/dev/null; then
		printf '  ok    %-46s %s\n' "$label" "$(grep -hoE -- "$pattern" "$file" | head -1)"
		pass=$((pass + 1))
	else
		printf '  FAIL  %-46s (no /%s/ in %s)\n' "$label" "$pattern" "${file##*/}"
		fail=$((fail + 1))
	fi
}

check_count() {
	local label="$1" file="$2" pattern="$3" want="$4" got

	got="$(grep -cE -- "$pattern" "$file" 2>/dev/null)"
	if [ "$got" = "$want" ]; then
		printf '  ok    %-46s %s\n' "$label" "$got"
		pass=$((pass + 1))
	else
		printf '  FAIL  %-46s want %s, got %s\n' "$label" "$want" "$got"
		fail=$((fail + 1))
	fi
}

check_pop_binding() {
	local file="$1"

	if python3 - "$file" "$REQ_CNF" <<'PY'
import base64, json, sys

token = open(sys.argv[1], encoding="utf-8").read().strip()
segments = token.split(".")
if len(segments) != 3:
    sys.exit(1)
payload = segments[1]
payload += "=" * (-len(payload) % 4)
claims = json.loads(base64.urlsafe_b64decode(payload))
sys.exit(0 if claims.get("cnf", {}).get("req_cnf") == sys.argv[2] else 1)
PY
	then
		check "PoP req_cnf binding" 0
	else
		check "PoP req_cnf binding" 1
	fi
}

inner() {
	"$XDP_BUILD/document-portal/xdg-permission-store" >"$LOGDIR/permission-store.log" 2>&1 &
	PERM=$!
	sleep 1

	# Start the frontend before GTK can activate the system portal through a settings request.
	"$XDP_BUILD/desktop-portal/xdg-desktop-portal" -v >"$LOGDIR/frontend.log" 2>&1 &
	FE=$!
	xdp_wait_for_name org.freedesktop.portal.Desktop "$FE" || {
		tail -20 "$LOGDIR/frontend.log" >&2
		echo "${0##*/}: Frontend did not acquire org.freedesktop.portal.Desktop"
		return 40
	}

	# The first PoP refresh requires interaction to exercise the RDS-AAD consent flow.
	rm -f "$LOGDIR/port" "$LOGDIR/events.log"
	python3 "$REPO/tools/mock-token-endpoint.py" \
		--cert "$LOGDIR/mock.pem" --key "$LOGDIR/mock.key" \
		--account "$ACCOUNT" --fail-pop-refresh 1 \
		--event-log "$LOGDIR/events.log" --port-file "$LOGDIR/port" \
		>"$LOGDIR/mock.log" 2>&1 &
	MOCK=$!
	for _ in $(seq 1 40); do
		[ -s "$LOGDIR/port" ] && break
		kill -0 "$MOCK" 2>/dev/null || {
			cat "$LOGDIR/mock.log" >&2
			echo "${0##*/}: Mock authority exited before publishing its port"
			return 40
		}
		sleep 0.25
	done
	PORT="$(cat "$LOGDIR/port" 2>/dev/null)"
	[ -n "$PORT" ] || {
		echo "${0##*/}: Mock authority did not publish a port"
		return 40
	}

	"$BACKEND" --verbose --debug-trust-certificate "localhost=$LOGDIR/mock.pem" \
		>"$LOGDIR/backend.log" 2>&1 &
	BE=$!
	xdp_wait_for_name org.freedesktop.impl.portal.desktop.webauth "$BE" || {
		tail -20 "$LOGDIR/backend.log" >&2
		echo "${0##*/}: Webauth backend did not acquire its bus name"
		return 40
	}

	# Only explicit user config entries can extend the authority allowlist for localhost.
	cat >"$LOGDIR/client.conf" <<EOF
[allow]
authorities = localhost:$PORT;

[testing]
trust_certificate = $LOGDIR/mock.pem
EOF

	local common=(--config "$LOGDIR/client.conf" --authority "localhost:$PORT"
		--tenant "$TENANT" --client-id a85cf173-4192-42f8-81fa-777a763e6e2c)
	[ -n "$VERBOSE" ] && common+=("$VERBOSE")

	echo
	echo "=== 1. login ==="
	"$HELPER" login "${common[@]}" \
		--scope "$SCOPE" --scope openid --scope profile --scope offline_access \
		>"$LOGDIR/login.out" 2>"$LOGDIR/login.err"
	local rc=$?
	sed 's/^/    /' "$LOGDIR/login.out" "$LOGDIR/login.err" 2>/dev/null | head -20
	check "Login exit 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check_grep "Login account label" "$LOGDIR/login.out" "^Signed in as $ACCOUNT\$"
	check_grep "Authorization PKCE S256" "$LOGDIR/events.log" \
		'GET authorize .*pkce=S256'
	check_grep "Authorization account selection" "$LOGDIR/events.log" \
		'GET authorize .*prompt=select_account'
	check_grep "Authorization code exchange" "$LOGDIR/events.log" 'POST token outcome=code_ok pop=no'
	check_grep "Portal completion" "$LOGDIR/backend.log" 'completed'

	echo
	echo "=== 2. accounts ==="
	"$HELPER" accounts --config "$LOGDIR/client.conf" >"$LOGDIR/accounts.out" 2>&1
	sed 's/^/    /' "$LOGDIR/accounts.out"
	check_grep "Stored account listing" "$LOGDIR/accounts.out" "^$ACCOUNT	localhost:$PORT	$TENANT\$"

	echo
	echo "=== 3. cached token ==="
	cp "$LOGDIR/events.log" "$LOGDIR/events-before-cache.log"
	"$HELPER" token "${common[@]}" --scope "$SCOPE" --scope openid --scope profile \
		--scope offline_access --account "$ACCOUNT" --prompt never \
		>"$LOGDIR/token.out" 2>"$LOGDIR/token.err"
	rc=$?
	check "Silent token exit 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check "Token JWT shape" \
		"$(grep -qE '^[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+\.[A-Za-z0-9_-]+$' "$LOGDIR/token.out" &&
			echo 0 || echo 1)"
	check "Cached token without authority request" \
		"$(cmp -s "$LOGDIR/events.log" "$LOGDIR/events-before-cache.log" && echo 0 || echo 1)"
	check "Single stdout line" \
		"$([ "$(wc -l <"$LOGDIR/token.out")" = 1 ] && echo 0 || echo 1)"

	echo
	echo "=== 4. PoP consent and code grant ==="
	"$HELPER" token "${common[@]}" --scope "$POP_SCOPE" --account "$ACCOUNT" \
		--req-cnf "$REQ_CNF" >"$LOGDIR/pop.out" 2>"$LOGDIR/pop.err"
	rc=$?
	sed 's/^/    /' "$LOGDIR/pop.err" | head -20
	check "PoP token exit 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check_grep "PoP refresh requires interaction" "$LOGDIR/events.log" \
		'POST token outcome=pop_refresh_interaction_required'
	check_grep "PoP interactive authorization" "$LOGDIR/events.log" \
		'GET authorize .*prompt=- '
	check_grep "Code exchange with req_cnf" "$LOGDIR/events.log" \
		'POST token outcome=code_ok pop=yes'
	check_grep "PoP request token_type" "$LOGDIR/events.log" \
		'POST token .*pop=yes token_type=pop'
	check_pop_binding "$LOGDIR/pop.out"

	echo
	echo "=== 5. uncached PoP token ==="
	cp "$LOGDIR/events.log" "$LOGDIR/events-before-second-pop.log"
	"$HELPER" token "${common[@]}" --scope "$POP_SCOPE" --account "$ACCOUNT" \
		--req-cnf "$REQ_CNF" --json >"$LOGDIR/pop2.out" 2>"$LOGDIR/pop2.err"
	rc=$?
	check "Second PoP token exit 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check "Second PoP authority request" \
		"$(cmp -s "$LOGDIR/events.log" "$LOGDIR/events-before-second-pop.log" && echo 1 || echo 0)"
	check_grep "JSON token type" "$LOGDIR/pop2.out" '"token_type" : "pop"'
	check "Response excludes refresh token" \
		"$(grep -q refresh_token "$LOGDIR/pop2.out" && echo 1 || echo 0)"
	# The mock requires interaction only for the first PoP refresh.
	check_grep "Second PoP refresh grant" "$LOGDIR/events.log" \
		'POST token outcome=refresh_ok pop=yes'

	echo
	echo "=== 6. logout and silent request ==="
	"$HELPER" logout --account "$ACCOUNT" --config "$LOGDIR/client.conf" \
		>"$LOGDIR/logout.out" 2>&1
	rc=$?
	sed 's/^/    /' "$LOGDIR/logout.out"
	check "Logout exit 0" "$([ "$rc" = 0 ] && echo 0 || echo 1)"
	check_grep "Logout account label" "$LOGDIR/logout.out" "^Removed $ACCOUNT\$"

	"$HELPER" accounts --config "$LOGDIR/client.conf" >"$LOGDIR/accounts-after.out" 2>&1
	check "Empty account listing" \
		"$([ ! -s "$LOGDIR/accounts-after.out" ] && echo 0 || echo 1)"

	"$HELPER" token "${common[@]}" --scope "$SCOPE" --prompt never \
		>"$LOGDIR/gone.out" 2>"$LOGDIR/gone.err"
	rc=$?
	check "Missing account exit 30" "$([ "$rc" = 30 ] && echo 0 || echo 1)"
	check "Empty stdout" "$([ ! -s "$LOGDIR/gone.out" ] && echo 0 || echo 1)"

	echo
	echo "=== 7. portal unavailable ==="
	# No session bus and no configured backend must both produce exit 40.
	env -u DBUS_SESSION_BUS_ADDRESS "$HELPER" login "${common[@]}" --scope "$SCOPE" \
		>"$LOGDIR/nobus.out" 2>"$LOGDIR/nobus.err"
	rc=$?
	check "Missing bus exit 40" "$([ "$rc" = 40 ] && echo 0 || echo 1)"
	check_grep "Diagnostic interface name" "$LOGDIR/nobus.err" \
		'org.freedesktop.portal.experimental.WebAuthentication1'
	check_grep "Diagnostic frontend opt-in" "$LOGDIR/nobus.err" \
		'XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=WebAuthentication1'

	echo
	echo "=== credential redaction ==="
	local leaked=0
	for f in "$LOGDIR/login.err" "$LOGDIR/token.err" "$LOGDIR/pop.err" "$LOGDIR/pop2.err"; do
		[ -s "$f" ] || continue
		if grep -qE 'code=|code_verifier|refresh_token|eyJ[A-Za-z0-9_-]{20}' "$f"; then
			echo "  FAIL  Credential found in $f"
			leaked=1
		fi
	done
	check "Credential redaction on stderr" "$leaked"

	if [ "$KEEP" = 1 ]; then
		echo
		echo "${0##*/}: --keep: Test stack running on $DBUS_SESSION_BUS_ADDRESS"
		echo "  authority  localhost:$PORT"
		echo "  config     $LOGDIR/client.conf"
		wait "$FE"
	fi

	kill "$BE" "$FE" "$MOCK" "$PERM" 2>/dev/null
	sleep 1

	echo
	echo "runs: $((pass + fail))  ok: $pass  failed: $fail"
	[ "$fail" = 0 ] || return 1
	return 0
}

"$XVFB" "$SCREEN" -screen 0 1280x1024x24 -nolisten tcp >"$LOGDIR/xvfb.log" 2>&1 &
XVFB_PID=$!
sleep 2

cleanup() {
	kill "$XVFB_PID" 2>/dev/null
	wait "$XVFB_PID" 2>/dev/null
}
trap cleanup EXIT

export DISPLAY="$SCREEN"
unset WAYLAND_DISPLAY
export GDK_BACKEND=x11
export GTK_A11Y=none
export ADW_DEBUG_COLOR_SCHEME="${ADW_DEBUG_COLOR_SCHEME:-prefer-dark}"

export XDG_DESKTOP_PORTAL_DIR="$DEVDIR"
export XDG_CURRENT_DESKTOP="${XDG_CURRENT_DESKTOP:-dev}"
export XDG_DATA_HOME="$LOGDIR/data-home"
mkdir -p "$XDG_DATA_HOME"
export G_MESSAGES_DEBUG="webauth entra"

# The fixture selects libsecret's file backend and supplies its own keyring password.
export SECRET_BACKEND=file
export SECRET_FILE_TEST_PATH="$LOGDIR/keyring"
export SECRET_FILE_TEST_PASSWORD="entra-e2e-fixture"
rm -f "$SECRET_FILE_TEST_PATH"

export LOGDIR REPO XDP_BUILD WEBAUTH_REPO BACKEND HELPER TENANT ACCOUNT SCOPE POP_SCOPE REQ_CNF KEEP VERBOSE
export pass fail

dbus-run-session -- bash -c "
	$(declare -f xdp_wait_for_name)
	$(declare -f check)
	$(declare -f check_grep)
	$(declare -f check_count)
	$(declare -f check_pop_binding)
	$(declare -f inner)
	inner"
rc=$?

echo
if [ "$rc" = 0 ]; then
	echo "entra-e2e: PASS; logs in $LOGDIR"
else
	echo "entra-e2e: FAIL ($rc); logs in $LOGDIR"
fi
exit "$rc"
