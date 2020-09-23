/*
 * Copyright 2011 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf.
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 *
 * URL handling for the "about" scheme.
 *
 * Based on the data fetcher by Rob Kendrick
 * This fetcher provides a simple scheme for the user to access
 * information from the browser from a known, fixed URL.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "netsurf/inttypes.h"
#include "netsurf/plot_style.h"

#include "utils/log.h"
#include "utils/corestrings.h"
#include "utils/nscolour.h"
#include "utils/nsoption.h"
#include "utils/utils.h"
#include "utils/messages.h"
#include "utils/ring.h"

#include "content/fetch.h"
#include "content/fetchers.h"

#include "desktop/system_colour.h"

#include "private.h"
#include "blank.h"
#include "certificate.h"
#include "imagecache.h"
#include "atestament.h"
#include "about.h"

typedef bool (*fetch_about_handler)(struct fetch_about_context *);

/**
 * Context for an about fetch
 */
struct fetch_about_context {
	struct fetch_about_context *r_next, *r_prev;

	struct fetch *fetchh; /**< Handle for this fetch */

	bool aborted; /**< Flag indicating fetch has been aborted */
	bool locked; /**< Flag indicating entry is already entered */

	nsurl *url; /**< The full url the fetch refers to */

	const struct fetch_multipart_data *multipart; /**< post data */

	fetch_about_handler handler;
};

static struct fetch_about_context *ring = NULL;

/**
 * handler info for about scheme
 */
struct about_handlers {
	const char *name; /**< name to match in url */
	int name_len;
	lwc_string *lname; /**< Interned name */
	fetch_about_handler handler; /**< handler for the url */
	bool hidden; /**< If entry should be hidden in listing */
};



/**
 * issue fetch callbacks with locking
 */
static bool
fetch_about_send_callback(const fetch_msg *msg, struct fetch_about_context *ctx)
{
	ctx->locked = true;
	fetch_send_callback(msg, ctx->fetchh);
	ctx->locked = false;

	return ctx->aborted;
}

/* exported interface documented in about/private.h */
bool
fetch_about_send_finished(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	msg.type = FETCH_FINISHED;
	return fetch_about_send_callback(&msg, ctx);
}

/* exported interface documented in about/private.h */
bool fetch_about_set_http_code(struct fetch_about_context *ctx, long code)
{
	fetch_set_http_code(ctx->fetchh, code);

	return ctx->aborted;
}

/* exported interface documented in about/private.h */
bool
fetch_about_send_header(struct fetch_about_context *ctx, const char *fmt, ...)
{
	char header[64];
	fetch_msg msg;
	va_list ap;

	va_start(ap, fmt);

	vsnprintf(header, sizeof header, fmt, ap);

	va_end(ap);

	msg.type = FETCH_HEADER;
	msg.data.header_or_data.buf = (const uint8_t *) header;
	msg.data.header_or_data.len = strlen(header);

	return fetch_about_send_callback(&msg, ctx);
}

/* exported interface documented in about/private.h */
nserror
fetch_about_senddata(struct fetch_about_context *ctx, const uint8_t *data, size_t data_len)
{
	fetch_msg msg;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = data;
	msg.data.header_or_data.len = data_len;

	if (fetch_about_send_callback(&msg, ctx)) {
		return NSERROR_INVALID;
	}

	return NSERROR_OK;
}

/* exported interface documented in about/private.h */
nserror
fetch_about_ssenddataf(struct fetch_about_context *ctx, const char *fmt, ...)
{
	char buffer[1024];
	char *dbuff;
	fetch_msg msg;
	va_list ap;
	int slen;

	va_start(ap, fmt);

	slen = vsnprintf(buffer, sizeof(buffer), fmt, ap);

	va_end(ap);

	if (slen < (int)sizeof(buffer)) {
		msg.type = FETCH_DATA;
		msg.data.header_or_data.buf = (const uint8_t *) buffer;
		msg.data.header_or_data.len = slen;

		if (fetch_about_send_callback(&msg, ctx)) {
			return NSERROR_INVALID;
		}

		return NSERROR_OK;
	}

	dbuff = malloc(slen + 1);
	if (dbuff == NULL) {
		return NSERROR_NOSPACE;
	}

	va_start(ap, fmt);

	slen = vsnprintf(dbuff, slen + 1, fmt, ap);

	va_end(ap);

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *)dbuff;
	msg.data.header_or_data.len = slen;

	if (fetch_about_send_callback(&msg, ctx)) {
		free(dbuff);
		return NSERROR_INVALID;
	}

	free(dbuff);
	return NSERROR_OK;
}


/* exported interface documented in about/private.h */
nsurl *fetch_about_get_url(struct fetch_about_context *ctx)
{
	return ctx->url;
}


/**
 * Generate a 500 server error respnse
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_srverror(struct fetch_about_context *ctx)
{
	nserror res;

	fetch_set_http_code(ctx->fetchh, 500);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/plain"))
		return false;

	res = fetch_about_ssenddataf(ctx, "Server error 500");
	if (res != NSERROR_OK) {
		return false;
	}

	fetch_about_send_finished(ctx);

	return true;
}


/**
 * Handler to generate about scheme credits page.
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_credits_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:credits.html";

	fetch_about_send_callback(&msg, ctx);

	return true;
}


/**
 * Handler to generate about scheme licence page.
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_licence_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:licence.html";

	fetch_about_send_callback(&msg, ctx);

	return true;
}


/**
 * Handler to generate about scheme config page
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_config_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	char buffer[1024];
	int slen = 0;
	unsigned int opt_loop = 0;
	int elen = 0; /* entry length */
	nserror res;
	bool even = false;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html")) {
		goto fetch_about_config_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			"<html>\n<head>\n"
			"<title>NetSurf Browser Config</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body "
				"id =\"configlist\" "
				"class=\"ns-even-bg ns-even-fg ns-border\" "
				"style=\"overflow: hidden;\">\n"
			"<h1 class=\"ns-border\">NetSurf Browser Config</h1>\n"
			"<table class=\"config\">\n"
			"<tr><th>Option</th>"
			"<th>Type</th>"
			"<th>Provenance</th>"
			"<th>Setting</th></tr>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_config_handler_aborted;
	}

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	do {
		if (even) {
			elen = nsoption_snoptionf(buffer + slen,
					sizeof buffer - slen,
					opt_loop,
					"<tr class=\"ns-even-bg\">"
						"<th class=\"ns-border\">%k</th>"
						"<td class=\"ns-border\">%t</td>"
						"<td class=\"ns-border\">%p</td>"
						"<td class=\"ns-border\">%V</td>"
					"</tr>\n");
		} else {
			elen = nsoption_snoptionf(buffer + slen,
					sizeof buffer - slen,
					opt_loop,
					"<tr class=\"ns-odd-bg\">"
						"<th class=\"ns-border\">%k</th>"
						"<td class=\"ns-border\">%t</td>"
						"<td class=\"ns-border\">%p</td>"
						"<td class=\"ns-border\">%V</td>"
					"</tr>\n");
		}
		if (elen <= 0)
			break; /* last option */

		if (elen >= (int) (sizeof buffer - slen)) {
			/* last entry would not fit in buffer, submit buffer */
			msg.data.header_or_data.len = slen;
			if (fetch_about_send_callback(&msg, ctx))
				goto fetch_about_config_handler_aborted;
			slen = 0;
		} else {
			/* normal addition */
			slen += elen;
			opt_loop++;
			even = !even;
		}
	} while (elen > 0);

	slen += snprintf(buffer + slen, sizeof buffer - slen,
			 "</table>\n</body>\n</html>\n");

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_config_handler_aborted;

	fetch_about_send_finished(ctx);

	return true;

fetch_about_config_handler_aborted:
	return false;
}


/**
 * Handler to generate the nscolours stylesheet
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_nscolours_handler(struct fetch_about_context *ctx)
{
	nserror res;
	const char *stylesheet;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/css; charset=utf-8")) {
		goto aborted;
	}

	res = nscolour_get_stylesheet(&stylesheet);
	if (res != NSERROR_OK) {
		goto aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			"html {\n"
			"\tbackground-color: #%06x;\n"
			"}\n"
			"%s",
			colour_rb_swap(nscolours[NSCOLOUR_WIN_ODD_BG]),
			stylesheet);
	if (res != NSERROR_OK) {
		goto aborted;
	}

	fetch_about_send_finished(ctx);

	return true;

aborted:

	return false;
}


/**
 * Generate the text of a Choices file which represents the current
 * in use options.
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_choices_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;
	char buffer[1024];
	int code = 200;
	int slen;
	unsigned int opt_loop = 0;
	int res = 0;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, code);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/plain"))
		goto fetch_about_choices_handler_aborted;

	msg.type = FETCH_DATA;
	msg.data.header_or_data.buf = (const uint8_t *) buffer;

	slen = snprintf(buffer, sizeof buffer,
		 "# Automatically generated current NetSurf browser Choices\n");

	do {
		res = nsoption_snoptionf(buffer + slen,
				sizeof buffer - slen,
				opt_loop,
				"%k:%v\n");
		if (res <= 0)
			break; /* last option */

		if (res >= (int) (sizeof buffer - slen)) {
			/* last entry would not fit in buffer, submit buffer */
			msg.data.header_or_data.len = slen;
			if (fetch_about_send_callback(&msg, ctx))
				goto fetch_about_choices_handler_aborted;
			slen = 0;
		} else {
			/* normal addition */
			slen += res;
			opt_loop++;
		}
	} while (res > 0);

	msg.data.header_or_data.len = slen;
	if (fetch_about_send_callback(&msg, ctx))
		goto fetch_about_choices_handler_aborted;

	fetch_about_send_finished(ctx);

	return true;

fetch_about_choices_handler_aborted:
	return false;
}




/**
 * Handler to generate about scheme logo page
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_logo_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:netsurf.png";

	fetch_about_send_callback(&msg, ctx);

	return true;
}


/**
 * Handler to generate about scheme welcome page
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_welcome_handler(struct fetch_about_context *ctx)
{
	fetch_msg msg;

	/* content is going to return redirect */
	fetch_set_http_code(ctx->fetchh, 302);

	msg.type = FETCH_REDIRECT;
	msg.data.redirect = "resource:welcome.html";

	fetch_about_send_callback(&msg, ctx);

	return true;
}


/**
 * generate the description of the login query
 */
static nserror
get_authentication_description(struct nsurl *url,
			       const char *realm,
			       const char *username,
			       const char *password,
			       char **out_str)
{
	nserror res;
	char *url_s;
	size_t url_l;
	char *str = NULL;
	const char *key;

	res = nsurl_get(url, NSURL_HOST, &url_s, &url_l);
	if (res != NSERROR_OK) {
		return res;
	}

	if ((*username == 0) && (*password == 0)) {
		key = "LoginDescription";
	} else {
		key = "LoginAgain";
	}

	str = messages_get_buff(key, url_s, realm);
	if (str != NULL) {
		NSLOG(netsurf, INFO,
		      "key:%s url:%s realm:%s str:%s",
		      key, url_s, realm, str);
		*out_str = str;
	} else {
		res = NSERROR_NOMEM;
	}

	free(url_s);

	return res;
}


/**
 * generate a generic query description
 */
static nserror
get_query_description(struct nsurl *url,
		      const char *key,
		      char **out_str)
{
	nserror res;
	char *url_s;
	size_t url_l;
	char *str = NULL;

	/* get the host in question */
	res = nsurl_get(url, NSURL_HOST, &url_s, &url_l);
	if (res != NSERROR_OK) {
		return res;
	}

	/* obtain the description with the url substituted */
	str = messages_get_buff(key, url_s);
	if (str == NULL) {
		res = NSERROR_NOMEM;
	} else {
		*out_str = str;
	}

	free(url_s);

	return res;
}


/**
 * Handler to generate about scheme authentication query page
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_query_auth_handler(struct fetch_about_context *ctx)
{
	nserror res;
	char *url_s;
	size_t url_l;
	const char *realm = "";
	const char *username = "";
	const char *password = "";
	const char *title;
	char *description = NULL;
	struct nsurl *siteurl = NULL;
	const struct fetch_multipart_data *curmd; /* mutipart data iterator */

	/* extract parameters from multipart post data */
	curmd = ctx->multipart;
	while (curmd != NULL) {
		if (strcmp(curmd->name, "siteurl") == 0) {
			res = nsurl_create(curmd->value, &siteurl);
			if (res != NSERROR_OK) {
				return fetch_about_srverror(ctx);
			}
		} else if (strcmp(curmd->name, "realm") == 0) {
			realm = curmd->value;
		} else if (strcmp(curmd->name, "username") == 0) {
			username = curmd->value;
		} else if (strcmp(curmd->name, "password") == 0) {
			password = curmd->value;
		}
		curmd = curmd->next;
	}

	if (siteurl == NULL) {
		return fetch_about_srverror(ctx);
	}

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html; charset=utf-8")) {
		goto fetch_about_query_auth_handler_aborted;
	}

	title = messages_get("LoginTitle");
	res = fetch_about_ssenddataf(ctx,
			"<html>\n<head>\n"
			"<title>%s</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body class=\"ns-even-bg ns-even-fg ns-border\" id =\"authentication\">\n"
			"<h1 class=\"ns-border\">%s</h1>\n",
			title, title);
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<form method=\"post\""
			 " enctype=\"multipart/form-data\">");
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = get_authentication_description(siteurl,
					     realm,
					     username,
					     password,
					     &description);
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx, "<p>%s</p>", description);
		free(description);
		if (res != NSERROR_OK) {
			goto fetch_about_query_auth_handler_aborted;
		}
	}

	res = fetch_about_ssenddataf(ctx, "<table>");
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<tr>"
			 "<th><label for=\"name\">%s:</label></th>"
			 "<td><input type=\"text\" id=\"username\" "
			 "name=\"username\" value=\"%s\"></td>"
			 "</tr>",
			 messages_get("Username"), username);
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<tr>"
			 "<th><label for=\"password\">%s:</label></th>"
			 "<td><input type=\"password\" id=\"password\" "
			 "name=\"password\" value=\"%s\"></td>"
			 "</tr>",
			 messages_get("Password"), password);
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx, "</table>");
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<div id=\"buttons\">"
			 "<input type=\"submit\" id=\"login\" name=\"login\" "
			 "value=\"%s\" class=\"default-action\">"
			 "<input type=\"submit\" id=\"cancel\" name=\"cancel\" "
			 "value=\"%s\">"
			 "</div>",
			 messages_get("Login"),
			 messages_get("Cancel"));
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = nsurl_get(siteurl, NSURL_COMPLETE, &url_s, &url_l);
	if (res != NSERROR_OK) {
		url_s = strdup("");
	}
	res = fetch_about_ssenddataf(ctx,
			 "<input type=\"hidden\" name=\"siteurl\" value=\"%s\">",
			 url_s);
	free(url_s);
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<input type=\"hidden\" name=\"realm\" value=\"%s\">",
			 realm);
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx, "</form></body>\n</html>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_query_auth_handler_aborted;
	}

	fetch_about_send_finished(ctx);

	nsurl_unref(siteurl);

	return true;

fetch_about_query_auth_handler_aborted:

	nsurl_unref(siteurl);

	return false;
}


/**
 * Handler to generate about scheme privacy query page
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_query_privacy_handler(struct fetch_about_context *ctx)
{
	nserror res;
	char *url_s;
	size_t url_l;
	const char *reason = "";
	const char *title;
	struct nsurl *siteurl = NULL;
	char *description = NULL;
	const char *chainurl = NULL;
	const struct fetch_multipart_data *curmd; /* mutipart data iterator */

	/* extract parameters from multipart post data */
	curmd = ctx->multipart;
	while (curmd != NULL) {
		if (strcmp(curmd->name, "siteurl") == 0) {
			res = nsurl_create(curmd->value, &siteurl);
			if (res != NSERROR_OK) {
				return fetch_about_srverror(ctx);
			}
		} else if (strcmp(curmd->name, "reason") == 0) {
			reason = curmd->value;
		} else if (strcmp(curmd->name, "chainurl") == 0) {
			chainurl = curmd->value;
		}
		curmd = curmd->next;
	}

	if (siteurl == NULL) {
		return fetch_about_srverror(ctx);
	}

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html; charset=utf-8")) {
		goto fetch_about_query_ssl_handler_aborted;
	}

	title = messages_get("PrivacyTitle");
	res = fetch_about_ssenddataf(ctx,
			"<html>\n<head>\n"
			"<title>%s</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body class=\"ns-even-bg ns-even-fg ns-border\" id =\"privacy\">\n"
			"<h1 class=\"ns-border ns-odd-fg-bad\">%s</h1>\n",
			title, title);
	if (res != NSERROR_OK) {
		goto fetch_about_query_ssl_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<form method=\"post\""
			 " enctype=\"multipart/form-data\">");
	if (res != NSERROR_OK) {
		goto fetch_about_query_ssl_handler_aborted;
	}

	res = get_query_description(siteurl,
				    "PrivacyDescription",
				    &description);
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx, "<div><p>%s</p></div>", description);
		free(description);
		if (res != NSERROR_OK) {
			goto fetch_about_query_ssl_handler_aborted;
		}
	}

	if (chainurl == NULL) {
		res = fetch_about_ssenddataf(ctx,
				 "<div><p>%s</p></div>"
				 "<div><p>%s</p></div>",
				 reason,
				 messages_get("ViewCertificatesNotPossible"));
	} else {
		res = fetch_about_ssenddataf(ctx,
				 "<div><p>%s</p></div>"
				 "<div><p><a href=\"%s\" target=\"_blank\">%s</a></p></div>",
				 reason,
				 chainurl,
				 messages_get("ViewCertificates"));
	}
	if (res != NSERROR_OK) {
		goto fetch_about_query_ssl_handler_aborted;
	}
	res = fetch_about_ssenddataf(ctx,
			 "<div id=\"buttons\">"
			 "<input type=\"submit\" id=\"back\" name=\"back\" "
			 "value=\"%s\" class=\"default-action\">"
			 "<input type=\"submit\" id=\"proceed\" name=\"proceed\" "
			 "value=\"%s\">"
			 "</div>",
			 messages_get("Backtosafety"),
			 messages_get("Proceed"));
	if (res != NSERROR_OK) {
		goto fetch_about_query_ssl_handler_aborted;
	}

	res = nsurl_get(siteurl, NSURL_COMPLETE, &url_s, &url_l);
	if (res != NSERROR_OK) {
		url_s = strdup("");
	}
	res = fetch_about_ssenddataf(ctx,
			 "<input type=\"hidden\" name=\"siteurl\" value=\"%s\">",
			 url_s);
	free(url_s);
	if (res != NSERROR_OK) {
		goto fetch_about_query_ssl_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx, "</form></body>\n</html>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_query_ssl_handler_aborted;
	}

	fetch_about_send_finished(ctx);

	nsurl_unref(siteurl);

	return true;

fetch_about_query_ssl_handler_aborted:
	nsurl_unref(siteurl);

	return false;
}


/**
 * Handler to generate about scheme timeout query page
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool fetch_about_query_timeout_handler(struct fetch_about_context *ctx)
{
	nserror res;
	char *url_s;
	size_t url_l;
	const char *reason = "";
	const char *title;
	struct nsurl *siteurl = NULL;
	char *description = NULL;
	const struct fetch_multipart_data *curmd; /* mutipart data iterator */

	/* extract parameters from multipart post data */
	curmd = ctx->multipart;
	while (curmd != NULL) {
		if (strcmp(curmd->name, "siteurl") == 0) {
			res = nsurl_create(curmd->value, &siteurl);
			if (res != NSERROR_OK) {
				return fetch_about_srverror(ctx);
			}
		} else if (strcmp(curmd->name, "reason") == 0) {
			reason = curmd->value;
		}
		curmd = curmd->next;
	}

	if (siteurl == NULL) {
		return fetch_about_srverror(ctx);
	}

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html; charset=utf-8")) {
		goto fetch_about_query_timeout_handler_aborted;
	}

	title = messages_get("TimeoutTitle");
	res = fetch_about_ssenddataf(ctx,
			"<html>\n<head>\n"
			"<title>%s</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body class=\"ns-even-bg ns-even-fg ns-border\" id =\"timeout\">\n"
			"<h1 class=\"ns-border ns-odd-fg-bad\">%s</h1>\n",
			title, title);
	if (res != NSERROR_OK) {
		goto fetch_about_query_timeout_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<form method=\"post\""
			 " enctype=\"multipart/form-data\">");
	if (res != NSERROR_OK) {
		goto fetch_about_query_timeout_handler_aborted;
	}

	res = get_query_description(siteurl,
				    "TimeoutDescription",
				    &description);
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx, "<div><p>%s</p></div>", description);
		free(description);
		if (res != NSERROR_OK) {
			goto fetch_about_query_timeout_handler_aborted;
		}
	}
	res = fetch_about_ssenddataf(ctx, "<div><p>%s</p></div>", reason);
	if (res != NSERROR_OK) {
		goto fetch_about_query_timeout_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<div id=\"buttons\">"
			 "<input type=\"submit\" id=\"back\" name=\"back\" "
			 "value=\"%s\" class=\"default-action\">"
			 "<input type=\"submit\" id=\"retry\" name=\"retry\" "
			 "value=\"%s\">"
			 "</div>",
			 messages_get("Backtoprevious"),
			 messages_get("TryAgain"));
	if (res != NSERROR_OK) {
		goto fetch_about_query_timeout_handler_aborted;
	}

	res = nsurl_get(siteurl, NSURL_COMPLETE, &url_s, &url_l);
	if (res != NSERROR_OK) {
		url_s = strdup("");
	}
	res = fetch_about_ssenddataf(ctx,
			 "<input type=\"hidden\" name=\"siteurl\" value=\"%s\">",
			 url_s);
	free(url_s);
	if (res != NSERROR_OK) {
		goto fetch_about_query_timeout_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx, "</form></body>\n</html>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_query_timeout_handler_aborted;
	}

	fetch_about_send_finished(ctx);

	nsurl_unref(siteurl);

	return true;

fetch_about_query_timeout_handler_aborted:
	nsurl_unref(siteurl);

	return false;
}


/**
 * Handler to generate about scheme fetch error query page
 *
 * \param ctx The fetcher context.
 * \return true if handled false if aborted.
 */
static bool
fetch_about_query_fetcherror_handler(struct fetch_about_context *ctx)
{
	nserror res;
	char *url_s;
	size_t url_l;
	const char *reason = "";
	const char *title;
	struct nsurl *siteurl = NULL;
	char *description = NULL;
	const struct fetch_multipart_data *curmd; /* mutipart data iterator */

	/* extract parameters from multipart post data */
	curmd = ctx->multipart;
	while (curmd != NULL) {
		if (strcmp(curmd->name, "siteurl") == 0) {
			res = nsurl_create(curmd->value, &siteurl);
			if (res != NSERROR_OK) {
				return fetch_about_srverror(ctx);
			}
		} else if (strcmp(curmd->name, "reason") == 0) {
			reason = curmd->value;
		}
		curmd = curmd->next;
	}

	if (siteurl == NULL) {
		return fetch_about_srverror(ctx);
	}

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html; charset=utf-8")) {
		goto fetch_about_query_fetcherror_handler_aborted;
	}

	title = messages_get("FetchErrorTitle");
	res = fetch_about_ssenddataf(ctx,
			"<html>\n<head>\n"
			"<title>%s</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body class=\"ns-even-bg ns-even-fg ns-border\" id =\"fetcherror\">\n"
			"<h1 class=\"ns-border ns-odd-fg-bad\">%s</h1>\n",
			title, title);
	if (res != NSERROR_OK) {
		goto fetch_about_query_fetcherror_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<form method=\"post\""
			 " enctype=\"multipart/form-data\">");
	if (res != NSERROR_OK) {
		goto fetch_about_query_fetcherror_handler_aborted;
	}

	res = get_query_description(siteurl,
				    "FetchErrorDescription",
				    &description);
	if (res == NSERROR_OK) {
		res = fetch_about_ssenddataf(ctx, "<div><p>%s</p></div>", description);
		free(description);
		if (res != NSERROR_OK) {
			goto fetch_about_query_fetcherror_handler_aborted;
		}
	}
	res = fetch_about_ssenddataf(ctx, "<div><p>%s</p></div>", reason);
	if (res != NSERROR_OK) {
		goto fetch_about_query_fetcherror_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx,
			 "<div id=\"buttons\">"
			 "<input type=\"submit\" id=\"back\" name=\"back\" "
			 "value=\"%s\" class=\"default-action\">"
			 "<input type=\"submit\" id=\"retry\" name=\"retry\" "
			 "value=\"%s\">"
			 "</div>",
			 messages_get("Backtoprevious"),
			 messages_get("TryAgain"));
	if (res != NSERROR_OK) {
		goto fetch_about_query_fetcherror_handler_aborted;
	}

	res = nsurl_get(siteurl, NSURL_COMPLETE, &url_s, &url_l);
	if (res != NSERROR_OK) {
		url_s = strdup("");
	}
	res = fetch_about_ssenddataf(ctx,
			 "<input type=\"hidden\" name=\"siteurl\" value=\"%s\">",
			 url_s);
	free(url_s);
	if (res != NSERROR_OK) {
		goto fetch_about_query_fetcherror_handler_aborted;
	}

	res = fetch_about_ssenddataf(ctx, "</form></body>\n</html>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_query_fetcherror_handler_aborted;
	}

	fetch_about_send_finished(ctx);

	nsurl_unref(siteurl);

	return true;

fetch_about_query_fetcherror_handler_aborted:
	nsurl_unref(siteurl);

	return false;
}


/* Forward declaration because this handler requires the handler table. */
static bool fetch_about_about_handler(struct fetch_about_context *ctx);

/**
 * List of about paths and their handlers
 */
struct about_handlers about_handler_list[] = {
	{
		"credits",
		SLEN("credits"),
		NULL,
		fetch_about_credits_handler,
		false
	},
	{
		"licence",
		SLEN("licence"),
		NULL,
		fetch_about_licence_handler,
		false
	},
	{
		"license",
		SLEN("license"),
		NULL,
		fetch_about_licence_handler,
		true
	},
	{
		"welcome",
		SLEN("welcome"),
		NULL,
		fetch_about_welcome_handler,
		false
	},
	{
		"config",
		SLEN("config"),
		NULL,
		fetch_about_config_handler,
		false
	},
	{
		"Choices",
		SLEN("Choices"),
		NULL,
		fetch_about_choices_handler,
		false
	},
	{
		"testament",
		SLEN("testament"),
		NULL,
		fetch_about_testament_handler,
		false
	},
	{
		"about",
		SLEN("about"),
		NULL,
		fetch_about_about_handler,
		true
	},
	{
		"nscolours.css",
		SLEN("nscolours.css"),
		NULL,
		fetch_about_nscolours_handler,
		true
	},
	{
		"logo",
		SLEN("logo"),
		NULL,
		fetch_about_logo_handler,
		true
	},
	{
		/* details about the image cache */
		"imagecache",
		SLEN("imagecache"),
		NULL,
		fetch_about_imagecache_handler,
		true
	},
	{
		/* The default blank page */
		"blank",
		SLEN("blank"),
		NULL,
		fetch_about_blank_handler,
		true
	},
	{
		/* details about a certificate */
		"certificate",
		SLEN("certificate"),
		NULL,
		fetch_about_certificate_handler,
		true
	},
	{
		"query/auth",
		SLEN("query/auth"),
		NULL,
		fetch_about_query_auth_handler,
		true
	},
	{
		"query/ssl",
		SLEN("query/ssl"),
		NULL,
		fetch_about_query_privacy_handler,
		true
	},
	{
		"query/timeout",
		SLEN("query/timeout"),
		NULL,
		fetch_about_query_timeout_handler,
		true
	},
	{
		"query/fetcherror",
		SLEN("query/fetcherror"),
		NULL,
		fetch_about_query_fetcherror_handler,
		true
	}
};

#define about_handler_list_len \
	(sizeof(about_handler_list) / sizeof(struct about_handlers))

/**
 * List all the valid about: paths available
 *
 * \param ctx The fetch context.
 * \return true for sucess or false to generate an error.
 */
static bool fetch_about_about_handler(struct fetch_about_context *ctx)
{
	nserror res;
	unsigned int abt_loop = 0;

	/* content is going to return ok */
	fetch_set_http_code(ctx->fetchh, 200);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/html"))
		goto fetch_about_config_handler_aborted;

	res = fetch_about_ssenddataf(ctx,
			"<html>\n<head>\n"
			"<title>List of NetSurf pages</title>\n"
			"<link rel=\"stylesheet\" type=\"text/css\" "
			"href=\"resource:internal.css\">\n"
			"</head>\n"
			"<body class=\"ns-even-bg ns-even-fg ns-border\">\n"
			"<h1 class =\"ns-border\">List of NetSurf pages</h1>\n"
			"<ul>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_config_handler_aborted;
	}

	for (abt_loop = 0; abt_loop < about_handler_list_len; abt_loop++) {

		/* Skip over hidden entries */
		if (about_handler_list[abt_loop].hidden)
			continue;

		res = fetch_about_ssenddataf(ctx,
			       "<li><a href=\"about:%s\">about:%s</a></li>\n",
			       about_handler_list[abt_loop].name,
			       about_handler_list[abt_loop].name);
		if (res != NSERROR_OK) {
			goto fetch_about_config_handler_aborted;
		}
	}

	res = fetch_about_ssenddataf(ctx, "</ul>\n</body>\n</html>\n");
	if (res != NSERROR_OK) {
		goto fetch_about_config_handler_aborted;
	}

	fetch_about_send_finished(ctx);

	return true;

fetch_about_config_handler_aborted:
	return false;
}

static bool
fetch_about_404_handler(struct fetch_about_context *ctx)
{
	nserror res;

	/* content is going to return 404 */
	fetch_set_http_code(ctx->fetchh, 404);

	/* content type */
	if (fetch_about_send_header(ctx, "Content-Type: text/plain; charset=utf-8")) {
		return false;
	}

	res = fetch_about_ssenddataf(ctx, "Unknown page: %s", nsurl_access(ctx->url));
	if (res != NSERROR_OK) {
		return false;
	}

	fetch_about_send_finished(ctx);

	return true;
}

/**
 * callback to initialise the about scheme fetcher.
 */
static bool fetch_about_initialise(lwc_string *scheme)
{
	unsigned int abt_loop = 0;
	lwc_error error;

	for (abt_loop = 0; abt_loop < about_handler_list_len; abt_loop++) {
		error = lwc_intern_string(about_handler_list[abt_loop].name,
					about_handler_list[abt_loop].name_len,
					&about_handler_list[abt_loop].lname);
		if (error != lwc_error_ok) {
			while (abt_loop-- != 0) {
				lwc_string_unref(about_handler_list[abt_loop].lname);
			}
			return false;
		}
	}

	return true;
}


/**
 * callback to finalise the about scheme fetcher.
 */
static void fetch_about_finalise(lwc_string *scheme)
{
	unsigned int abt_loop = 0;
	for (abt_loop = 0; abt_loop < about_handler_list_len; abt_loop++) {
		lwc_string_unref(about_handler_list[abt_loop].lname);
	}
}


static bool fetch_about_can_fetch(const nsurl *url)
{
	return true;
}


/**
 * callback to set up a about scheme fetch.
 *
 * \param post_urlenc post data in urlenc format, owned by the llcache object
 *                        hence valid the entire lifetime of the fetch.
 * \param post_multipart post data in multipart format, owned by the llcache
 *                        object hence valid the entire lifetime of the fetch.
 */
static void *
fetch_about_setup(struct fetch *fetchh,
		  nsurl *url,
		  bool only_2xx,
		  bool downgrade_tls,
		  const char *post_urlenc,
		  const struct fetch_multipart_data *post_multipart,
		  const char **headers)
{
	struct fetch_about_context *ctx;
	unsigned int handler_loop;
	lwc_string *path;
	bool match;

	ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL)
		return NULL;

	path = nsurl_get_component(url, NSURL_PATH);

	for (handler_loop = 0;
	     handler_loop < about_handler_list_len;
	     handler_loop++) {
		if (lwc_string_isequal(path,
				       about_handler_list[handler_loop].lname,
				       &match) == lwc_error_ok && match) {
			ctx->handler = about_handler_list[handler_loop].handler;
			break;
		}
	}

	if (path != NULL)
		lwc_string_unref(path);

	ctx->fetchh = fetchh;
	ctx->url = nsurl_ref(url);
	ctx->multipart = post_multipart;

	RING_INSERT(ring, ctx);

	return ctx;
}


/**
 * callback to free a about scheme fetch
 */
static void fetch_about_free(void *ctx)
{
	struct fetch_about_context *c = ctx;
	nsurl_unref(c->url);
	free(ctx);
}


/**
 * callback to start an about scheme fetch
 */
static bool fetch_about_start(void *ctx)
{
	return true;
}


/**
 * callback to abort a about fetch
 */
static void fetch_about_abort(void *ctx)
{
	struct fetch_about_context *c = ctx;

	/* To avoid the poll loop having to deal with the fetch context
	 * disappearing from under it, we simply flag the abort here.
	 * The poll loop itself will perform the appropriate cleanup.
	 */
	c->aborted = true;
}


/**
 * callback to poll for additional about fetch contents
 */
static void fetch_about_poll(lwc_string *scheme)
{
	struct fetch_about_context *c, *save_ring = NULL;

	/* Iterate over ring, processing each pending fetch */
	while (ring != NULL) {
		/* Take the first entry from the ring */
		c = ring;
		RING_REMOVE(ring, c);

		/* Ignore fetches that have been flagged as locked.
		 * This allows safe re-entrant calls to this function.
		 * Re-entrancy can occur if, as a result of a callback,
		 * the interested party causes fetch_poll() to be called
		 * again.
		 */
		if (c->locked == true) {
			RING_INSERT(save_ring, c);
			continue;
		}

		/* Only process non-aborted fetches */
		if (c->aborted == false) {
			/* about fetches can be processed in one go */
			if (c->handler == NULL) {
				fetch_about_404_handler(c);
			} else {
				c->handler(c);
			}
		}

		/* And now finish */
		fetch_remove_from_queues(c->fetchh);
		fetch_free(c->fetchh);
	}

	/* Finally, if we saved any fetches which were locked, put them back
	 * into the ring for next time
	 */
	ring = save_ring;
}


nserror fetch_about_register(void)
{
	lwc_string *scheme = lwc_string_ref(corestring_lwc_about);
	const struct fetcher_operation_table fetcher_ops = {
		.initialise = fetch_about_initialise,
		.acceptable = fetch_about_can_fetch,
		.setup = fetch_about_setup,
		.start = fetch_about_start,
		.abort = fetch_about_abort,
		.free = fetch_about_free,
		.poll = fetch_about_poll,
		.finalise = fetch_about_finalise
	};

	return fetcher_add(scheme, &fetcher_ops);
}
