# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# The portal is a runtime D-Bus dependency; builds are independent of its frontend and backend.

project(
  'entra-token-client',
  'c',
  version: '0.1.0',
  license: 'LGPL-2.1-or-later',
  meson_version: '>= 0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
    'werror=false',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments([
    '-Wshadow',
    '-Wpointer-arith',
    '-Wstrict-prototypes',
    '-Wmissing-prototypes',
    '-Wwrite-strings',
    '-Wformat=2',
    '-Wno-unused-parameter',
    '-Wno-declaration-after-statement',
  ]),
  language: 'c',
)

glib_dep = dependency('glib-2.0', version: '>= 2.66')
gobject_dep = dependency('gobject-2.0', version: '>= 2.66')
gio_dep = dependency('gio-2.0', version: '>= 2.66')

soup_dep = dependency('libsoup-3.0', version: '>= 3.0')
json_dep = dependency('json-glib-1.0', version: '>= 1.6')

# Credentials are stored through libsecret; see docs/SECURITY.md.
secret_dep = dependency('libsecret-1', version: '>= 0.20')

entra_deps = [glib_dep, gobject_dep, gio_dep, soup_dep, json_dep, secret_dep]

# --verbose applies only to the client log domain.
entra_c_args = [
  '-DG_LOG_DOMAIN="entra"',
  '-DENTRA_VERSION="' + meson.project_version() + '"',
]

# Core sources include I/O; unit tests exercise their local parsing and validation paths.
entra_core_sources = files(
  'src/cache/keyring.c',
  'src/entra-config.c',
  'src/entra-error.c',
  'src/ipc/request.c',
  'src/log/redact.c',
  'src/oauth/callback.c',
  'src/oauth/clouds.c',
  'src/oauth/discovery.c',
  'src/oauth/http.c',
  'src/oauth/jwt.c',
  'src/oauth/token.c',
  'src/oauth/transaction.c',
)

entra_core = static_library(
  'entra-core',
  entra_core_sources,
  c_args: entra_c_args,
  dependencies: entra_deps,
  install: false,
)

entra_core_dep = declare_dependency(
  link_with: entra_core,
  include_directories: include_directories('src'),
  dependencies: entra_deps,
)

executable(
  'entra-token-helper',
  sources: ['src/main.c', 'src/acquire.c', 'src/webauth_client.c'],
  c_args: entra_c_args,
  dependencies: [entra_core_dep],
  install: true,
)

subdir('tests')

summary(
  {
    'glib-2.0': glib_dep.version(),
    'gio-2.0': gio_dep.version(),
    'libsoup-3.0 (token endpoint)': soup_dep.version(),
    'json-glib-1.0': json_dep.version(),
    'libsecret-1 (account store)': secret_dep.version(),
  },
  section: 'entra-token-client dependencies',
)

summary(
  {
    'public interface called': 'org.freedesktop.portal.experimental.WebAuthentication1',
    'on': 'org.freedesktop.portal.Desktop',
    'object path': '/org/freedesktop/portal/desktop',
    'backends known to this program': 'none',
    'redirect (both clouds)': 'https://login.microsoftonline.com/common/oauth2/nativeclient',
    'account store': 'Secret Service, schema io.github.sjtrotter.entra-token-helper',
  },
  section: 'entra-token-client interfaces',
)

summary(
  {
    'login, token, accounts, logout': true,
    'proof-of-possession tokens (--req-cnf)': true,
    'identity provider support': true,
  },
  section: 'status',
  bool_yn: true,
)
