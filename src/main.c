/* SPDX-License-Identifier: LGPL-2.1-or-later
 * SPDX-FileCopyrightText: 2026 Stephen J. Trotter <stephen.j.trotter@gmail.com>
 */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <glib.h>

#include "acquire.h"
#include "cache/keyring.h"
#include "entra-config.h"
#include "entra-error.h"
#include "ipc/request.h"
#include "log/redact.h"
#include "oauth/clouds.h"
#include "webauth_client.h"

typedef enum
{
	ETH_VERB_NONE,
	ETH_VERB_LOGIN,
	ETH_VERB_TOKEN,
	ETH_VERB_ACCOUNTS,
	ETH_VERB_LOGOUT
} EthVerb;

typedef struct
{
	EthVerb verb;
	const char* authority;
	const char* tenant;
	const char* cloud;
	const char* client_id;
	GPtrArray* scopes; /* Elements are borrowed from argv. */
	const char* req_cnf;
	const char* account;
	const char* config;
	const char* parent_window;
	const char* session_mode;
	const char* format;
	const char* timeout;
	EntraPrompt prompt;
	gboolean json;
	gboolean verbose;
} EthRequest;

#define ETH_DEFAULT_TIMEOUT_SECONDS 300

static void eth_usage(FILE* out)
{
	fprintf(out,
	        "entra-token-helper - Entra ID sign-in helper for Azure Virtual Desktop\n"
	        "\n"
	        "Usage: entra-token-helper <verb> [options]\n"
	        "\n"
	        "Verbs:\n"
	        "  login       sign in and store an account\n"
	        "  token       acquire an access token (PoP with --req-cnf)\n"
	        "  accounts    list stored accounts\n"
	        "  logout      forget stored accounts matching --account, or all accounts\n"
	        "\n"
	        "Options:\n"
	        "  --authority <a>        authority host or HTTPS URL with an optional tenant\n"
	        "  --tenant <id>          tenant identifier [default: URL tenant or common]\n"
	        "  --cloud <name>         commercial | usgov; sets authority and default AVD scopes\n"
	        "  --client-id <guid>     public client ID [default: " ENTRA_AVD_CLIENT_ID "]\n"
	        "  --scope <scope>        repeatable; accepts whitespace-separated scopes\n"
	        "  --req-cnf <b64url>     caller's confirmation object for a PoP token request\n"
	        "  --account <id>         account label\n"
	        "  --prompt <policy>      auto (default) | always | never\n"
	        "  --parent-window <h>    caller's window handle for the portal\n"
	        "  --session-mode <m>     shared | ephemeral\n"
	        "  --format <f>           raw (default) | json\n"
	        "  --json                 alias for --format json\n"
	        "  --timeout <seconds>    sign-in window timeout [default: 300]\n"
	        "  --config <path>        config path [default: ENTRA_TOKEN_HELPER_CONFIG or XDG]\n"
	        "  --verbose              enable client debug logs on stderr\n"
	        "  -h, --help             show usage\n"
	        "  -V, --version          show version\n"
	        "\n"
	        "Exit codes:\n"
	        "   0 success                   30 no account or multiple matches\n"
	        "  10 interaction required      40 provider unavailable (portal, keyring or authority)\n"
	        "  20 cancelled                 50 authorization server error\n"
	        "  64 usage error               70 internal error\n"
	        "\n"
	        "Redirect URI for both clouds:\n"
	        "  " ENTRA_NATIVECLIENT_REDIRECT "\n"
	        "  The AVD registration requires this URI in both clouds.\n"
	        "\n"
	        "Interactive portal:\n"
	        "  " ENTRA_PORTAL_INTERFACE "\n"
	        "  Bus: " ENTRA_PORTAL_BUS_NAME "\n"
	        "  Path: " ENTRA_PORTAL_OBJECT_PATH "\n"
	        "  The frontend requires a configured backend and\n"
	        "  XDG_DESKTOP_PORTAL_ENABLE_EXPERIMENTAL=WebAuthentication1.\n");
}

static EthVerb eth_verb_from_string(const char* s)
{
	if (g_strcmp0(s, "login") == 0)
		return ETH_VERB_LOGIN;
	if (g_strcmp0(s, "token") == 0)
		return ETH_VERB_TOKEN;
	if (g_strcmp0(s, "accounts") == 0)
		return ETH_VERB_ACCOUNTS;
	if (g_strcmp0(s, "logout") == 0)
		return ETH_VERB_LOGOUT;
	return ETH_VERB_NONE;
}

static const char* eth_verb_to_string(EthVerb verb)
{
	switch (verb)
	{
		case ETH_VERB_LOGIN:
			return "login";
		case ETH_VERB_TOKEN:
			return "token";
		case ETH_VERB_ACCOUNTS:
			return "accounts";
		case ETH_VERB_LOGOUT:
			return "logout";
		case ETH_VERB_NONE:
		default:
			return "none";
	}
}

/* Usage failures leave stdout empty. */
static int eth_usage_error(const char* fmt, ...) G_GNUC_PRINTF(1, 2);

static int eth_usage_error(const char* fmt, ...)
{
	va_list ap;

	fputs("entra-token-helper: ", stderr);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	fputs("Try 'entra-token-helper --help'.\n", stderr);

	return entra_status_exit_code(ENTRA_STATUS_USAGE);
}

static gboolean eth_take_value(int argc, char** argv, int* i, const char* name, const char* eq,
                               const char** out)
{
	if (*out != NULL)
	{
		(void)eth_usage_error("%s appears more than once", name);
		return FALSE;
	}

	if (eq != NULL)
	{
		if (eq[1] == '\0')
		{
			(void)eth_usage_error("%s requires a value", name);
			return FALSE;
		}
		*out = &eq[1];
		return TRUE;
	}

	if (*i + 1 >= argc)
	{
		(void)eth_usage_error("%s requires a value", name);
		return FALSE;
	}

	*i += 1;
	*out = argv[*i];
	return TRUE;
}

/* Errors must be redacted before reaching this output path. */
static int eth_fail(const EthRequest* request, GError* error)
{
	EntraStatus status = entra_status_from_error(error);

	if (request->json)
	{
		g_autofree char* json = entra_response_error_json(status, entra_status_symbol(status),
		                                                  error != NULL ? error->message : "failed");

		fprintf(stderr, "%s\n", json);
	}
	else
	{
		fprintf(stderr, "entra-token-helper: %s\n", error != NULL ? error->message : "failed");
	}

	return entra_status_exit_code(status);
}

static gboolean eth_build_acquire(const EthRequest* request, EntraAcquire* acquire,
                                  EntraConfig* config, GError** error)
{
	const EntraCloud* cloud = entra_cloud_by_name(request->cloud);
	const char* authority = request->authority;
	g_autoptr(GPtrArray) scopes = g_ptr_array_new();

	if (request->cloud != NULL && cloud == NULL)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
		                    "--cloud takes commercial or usgov");
		return FALSE;
	}

	if (authority == NULL && cloud != NULL)
		authority = cloud->authority;

	if (!entra_authority_parse(authority, request->tenant, &acquire->authority, error))
		return FALSE;

	if (cloud != NULL && entra_cloud_for_authority(acquire->authority.host) != cloud)
	{
		g_set_error_literal(error, ENTRA_ERROR, ENTRA_ERROR_USAGE,
		                    "--cloud and --authority name different clouds");
		return FALSE;
	}

	for (guint i = 0; i < request->scopes->len; i++)
		g_ptr_array_add(scopes, g_ptr_array_index(request->scopes, i));

	if (scopes->len == 0 && cloud != NULL)
	{
		g_ptr_array_add(scopes, (gpointer)cloud->avd_scope);
		g_ptr_array_add(scopes, (gpointer) "openid");
		g_ptr_array_add(scopes, (gpointer) "profile");
		g_ptr_array_add(scopes, (gpointer) "offline_access");
	}

	g_ptr_array_add(scopes, NULL);

	acquire->client_id = request->client_id != NULL ? request->client_id : ENTRA_AVD_CLIENT_ID;
	acquire->scopes = entra_scopes_split((const char* const*)scopes->pdata);
	acquire->account = request->account;
	acquire->req_cnf = request->req_cnf;
	acquire->parent_window = request->parent_window;
	acquire->session_mode = request->session_mode;
	acquire->prompt = request->prompt;
	acquire->config = config;
	acquire->timeout_seconds =
	    request->timeout != NULL ? (guint)g_ascii_strtoull(request->timeout, NULL, 10)
	                             : ETH_DEFAULT_TIMEOUT_SECONDS;

	return entra_acquire_prepare(acquire, error);
}

static int eth_do_login(const EthRequest* request, EntraConfig* config)
{
	EntraAcquire acquire = { 0 };
	g_autoptr(GError) error = NULL;
	g_autofree char* account = NULL;
	int rc;

	if (!eth_build_acquire(request, &acquire, config, &error) ||
	    !entra_acquire_login(&acquire, &account, &error))
	{
		rc = eth_fail(request, error);
		entra_acquire_clear(&acquire);
		return rc;
	}

	if (request->json)
	{
		g_autoptr(GPtrArray) records = g_ptr_array_new_with_free_func(
		    (GDestroyNotify)entra_account_record_free);
		EntraAccountRecord* record = entra_account_record_new();
		g_autofree char* json = NULL;

		record->account = g_strdup(account);
		record->host = g_strdup(acquire.authority.host);
		record->tenant = g_strdup(acquire.authority.tenant);
		record->client_id = g_strdup(acquire.client_id);
		g_ptr_array_add(records, record);

		json = entra_response_accounts_json(records);
		printf("%s\n", json);
	}
	else
	{
		printf("Signed in as %s\n", account);
	}

	entra_acquire_clear(&acquire);
	return 0;
}

static int eth_do_token(const EthRequest* request, EntraConfig* config)
{
	EntraAcquire acquire = { 0 };
	EntraTokenResult result = { 0 };
	g_autoptr(GError) error = NULL;
	int rc = 0;

	if (!eth_build_acquire(request, &acquire, config, &error) ||
	    !entra_acquire_token(&acquire, &result, &error))
	{
		rc = eth_fail(request, error);
		entra_token_result_clear(&result);
		entra_acquire_clear(&acquire);
		return rc;
	}

	if (request->json)
	{
		g_autofree char* json = entra_response_token_json(result.access_token, result.token_type,
		                                                  result.expires_in, result.scope,
		                                                  result.account);

		printf("%s\n", json);
	}
	else
	{
		/* Raw stdout is a single token line for the caller to capture. */
		printf("%s\n", result.access_token);
	}

	entra_token_result_clear(&result);
	entra_acquire_clear(&acquire);
	return 0;
}

static int eth_do_accounts(const EthRequest* request)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) records = NULL;

	if (!entra_keyring_available(&error))
		return eth_fail(request, error);

	records = entra_keyring_list(&error);
	if (records == NULL)
		return eth_fail(request, error);

	if (request->json)
	{
		g_autofree char* json = entra_response_accounts_json(records);

		printf("%s\n", json);
		return 0;
	}

	for (guint i = 0; i < records->len; i++)
	{
		const EntraAccountRecord* record = g_ptr_array_index(records, i);

		printf("%s\t%s\t%s\n", record->account, record->host != NULL ? record->host : "-",
		       record->tenant != NULL ? record->tenant : "-");
	}

	return 0;
}

static int eth_do_logout(const EthRequest* request)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GPtrArray) records = NULL;
	guint removed = 0;

	if (!entra_keyring_available(&error))
		return eth_fail(request, error);

	records = entra_keyring_list(&error);
	if (records == NULL)
		return eth_fail(request, error);

	if (!entra_keyring_forget(NULL, NULL, request->account, &removed, &error))
		return eth_fail(request, error);

	if (request->json)
	{
		g_autoptr(GPtrArray) gone = g_ptr_array_new();
		g_autofree char* json = NULL;

		for (guint i = 0; i < records->len; i++)
		{
			EntraAccountRecord* record = g_ptr_array_index(records, i);

			if (request->account == NULL || g_strcmp0(record->account, request->account) == 0)
				g_ptr_array_add(gone, record);
		}

		json = entra_response_accounts_json(gone);
		printf("%s\n", json);
		return 0;
	}

	for (guint i = 0; i < records->len; i++)
	{
		const EntraAccountRecord* record = g_ptr_array_index(records, i);

		if (request->account == NULL || g_strcmp0(record->account, request->account) == 0)
			printf("Removed %s\n", record->account);
	}

	if (removed == 0)
		fprintf(stderr, "entra-token-helper: No stored accounts to remove\n");

	return 0;
}

int main(int argc, char** argv)
{
	EthRequest request = { 0 };
	g_autoptr(EntraConfig) config = NULL;
	g_autoptr(GError) error = NULL;
	const char* prompt = NULL;
	int rc = entra_status_exit_code(ENTRA_STATUS_USAGE);

	request.prompt = ENTRA_PROMPT_AUTO;
	request.scopes = g_ptr_array_new();

	for (int i = 1; i < argc; i++)
	{
		const char* arg = argv[i];
		char* eq = NULL;
		gsize namelen = 0;

		if (g_strcmp0(arg, "--help") == 0 || g_strcmp0(arg, "-h") == 0)
		{
			eth_usage(stdout);
			rc = 0;
			goto out;
		}

		if (g_strcmp0(arg, "--version") == 0 || g_strcmp0(arg, "-V") == 0)
		{
			printf("entra-token-helper " ENTRA_VERSION "\n");
			rc = 0;
			goto out;
		}

		if (g_strcmp0(arg, "--") == 0)
		{
			if (i + 1 < argc)
			{
				rc = eth_usage_error("Unexpected argument: '%s'", argv[i + 1]);
				goto out;
			}
			continue;
		}

		if (arg[0] != '-')
		{
			if (request.verb != ETH_VERB_NONE)
			{
				rc = eth_usage_error("Unexpected argument: '%s'", arg);
				goto out;
			}

			request.verb = eth_verb_from_string(arg);
			if (request.verb == ETH_VERB_NONE)
			{
				rc = eth_usage_error("Unknown verb: '%s'", arg);
				goto out;
			}
			continue;
		}

		eq = strchr(arg, '=');
		namelen = eq ? (gsize)(eq - arg) : strlen(arg);

		if (namelen == 6 && strncmp(arg, "--json", namelen) == 0)
		{
			if (eq)
			{
				rc = eth_usage_error("--json takes no value");
				goto out;
			}
			request.json = TRUE;
			continue;
		}

		if (namelen == 9 && strncmp(arg, "--verbose", namelen) == 0)
		{
			if (eq)
			{
				rc = eth_usage_error("--verbose takes no value");
				goto out;
			}
			request.verbose = TRUE;
			continue;
		}

#define ETH_OPT(literal, field)                                            \
	if (namelen == strlen(literal) && strncmp(arg, literal, namelen) == 0) \
	{                                                                      \
		if (!eth_take_value(argc, argv, &i, literal, eq, &(field)))        \
		{                                                                  \
			rc = entra_status_exit_code(ENTRA_STATUS_USAGE);               \
			goto out;                                                      \
		}                                                                  \
		continue;                                                          \
	}

		ETH_OPT("--authority", request.authority)
		ETH_OPT("--tenant", request.tenant)
		ETH_OPT("--cloud", request.cloud)
		ETH_OPT("--client-id", request.client_id)
		ETH_OPT("--req-cnf", request.req_cnf)
		ETH_OPT("--account", request.account)
		ETH_OPT("--config", request.config)
		ETH_OPT("--prompt", prompt)
		ETH_OPT("--parent-window", request.parent_window)
		ETH_OPT("--session-mode", request.session_mode)
		ETH_OPT("--format", request.format)
		ETH_OPT("--timeout", request.timeout)

#undef ETH_OPT

		if (namelen == 7 && strncmp(arg, "--scope", namelen) == 0)
		{
			const char* value = NULL;

			if (!eth_take_value(argc, argv, &i, "--scope", eq, &value))
			{
				rc = entra_status_exit_code(ENTRA_STATUS_USAGE);
				goto out;
			}

			g_ptr_array_add(request.scopes, (gpointer)value);
			continue;
		}

		rc = eth_usage_error("Unknown option: '%.*s'", (int)namelen, arg);
		goto out;
	}

	if (request.verb == ETH_VERB_NONE)
	{
		eth_usage(stderr);
		rc = entra_status_exit_code(ENTRA_STATUS_USAGE);
		goto out;
	}

	if (prompt != NULL)
	{
		if (g_strcmp0(prompt, "auto") == 0)
			request.prompt = ENTRA_PROMPT_AUTO;
		else if (g_strcmp0(prompt, "always") == 0)
			request.prompt = ENTRA_PROMPT_ALWAYS;
		else if (g_strcmp0(prompt, "never") == 0)
			request.prompt = ENTRA_PROMPT_NEVER;
		else
		{
			rc = eth_usage_error("--prompt must be one of auto, always, never");
			goto out;
		}
	}

	if (request.format != NULL)
	{
		if (g_strcmp0(request.format, "json") == 0)
			request.json = TRUE;
		else if (g_strcmp0(request.format, "raw") != 0)
		{
			rc = eth_usage_error("--format must be raw or json");
			goto out;
		}
	}

	if (request.session_mode != NULL && g_strcmp0(request.session_mode, "shared") != 0 &&
	    g_strcmp0(request.session_mode, "ephemeral") != 0)
	{
		rc = eth_usage_error("--session-mode must be shared or ephemeral");
		goto out;
	}

	switch (request.verb)
	{
		case ETH_VERB_LOGIN:
			if (request.prompt == ENTRA_PROMPT_NEVER)
			{
				rc = eth_usage_error("login requires --prompt auto or --prompt always");
				goto out;
			}
			if (request.req_cnf != NULL)
			{
				rc = eth_usage_error("--req-cnf requires the token verb");
				goto out;
			}
			break;

		case ETH_VERB_TOKEN:
			if (request.scopes->len == 0 && request.cloud == NULL)
			{
				rc = eth_usage_error("token requires --scope or --cloud");
				goto out;
			}
			break;

		case ETH_VERB_ACCOUNTS:
		case ETH_VERB_LOGOUT:
			if (request.req_cnf != NULL || request.scopes->len > 0 || prompt != NULL)
			{
				rc = eth_usage_error("%s does not accept --req-cnf, --scope or --prompt",
				                     eth_verb_to_string(request.verb));
				goto out;
			}
			break;

		case ETH_VERB_NONE:
		default:
			g_assert_not_reached();
	}

	if ((request.verb == ETH_VERB_LOGIN || request.verb == ETH_VERB_TOKEN) &&
	    request.authority == NULL && request.cloud == NULL)
	{
		rc = eth_usage_error("%s requires --authority or --cloud", eth_verb_to_string(request.verb));
		goto out;
	}

	entra_log_set_verbose(request.verbose);

	config = entra_config_load(request.config, &error);
	if (config == NULL)
	{
		rc = eth_fail(&request, error);
		goto out;
	}

	switch (request.verb)
	{
		case ETH_VERB_LOGIN:
			rc = eth_do_login(&request, config);
			break;
		case ETH_VERB_TOKEN:
			rc = eth_do_token(&request, config);
			break;
		case ETH_VERB_ACCOUNTS:
			rc = eth_do_accounts(&request);
			break;
		case ETH_VERB_LOGOUT:
			rc = eth_do_logout(&request);
			break;
		case ETH_VERB_NONE:
		default:
			g_assert_not_reached();
	}

out:
	g_ptr_array_free(request.scopes, TRUE);
	return rc;
}
