#!/bin/bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

XDP_FIXTURE_MARKER=".xdg-desktop-portal-webauth-fixture"

lib_die() {
	echo "${0##*/}: $*" >&2
	exit 40
}

fixture_tmp_root() {
	local root="${TMPDIR:-/tmp}"

	root="$(realpath -m -- "$root")" || lib_die "TMPDIR resolution failed: '$root'"
	printf '%s' "${root%/}"
}

fixture_no_symlinks() {
	local path="$1" lexical real

	lexical="$(realpath -m -s -- "$path")" || return 1
	real="$(realpath -e -- "$path" 2>/dev/null)" || return 1
	[ "$lexical" = "$real" ]
}

# TMPDIR is operator-selected; containment guards against accidental writes outside it.
fixture_under_tmp() {
	local path real root

	path="$1"
	root="$(fixture_tmp_root)"
	real="$(realpath -m -- "$path")" || return 1

	case "$real" in
	"$root"/?*) return 0 ;;
	*) return 1 ;;
	esac
}

# Ancestor checks prevent other users from replacing directories on the fixture path.
fixture_dir_safe() {
	local path="$1" owner mode sticky group other

	owner="$(stat -c %u -- "$path" 2>/dev/null)" || return 1
	[ "$owner" = "$(id -u)" ] || [ "$owner" = 0 ] || return 1

	mode="$(stat -c %04a -- "$path" 2>/dev/null)" || return 1
	sticky=$((0${mode:0:1} & 1))
	group=$((0${mode:2:1} & 2))
	other=$((0${mode:3:1} & 2))

	if [ "$group" -ne 0 ] || [ "$other" -ne 0 ]; then
		[ "$sticky" -eq 1 ] || return 1
	fi

	return 0
}

fixture_ancestors_ok() {
	local real="$1" root path

	root="$(fixture_tmp_root)"
	fixture_dir_safe "$root" ||
		lib_die "Temporary root $root requires user or root ownership and a sticky bit when writable by others"

	path="$(dirname -- "$real")"
	while [ "$path" != "$root" ] && [ "$path" != "/" ]; do
		fixture_dir_safe "$path" ||
			lib_die "Fixture ancestor $path requires user or root ownership and a sticky bit when writable by others"
		path="$(dirname -- "$path")"
	done
}

# Only a directory just created by this script may receive a fixture marker.
fixture_claim() {
	local dir="$1" tag="$2"

	(umask 177 && printf '%s\n' "$tag" >"$dir/$XDP_FIXTURE_MARKER") ||
		lib_die "Fixture marker write failed in $dir"
	chmod 600 "$dir/$XDP_FIXTURE_MARKER" || lib_die "Fixture marker permission change failed in $dir"
}

fixture_refuse() {
	lib_die "$2
Disposable fixtures can be removed manually:
    rm -rf -- '$1'"
}

# Reuse and deletion require an owned private directory and a mode 0600 marker with the expected tag.
fixture_check() {
	local dir="$1" tag="$2" real marker owner mode claimed kind

	[ -L "$dir" ] && lib_die "Fixture path is a symlink: $dir"
	[ -d "$dir" ] || lib_die "Fixture path is not a directory: $dir"

	fixture_no_symlinks "$dir" ||
		lib_die "Fixture path contains a symlink: $dir"

	fixture_under_tmp "$dir" ||
		lib_die "Fixture path $dir must be inside ${TMPDIR:-/tmp}; set TMPDIR to choose another temporary root"

	real="$(realpath -e -- "$dir")" || lib_die "Fixture path resolution failed: $dir"
	fixture_ancestors_ok "$real"

	owner="$(stat -c %u -- "$real")"
	[ "$owner" = "$(id -u)" ] ||
		fixture_refuse "$real" "Fixture $real requires uid $(id -u); owner is uid $owner"

	marker="$real/$XDP_FIXTURE_MARKER"

	[ -L "$marker" ] &&
		fixture_refuse "$real" "Fixture marker is a symlink in $real"
	kind="$(stat -c %F -- "$marker" 2>/dev/null)" || kind=""
	[ "$kind" = "regular file" ] ||
		fixture_refuse "$real" "Fixture marker $real/$XDP_FIXTURE_MARKER must be a regular file"

	owner="$(stat -c %u -- "$marker")"
	[ "$owner" = "$(id -u)" ] ||
		fixture_refuse "$real" "Fixture marker in $real belongs to uid $owner; current user ownership is required"

	mode="$(stat -c %a -- "$marker")"
	[ "$mode" = "600" ] ||
		fixture_refuse "$real" "Fixture marker in $real has mode $mode; mode 0600 is required"

	claimed="$(cat -- "$marker" 2>/dev/null)"
	[ "$claimed" = "$tag" ] ||
		fixture_refuse "$real" "Fixture $real has tag '$claimed'; expected '$tag'"

	# Fixture permissions are validated without repair because exposed markers may be forged.
	mode="$(stat -c %a -- "$real")"
	case "$mode" in
	700 | 500) ;;
	*)
		fixture_refuse "$real" "Fixture $real has mode $mode; modes 0700 and 0500 are accepted"
		;;
	esac
}

# Exclusive creation with umask 077 prevents adopting another user's directory or exposing the fixture.
fixture_create() {
	local dir="$1" tag="$2"

	fixture_under_tmp "$dir" ||
		lib_die "Fixture path $dir must be inside ${TMPDIR:-/tmp}"

	(umask 077 && mkdir -- "$dir") || lib_die "Fixture directory creation failed: $dir"
	fixture_claim "$dir" "$tag"
}

fixture_make() {
	local dir="$1" tag="$2"

	[ -e "$dir" ] || [ -L "$dir" ] || fixture_create "$dir" "$tag"
	fixture_check "$dir" "$tag"
}

# Deletion uses the checked real path and must not cross into mounted filesystems.
fixture_remove() {
	local dir="$1" tag="$2" real

	[ -e "$dir" ] || [ -L "$dir" ] || return 0
	fixture_check "$dir" "$tag"

	real="$(realpath -e -- "$dir")" || lib_die "Fixture path resolution failed: $dir"
	rm -rf --one-file-system -- "$real" || lib_die "Fixture directory removal failed: $real"
}

fixture_mktemp() {
	local prefix="$1" tag="$2" dir

	dir="$(mktemp -d "${TMPDIR:-/tmp}/$prefix.XXXXXXXX")" || lib_die "Temporary fixture creation failed"
	fixture_claim "$dir" "$tag"
	printf '%s' "$dir"
}


xdp_enable_experimental() {
	local name

	for name in WebAuthentication1 Certificate1; do
		case ",${XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL:-}," in
		*,"$name",*) ;;
		*) XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL="${XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL:+$XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL,}$name" ;;
		esac
	done
	export XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL
}

# The frontend searches XDG_DATA_HOME before XDG_DATA_DIRS.
xdp_data_dirs() {
	local dirs

	printf '%s\n' "${XDG_DATA_HOME:-$HOME/.local/share}"
	dirs="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
	printf '%s\n' "$dirs" | tr ':' '\n'
}

xdp_desktops() {
	printf '%s\n' "${XDG_CURRENT_DESKTOP:-}" | tr ':' '\n' | tr '[:upper:]' '[:lower:]' |
		grep -v '^$'
}

# Within each directory, the first matching desktop config wins; portals.conf is the fallback.
xdp_config_chain() {
	local dir desktop found

	for dir in "${XDG_CONFIG_HOME:-$HOME/.config}/xdg-desktop-portal" \
		$(printf '%s\n' "${XDG_CONFIG_DIRS:-/etc/xdg}" | tr ':' '\n' | sed 's:$:/xdg-desktop-portal:') \
		/etc/xdg-desktop-portal \
		$(xdp_data_dirs | sed 's:$:/xdg-desktop-portal:') \
		/usr/share/xdg-desktop-portal; do
		found=""

		while read -r desktop; do
			if [ -f "$dir/$desktop-portals.conf" ]; then
				found="$dir/$desktop-portals.conf"
				break
			fi
		done < <(xdp_desktops)

		[ -z "$found" ] && [ -f "$dir/portals.conf" ] && found="$dir/portals.conf"
		[ -n "$found" ] && printf '%s\n' "$found"
	done | awk '!seen[$0]++'
}

# Resolve interfaces across the full chain using each file's interface candidates then defaults, or defaults alone for absent interfaces; an applicable none ends the chain and the first installed candidate wins.
xdp_flatten_preferred() {
	[ $# -gt 0 ] || return 0

	awk '
	function trim(s) { gsub(/^[ \t]+|[ \t]+$/, "", s); return s }
	function has_none(s,   n, a, i) {
		n = split(s, a, ";")
		for (i = 1; i <= n; i++) if (trim(a[i]) == "none") return 1
		return 0
	}
	function add(item) {
		if (item == "" || item == "none" || (item in used)) return
		used[item] = 1
		out = (out == "" ? item : out ";" item)
	}
	function addlist(s,   n, a, i) {
		n = split(s, a, ";")
		for (i = 1; i <= n; i++) add(trim(a[i]))
	}
	function terminate() { out = (out == "" ? "none" : out ";none"); return out }
	function flatten(key,   i, il, dl) {
		delete used
		out = ""
		for (i = 1; i <= nf; i++) {
			il = ((i SUBSEP key) in vals) ? vals[i, key] : NOKEY
			dl = ((i SUBSEP "default") in vals) ? vals[i, "default"] : NOKEY
			if (il != NOKEY) {
				if (has_none(il)) return terminate()
				addlist(il)
				if (dl != NOKEY) addlist(dl)
			} else if (dl != NOKEY) {
				if (has_none(dl)) return terminate()
				addlist(dl)
			}
		}
		return out
	}
	BEGIN { NOKEY = "\001none-such\001"; nf = 0; nk = 0 }
	FNR == 1 { nf++; section = "" }
	{
		line = trim($0)
		if (line ~ /^[#;]/ || line == "") next
		if (line ~ /^\[.*\]$/) { section = substr(line, 2, length(line) - 2); next }
		if (section != "preferred") next
		eq = index(line, "=")
		if (eq == 0) next
		key = trim(substr(line, 1, eq - 1))
		if (!(key in seen)) { seen[key] = 1; allkeys[++nk] = key }
		vals[nf, key] = trim(substr(line, eq + 1))
	}
	END {
		print "[preferred]"
		for (i = 1; i <= nk; i++) {
			if (allkeys[i] == "default") continue
			r = flatten(allkeys[i])
			if (r != "") printf "%s=%s;\n", allkeys[i], r
		}
		r = flatten("default")
		if (r != "") printf "default=%s;\n", r
	}
	' "$@"
}

# Generated config has one preferred section; each key must occur once.
xdp_conf_set() {
	local file="$1" key="$2" value="$3" tmp="$1.tmp"

	awk -v k="$key" 'index($0, k "=") != 1' "$file" >"$tmp" || lib_die "Portal config rewrite failed: $file"
	printf '%s=%s\n' "$key" "$value" >>"$tmp"
	mv -f -- "$tmp" "$file" || lib_die "Portal config replacement failed: $file"
}

# XDG_DESKTOP_PORTAL_DIR replaces all portal and config search paths, so preserve installed portals.
xdp_write_portal_dir() {
	local dir="$1" repo="$2" mode="${3:-private}" source_dir base found
	local chain=()

	for source_dir in $(xdp_data_dirs | sed 's:$:/xdg-desktop-portal/portals:'); do
		[ -d "$source_dir" ] || continue

		for found in "$source_dir"/*.portal; do
			[ -f "$found" ] || continue
			base="$(basename "$found")"
			# The frontend uses the first installed portal with this filename.
			[ -e "$dir/$base" ] && continue
			ln -s -- "$found" "$dir/$base"
		done
	done

	# The fixture portal must override any installed copy.
	rm -f -- "$dir/webauth.portal"
	sed -e '/^#/d' -e '/^$/d' "$repo/data/webauth.portal.in" >"$dir/webauth.portal"

	mapfile -t chain < <(xdp_config_chain)

	# The generated portals.conf fallback applies to every XDG_CURRENT_DESKTOP value.
	if [ "${#chain[@]}" -gt 0 ]; then
		xdp_flatten_preferred "${chain[@]}" >"$dir/portals.conf"
		echo "${0##*/}: Portal config flattened from ${#chain[@]} files" >&2
		printf '%s: chain %s\n' "${0##*/}" "${chain[@]}" >&2
	else
		printf '[preferred]\ndefault=none;\n' >"$dir/portals.conf"
		echo "${0##*/}: Portal config missing; using default=none" >&2
	fi

	xdp_conf_set "$dir/portals.conf" \
		org.freedesktop.impl.portal.experimental.WebAuthentication1 'webauth;'

	# Disabling Secret on a private bus avoids a 25-second keyring activation timeout.
	if [ "$mode" != live ]; then
		xdp_conf_set "$dir/portals.conf" org.freedesktop.impl.portal.Secret 'none;'
	fi
}

xdp_wait_for_name() {
	local name="$1" pid="$2" owned=""

	for _ in $(seq 1 120); do
		owned="$(gdbus call --session --dest org.freedesktop.DBus \
			--object-path /org/freedesktop/DBus \
			--method org.freedesktop.DBus.NameHasOwner "$name" 2>/dev/null)"
		case "$owned" in
		*true*) return 0 ;;
		esac

		kill -0 "$pid" 2>/dev/null || return 1
		sleep 0.25
	done

	return 1
}
