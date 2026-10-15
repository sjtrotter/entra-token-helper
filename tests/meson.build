# SPDX-License-Identifier: LGPL-2.1-or-later
# SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>

# Unit tests use local fixtures; tools/entra-e2e.sh covers interactive sign-in.
entra_unit_tests = {
  'pkce': 'test-pkce.c',
  'callback': 'test-callback.c',
  'jwt': 'test-jwt.c',
  'discovery': 'test-discovery.c',
  'cache': 'test-cache.c',
  'redact': 'test-redact.c',
}

foreach name, source : entra_unit_tests
  test(
    name,
    executable(
      'entra-test-' + name,
      source,
      c_args: entra_c_args,
      dependencies: [entra_core_dep],
      install: false,
    ),
    suite: 'unit',
    protocol: 'tap',
    args: ['--tap'],
  )
endforeach
