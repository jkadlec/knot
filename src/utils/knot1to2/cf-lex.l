/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
%{

#include <arpa/inet.h>
#include <config.h>
#include <dirent.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "utils/knot1to2/scheme.h"
#include "utils/knot1to2/includes.h"
#include "utils/knot1to2/extra.h"
#include "utils/knot1to2/cf-parse.tab.h"
#include "libknot/internal/strlcpy.h"

/* Imported symbols. */
#define lval (yylval->tok)
extern void cf_error(void *scanner, const char *format, ...);

/* Convert hex to binary. */
static inline char xd(char d) {
	if (d >= '0' && d <= '9') return d - '0';
	if (d >= 'a' && d <= 'f') return d - 'a' + 10;
	if (d >= 'A' && d <= 'F') return d - 'A' + 10;
	return 0;
}
int hex2bin(const char* src, char *dst, size_t len) {
	for (unsigned i = 0; i < len; ++i) {
		dst[i] = (xd(src[i<<1])<<4) + xd(src[(i<<1)+1]);
	}
	return 0;
}

#define YY_NO_UNPUT

%}

%option reentrant
%option bison-bridge
%option noyywrap
%option noinput
%option nounput
%option noreject
%option yylineno
%option prefix = "cf_"
%option outfile = "cf-lex.c"
%option extra-type = "conf_extra_t *"

%x include

ALPHA [a-zA-Z_]
DIGIT [0-9]
HEXA  [0-9a-fA-F]
ALNUM [a-zA-Z_0-9]
BLANK [ \t\n]

%%
\#.*\n          /* Ignore comments */;
{BLANK}+        /* Ignore whitespace */;
[\=\!\$\%\^\&\*\(\)\/\+\-\@\{\}\;\,] { return yytext[0]; }
system                { lval.t = yytext; return SYSTEM; }
identity              { lval.t = yytext; return IDENTITY; }
hostname              { lval.t = yytext; return HOSTNAME; }
version               { lval.t = yytext; return SVERSION; }
nsid                  { lval.t = yytext; return NSID; }
max-udp-payload       { lval.t = yytext; return MAX_UDP_PAYLOAD; }
storage               { lval.t = yytext; return STORAGE; }
key                   { lval.t = yytext; return KEY; }
keys                  { lval.t = yytext; return KEYS; }
remotes               { lval.t = yytext; return REMOTES; }
groups                { lval.t = yytext; return GROUPS; }

zones                 { lval.t = yytext; return ZONES; }
file                  { lval.t = yytext; return FILENAME; }
disable-any           { lval.t = yytext; return DISABLE_ANY; }
semantic-checks       { lval.t = yytext; return SEMANTIC_CHECKS; }
notify-retries        { lval.t = yytext; return NOTIFY_RETRIES; }
notify-timeout        { lval.t = yytext; return NOTIFY_TIMEOUT; }
zonefile-sync         { lval.t = yytext; return DBSYNC_TIMEOUT; }
ixfr-fslimit          { lval.t = yytext; return IXFR_FSLIMIT; }
xfr-in                { lval.t = yytext; return XFR_IN; }
xfr-out               { lval.t = yytext; return XFR_OUT; }
update-in             { lval.t = yytext; return UPDATE_IN; }
notify-in             { lval.t = yytext; return NOTIFY_IN; }
notify-out            { lval.t = yytext; return NOTIFY_OUT; }
workers               { lval.t = yytext; return WORKERS; }
background-workers    { lval.t = yytext; return BACKGROUND_WORKERS; }
asynchronous-start    { lval.t = yytext; return ASYNC_START; }
user                  { lval.t = yytext; return USER; }
pidfile               { lval.t = yytext; return PIDFILE; }
rundir                { lval.t = yytext; return RUNDIR; }
ixfr-from-differences { lval.t = yytext; return BUILD_DIFFS; }
serial-policy         { lval.t = yytext; return SERIAL_POLICY; }
max-conn-idle         { lval.t = yytext; return MAX_CONN_IDLE; }
max-conn-handshake    { lval.t = yytext; return MAX_CONN_HS; }
max-conn-reply        { lval.t = yytext; return MAX_CONN_REPLY; }
max-tcp-clients       { lval.t = yytext; return MAX_TCP_CLIENTS; }
rate-limit            { lval.t = yytext; return RATE_LIMIT; }
rate-limit-size       { lval.t = yytext; return RATE_LIMIT_SIZE; }
rate-limit-slip       { lval.t = yytext; return RATE_LIMIT_SLIP; }
transfers             { lval.t = yytext; return TRANSFERS; }
dnssec-enable         { lval.t = yytext; return DNSSEC_ENABLE; }
dnssec-keydir         { lval.t = yytext; return DNSSEC_KEYDIR; }
signature-lifetime    { lval.t = yytext; return SIGNATURE_LIFETIME; }
query_module          { lval.t = yytext; return QUERY_MODULE; }

interfaces            { lval.t = yytext; return INTERFACES; }
address               { lval.t = yytext; return ADDRESS; }
port                  { lval.t = yytext; return PORT; }
via                   { lval.t = yytext; return VIA; }

control               { lval.t = yytext; return CONTROL; }
allow                 { lval.t = yytext; return ALLOW; }
listen-on             { lval.t = yytext; return LISTEN_ON; }

log                   { lval.t = yytext; return LOG; }

any                   { lval.t = C_ANY;  return LOG_SRC; }
server                { lval.t = C_SRV;  return LOG_SRC; }
zone                  { lval.t = C_ZONE; return LOG_SRC; }

stdout                { lval.t = yytext; return LOG_DEST; }
stderr                { lval.t = yytext; return LOG_DEST; }
syslog                { lval.t = yytext; return LOG_DEST; }

debug                 { lval.t = "debug";    return LOG_LEVEL; }
info                  { lval.t = "info";     return LOG_LEVEL; }
notice                { lval.t = "notice";   return LOG_LEVEL; }
warning               { lval.t = "warning";  return LOG_LEVEL; }
error                 { lval.t = "error";    return LOG_LEVEL; }
critical              { lval.t = "critical"; return LOG_LEVEL; }
all                   { lval.t = "info";     return LOG_LEVEL; }

increment|unixtime {
  if (strcmp(yytext, "increment") == 0) {
    lval.t = "increment";
  } else {
    lval.t = "unixtime";
  }
  return SERIAL_POLICY_VAL;
}

on|off {
  lval.t = yytext;
  lval.i = 0;
  if (strcmp(yytext, "on") == 0) {
    lval.i = 1;
  }
  return BOOL;
}

include BEGIN(include);

{DIGIT}+[smhd] {
  size_t mpos = strlen(yytext) - 1;
  char multiplier = yytext[mpos];
  yytext[mpos] = '\0';
  lval.i = atoi(yytext);
  if (lval.i < 1) {
	  cf_error(yyscanner, "interval must be a positive integer");
	  return END;
  }

  /* Handle multiplier. */
  switch(multiplier) {
  case 'm': lval.i *= 60; break; /* minutes */
  case 'h': lval.i *= 60*60; break; /* hours */
  case 'd': lval.i *= 24*60*60; break; /* days */
  case 's': /* seconds */
  default: break;
  }

  return INTERVAL;
}

{DIGIT}+[kMG] {
  size_t mpos = strlen(yytext) - 1;
  char multiplier = yytext[mpos];
  yytext[mpos] = '\0';
  lval.i = atol(yytext);
  if (lval.i < 1) {
	  cf_error(yyscanner, "size must be a positive integer");
	  return END;
  }

  /* Handle multiplier. */
  switch(multiplier) {
  case 'k': lval.l = lval.i * 1024; break; /* kB */
  case 'M': lval.l = lval.i * 1024*1024; break; /* MB */
  case 'G': lval.l = lval.i * 1024*1024*1024; break; /* GB */
  default: break;
  }

  return SIZE;
}

{DIGIT}+ {
  lval.i = atol(yytext);
  return NUM;
}

{DIGIT}+\.{DIGIT}+\.{DIGIT}+\.{DIGIT}+ {
  unsigned char buf[sizeof(struct in_addr)];
  if (inet_pton(AF_INET, yytext, buf)) {
	  lval.t = strdup(yytext);
	  return IPA;
  }
  cf_error(yyscanner, "Invalid IP address.");
}

\[({HEXA}*::|({HEXA}*:){3,})({HEXA}*|{DIGIT}+\.{DIGIT}+\.{DIGIT}+\.{DIGIT}+)\] {
  unsigned char buf[sizeof(struct in6_addr)];
  yytext[strlen(yytext)-1] = '\0';
  if (inet_pton(AF_INET6, yytext+1, buf)) {
	  lval.t = strdup(yytext+1);
	  return IPA6;
  }
  cf_error(yyscanner, "Invalid IPv6 address.");
  }

({HEXA}*::|({HEXA}*:){3,})({HEXA}*|{DIGIT}+\.{DIGIT}+\.{DIGIT}+\.{DIGIT}+) {
  unsigned char buf[sizeof(struct in6_addr)];
  if (inet_pton(AF_INET6, yytext, buf)) {
	  lval.t = strdup(yytext);
	  return IPA6;
  }
  cf_error(yyscanner, "Invalid IPv6 address.");
}

hmac-md5        { lval.t = strdup(yytext); return TSIG_ALGO_NAME; }
hmac-sha1       { lval.t = strdup(yytext); return TSIG_ALGO_NAME; }
hmac-sha224     { lval.t = strdup(yytext); return TSIG_ALGO_NAME; }
hmac-sha256     { lval.t = strdup(yytext); return TSIG_ALGO_NAME; }
hmac-sha384     { lval.t = strdup(yytext); return TSIG_ALGO_NAME; }
hmac-sha512     { lval.t = strdup(yytext); return TSIG_ALGO_NAME; }

["][^"\n]*["] {
  yytext[yyleng-1] = 0;
  lval.t = strdup(yytext + 1);
  return TEXT;
}

["][^"\n]*\n    cf_error(yyscanner, "Unterminated string.");

[a-zA-Z0-9\.\-\_]+ {
  lval.t = strdup(yytext);
  return TEXT /* Last resort, alphanumeric word. */;
}

: /* Optional : in assignments. */;

<<EOF>> {
	conf_includes_remove(yyextra->includes);

	yypop_buffer_state(yyscanner);

	if (!YY_CURRENT_BUFFER) {
		return END;
	}
}

<include>{BLANK}+
<include>["][^"\n]*["][;]{0,1} {
	BEGIN(INITIAL);

	// silently skip includes if there was a former error
	if (yyextra->error) {
		return END;
	}

	// remove optional semicolon
	if (yytext[yyleng - 1] == ';') {
		yyleng -= 1;
	}
	// remove quotes
	yytext += 1;
	yyleng -= 2;
	yytext[yyleng] = '\0';

	if (!conf_includes_push(yyextra->includes, yytext)) {
		cf_error(yyscanner, "include loop");
		return END;
	}

	// retrieved relative to previous config
	conf_include_t *inc = conf_includes_top(yyextra->includes);

	FILE *included = fopen(inc->filename, "r");
	if (!included) {
		cf_error(yyscanner, "cannot open file '%s'", inc->filename);
		conf_includes_remove(yyextra->includes);
		return END;
	}
	inc->handle = included;

	struct stat file_stat;
	if (fstat(fileno(included), &file_stat) == -1) {
		cf_error(yyscanner, "cannot stat file '%s'", yytext);
		conf_includes_remove(yyextra->includes);
		return END;
	}

	if (S_ISDIR(file_stat.st_mode)) {
		// Store dirname statically to reduce deallocation.
		char dirname[256] = { 0 };
		int ret = snprintf(dirname, sizeof(dirname), "%s",
		                   inc->filename);
		if (ret <= 0 || ret >= sizeof(dirname)) {
			cf_error(yyscanner, "dir name is too long '%s'",
			         inc->filename);
			return END;
		}

		// Remove include directory from the stack.
		conf_includes_remove(yyextra->includes);

		DIR *dp = opendir(dirname);
		if (dp == NULL) {
			cf_error(yyscanner, "cannot open directory '%s'",
			         dirname);
			return END;
		}

		struct dirent *ep;
		while ((ep = readdir(dp)) != NULL) {
			// Skip names with leading dot.
			if (*ep->d_name == '.') {
				continue;
			}

			char infile[256] = { 0 };
			int ret = snprintf(infile, sizeof(infile), "%s/%s",
			                   dirname, ep->d_name);
			if (ret <= 0 || ret >= sizeof(infile)) {
				cf_error(yyscanner, "cannot open file '%s/%s'",
				         dirname, ep->d_name);
				return END;
			}

			if (!conf_includes_push(yyextra->includes, infile)) {
				cf_error(yyscanner, "include loop");
				return END;
			}

			// retrieved relative to previous config
			conf_include_t *inc = conf_includes_top(yyextra->includes);

			FILE *included = fopen(inc->filename, "r");
			if (!included) {
				cf_error(yyscanner, "cannot open file '%s'",
				         inc->filename);
				conf_includes_remove(yyextra->includes);
				return END;
			}
			inc->handle = included;

			YY_BUFFER_STATE bs = yy_create_buffer(included,
			                                      YY_BUF_SIZE,
			                                      yyscanner);
			yypush_buffer_state(bs, yyscanner);
		}
		(void)closedir(dp);
	} else {
		YY_BUFFER_STATE bs = yy_create_buffer(included, YY_BUF_SIZE,
		                                      yyscanner);
		yypush_buffer_state(bs, yyscanner);
	}
}
<include>["][^"\n]*\n cf_error(yyscanner, "Unterminated string.");

%%
