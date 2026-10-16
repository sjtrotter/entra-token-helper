/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <string.h>

#include <glib.h>

#include "log/redact.h"

#define TOKEN "eyJ0eXAiOiJKV1QiLCJhbGciOiJSUzI1NiJ9.PAYLOAD.SIGNATURE"
#define CODE "1.CwMAjbExg4ct70ijX6yIGOv5tHPxXKiSQfhCgfp3enY"
#define ACCOUNT "stephen.trotter@us.af.mil"
#define DESCRIPTION "AADSTS50173: The provided grant has expired for stephen.trotter@us.af.mil"

static void test_a_token_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_TOKEN, TOKEN);

	g_assert_null(strstr(rendered, "eyJ"));
	g_assert_null(strstr(rendered, "PAYLOAD"));
	g_assert_true(g_str_has_prefix(rendered, "<token:"));
}

static void test_a_code_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_CODE, CODE);

	g_assert_null(strstr(rendered, "CwMA"));
	g_assert_true(g_str_has_prefix(rendered, "<code:"));
}

static void test_an_account_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_ACCOUNT, ACCOUNT);

	g_assert_null(strstr(rendered, "stephen"));
	g_assert_null(strstr(rendered, "us.af.mil"));
	g_assert_true(g_str_has_prefix(rendered, "<account:"));
}

static void test_a_description_is_never_rendered(void)
{
	g_autofree char* rendered = entra_redact_field(ENTRA_FIELD_DESCRIPTION, DESCRIPTION);

	g_assert_cmpstr(rendered, ==, "<description>");
}

static void test_a_tenant_is_a_hash(void)
{
	g_autofree char* one = entra_redact_field(ENTRA_FIELD_TENANT,
	                                          "8331b18d-2d87-48ef-a35f-ac8818ebf9b4");
	g_autofree char* same = entra_redact_field(ENTRA_FIELD_TENANT,
	                                           "8331b18d-2d87-48ef-a35f-ac8818ebf9b4");
	g_autofree char* other = entra_redact_field(ENTRA_FIELD_TENANT, "common");

	g_assert_null(strstr(one, "8331b18d"));
	g_assert_cmpstr(one, ==, same);
	g_assert_cmpstr(one, !=, other);
}

static void test_the_loggable_fields_are_loggable(void)
{
	g_autofree char* authority = entra_redact_field(ENTRA_FIELD_AUTHORITY,
	                                                "login.microsoftonline.us");
	g_autofree char* code = entra_redact_field(ENTRA_FIELD_ERROR_CODE, "invalid_grant");
	g_autofree char* outcome = entra_redact_field(ENTRA_FIELD_OUTCOME, "CODE");

	g_assert_cmpstr(authority, ==, "login.microsoftonline.us");
	g_assert_cmpstr(code, ==, "invalid_grant");
	g_assert_cmpstr(outcome, ==, "CODE");
}

static void test_error_text_is_cut_before_a_uri(void)
{
	g_autofree char* cut = entra_redact_error_text(
	    "TLS handshake failed for https://login.microsoftonline.us/t/oauth2/v2.0/token?code=x");
	g_autofree char* nothing = entra_redact_error_text(NULL);

	g_assert_null(strstr(cut, "login.microsoftonline.us"));
	g_assert_null(strstr(cut, "code="));
	g_assert_true(g_str_has_prefix(cut, "TLS handshake failed"));
	g_assert_cmpstr(nothing, ==, "(no message)");
}

static void test_verbose_is_a_frequency_not_a_permission(void)
{
	g_assert_false(entra_log_get_verbose());
	entra_log_set_verbose(TRUE);
	g_assert_true(entra_log_get_verbose());
	entra_log_set_verbose(FALSE);
}

static void test_logs_use_stderr(gconstpointer data)
{
	const char* debug_domains = data;
	g_autofree char* saved_domains = g_strdup(g_getenv("G_MESSAGES_DEBUG"));
	g_autofree char* saved_invocation = g_strdup(g_getenv("DEBUG_INVOCATION"));

	if (g_test_subprocess())
	{
		entra_log_init();
		g_log_set_default_handler(g_log_default_handler, NULL);
		g_log_set_always_fatal(G_LOG_FATAL_MASK);
		entra_log_set_verbose(FALSE);
		entra_log_event(G_LOG_LEVEL_DEBUG, "verbose-disabled", NULL);
		entra_log_set_verbose(TRUE);
		entra_log_event(G_LOG_LEVEL_DEBUG, "debug-event", "token", ENTRA_FIELD_TOKEN, TOKEN,
		                "account", ENTRA_FIELD_ACCOUNT, ACCOUNT, NULL);
		entra_log_event(G_LOG_LEVEL_INFO, "info-event", NULL);
		g_log_structured("dependency", G_LOG_LEVEL_DEBUG, "MESSAGE", "dependency-debug");
		g_log_structured("dependency", G_LOG_LEVEL_INFO, "MESSAGE", "dependency-info");
		entra_log_event(G_LOG_LEVEL_MESSAGE, "message-event", NULL);
		entra_log_event(G_LOG_LEVEL_WARNING, "warning-event", NULL);
		entra_log_event(G_LOG_LEVEL_CRITICAL, "critical-event", NULL);
		return;
	}

	g_setenv("G_MESSAGES_DEBUG", debug_domains, TRUE);
	g_unsetenv("DEBUG_INVOCATION");
	g_test_trap_subprocess(NULL, 5 * G_USEC_PER_SEC, G_TEST_SUBPROCESS_DEFAULT);
	if (saved_domains != NULL)
		g_setenv("G_MESSAGES_DEBUG", saved_domains, TRUE);
	else
		g_unsetenv("G_MESSAGES_DEBUG");
	if (saved_invocation != NULL)
		g_setenv("DEBUG_INVOCATION", saved_invocation, TRUE);

	g_test_trap_assert_passed();
	g_test_trap_assert_stdout("");
	g_test_trap_assert_stderr_unmatched("*verbose-disabled*");
	g_test_trap_assert_stderr_unmatched("*" TOKEN "*");
	g_test_trap_assert_stderr_unmatched("*" ACCOUNT "*");
	g_test_trap_assert_stderr("*message-event*warning-event*critical-event*");
	if (g_strcmp0(debug_domains, "all") == 0 || g_strcmp0(debug_domains, "entra") == 0)
		g_test_trap_assert_stderr("*debug-event token=<token:* account=<account:*info-event*");
	else
	{
		g_test_trap_assert_stderr_unmatched("*debug-event*");
		g_test_trap_assert_stderr_unmatched("*info-event*");
	}
	if (g_strcmp0(debug_domains, "all") == 0 || g_strcmp0(debug_domains, "dependency") == 0)
		g_test_trap_assert_stderr("*dependency-debug*dependency-info*");
	else
		g_test_trap_assert_stderr_unmatched("*dependency-*");
}

int main(int argc, char** argv)
{
	g_test_init(&argc, &argv, NULL);

	g_test_add_func("/redact/token", test_a_token_is_never_rendered);
	g_test_add_func("/redact/code", test_a_code_is_never_rendered);
	g_test_add_func("/redact/account", test_an_account_is_never_rendered);
	g_test_add_func("/redact/description", test_a_description_is_never_rendered);
	g_test_add_func("/redact/tenant", test_a_tenant_is_a_hash);
	g_test_add_func("/redact/public-fields-preserved", test_the_loggable_fields_are_loggable);
	g_test_add_func("/redact/error-text", test_error_text_is_cut_before_a_uri);
	g_test_add_func("/redact/verbose-toggle", test_verbose_is_a_frequency_not_a_permission);
	g_test_add_data_func("/redact/log-stderr/all", "all", test_logs_use_stderr);
	g_test_add_data_func("/redact/log-stderr/domain", "entra", test_logs_use_stderr);
	g_test_add_data_func("/redact/log-stderr/other-domain", "dependency", test_logs_use_stderr);
	g_test_add_data_func("/redact/log-stderr/filtered", "", test_logs_use_stderr);

	return g_test_run();
}
