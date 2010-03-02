/*
 * Copyright (c) 2009, 2010 bytemine GmbH <info@bytemine.net>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
	##   Author: Holger Rasch <rasch@bytemine.net>   ##
	##   http://www.bytemine.net                     ##
 */

/*
 *	ut: conf.l			-- HR09
 *
 *	config file parser
 *
 *
 *	[ flex input file ]
 *
 */


/*
 *	all error loggin in this file is done using 'tesc_emerg()'
 *	
 *	first, because 'mlpx_printf()' can only be used after
 *	'mlpx_init()' which depends on the config data built
 *	up here.
 *
 *	second, while 'mlpx_init()' could be split in two parts
 *	(config independant / config dependant) to make 'mlpx_printf()'
 *	available this early, the messages would simply fill the
 *	output queue (since 'tesc_main()', which does the actual
 *	writing, is not yet running). if a fatal error occured
 *	before the scheduler is finally running, those messages
 *	would be silently discarded.
 */


/***********************************************************************

config		= ka? ti? msg? log? channel*

ka		= "keepalive" num

ti		= "timeout" num

msg		= "msg" stringlist

log		= "log" string

channel		= "channel" string '{' type method msg? log? '}'

stringlist	= string | '{' string+ '}'

string		= '"' single-line-can-contain-backslash-dq '"'

type		= "type" ("VPNM" | "BACI" | "BASD" | "FLRD" | "FLWR" | string)

method		= "method" "{" ( unix | inet | popen | read | write ) "}"

msg		= "msg" stringlist

log		= "log" string

unix		= "unix" string

inet		= "inet" string num

popen		= "popen" string

read		= "read" string

write		= "write" string

num		= [1-9][0-9]*

***********************************************************************/


%{
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <string.h>
#include <errno.h>

#include "conf.h"
#include "mlpx.h"
#include "data.h"
#include "tesc.h"


/* tokens */
#define	T_EOF			0x00
#define	T_STRING		0x01
#define	T_channel		0x02
#define	T_begin			0x03
#define	T_end			0x04
#define	T_type			0x05
#define	T_type_vpnm		0x06
#define	T_type_baci		0x07
#define	T_type_basd		0x08
#define	T_type_flrd		0x09
#define	T_type_flwr		0x0a
#define	T_method		0x0b
#define	T_method_unix		0x0c
#define	T_method_inet		0x0d
#define	T_method_popen		0x0e
#define	T_method_read		0x0f
#define	T_method_write		0x10
#define	T_log			0x11
#define	T_msg			0x12
#define	T_NUM			0x13
#define	T_ERROR			0x14
#define	T_kal			0x15
#define	T_timo			0x16


/*
 *
 */
static int err (const char *s)
{
	mlpx_printf (CHN_MSG, MF_ERR, "line %d: %s\n", yylineno, s);
	return T_ERROR;
}


%}


%pointer
%option noyywrap
%option yylineno

/* comment to end of line */
CMT	#[^\n]*

/* string without closing quote, may include escaped dq ('\"') */
NCQ	\"([^\"\n]|\\\")*

/* whitespace */
WHT	[ \t\n]+

%%

{WHT}		/* skip */
{CMT}		/* skip */
 
{NCQ}\"		return T_STRING;
{NCQ}\n		return err ("unterminated string");

"keepalive"	return T_kal;
"timeout"	return T_timo;
"msg"		return T_msg;
"log"		return T_log;
"channel"	return T_channel;
 
"{"		return T_begin;
"}"		return T_end;
 
"type"		return T_type;
"VPNM"		return T_type_vpnm;
"BACI"		return T_type_baci;
"BASD"		return T_type_basd;
"FLRD"		return T_type_flrd;
"FLWR"		return T_type_flwr;

"method"	return T_method;
"unix"		return T_method_unix;
"inet"		return T_method_inet;
"popen"		return T_method_popen;
"read"		return T_method_read;
"write"		return T_method_write;
 
[1-9][0-9]*	return T_NUM;

[^ \t\n{}\"]+	return err ("syntax error");

%%


/* utility functions */

/*
 *	sec_malloc()
 *
 *	try to malloc, exit on failure with
 *	error message to stdout not stderr!
 *
 *	NEVER returns invalid ptr!
 */
void *sec_malloc (size_t size)
{
void *tmp;

    if ((tmp = malloc (size)) == NULL) {
	tesc_emerg (CHN_MSG, MF_ERR, "cannot allocated memory: %s\n",
						strerror(errno));
	tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }

    return tmp;
}


/*
 *	make_string()
 *
 *	allocate, copy, return -- config items will never be freed,
 *	even if errors occur (in the config)
 *
 *	if len == 0 strlen will be used, else 's' need
 *	not be 0 terminated
 *
 *	if strip, remove outer double quotes and replace \" with "
 *
 */
static const char *make_string (const char *s, size_t len, int strip)
{
unsigned i, j, e;
char *str;

    if (!len)
	len = strlen (s);

    ++len;	/* space for '\0' */

    str = sec_malloc (len);	/* may exit() */

    if (strip) {
	for (i = 1, j = 0, e = 0; i < len - 2; ++i)
	    if (!e) {	/* norm */
		if ((str[j++] = s[i]) == '\\')
		    ++e;
	    } else {	/* esc */
		if ((str[j++] = s[i]) == '"')
		    str[--j - 1] = '"';
		e = 0;
	    }
	str[j] = 0;
    } else {
	for (i = 0; i < len - 1; ++i)
	    str[i] = s[i];
	str[i] = 0;
    }

    return str;
}


/* ---------------------------------- */


/***
 ***	parser functions
 ***
 ***	all return 0 on success
 ***/


/*
 *	Pnum		-- parse number
 */
static int Pnum (int *num)
{
    if (yylex() != T_NUM) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: number expected\n", yylineno);
	return 1;
    }
    *num = atoi (yytext);	/* should be sufficient here */

    return 0;
}


/*
 *	Pslist		-- parse string list
 */
static int Pslist (struct strlist **msgp)
{
int t;
int n = 0;

    while ((t = yylex()) != T_EOF) {
	if (t == T_STRING) {
	    *msgp = sec_malloc (sizeof(struct strlist)); /* may exit() */
	    (*msgp)->next = 0;
	    (*msgp)->str = make_string (yytext, yyleng, 1); /* may exit() */
	    msgp = &(*msgp)->next;	/* prepare for next string */
	    ++n;
	    continue;
	}
	if (t == T_end) {
	    if (!n)
		tesc_emerg (CHN_MSG, 0,
				"line %d: empty message list\n", yylineno);
	    return 0;
	}
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: string expected\n", yylineno);
	return 1;
    }

    tesc_emerg (CHN_MSG, MF_ERR, "EOF while parsing message list\n");
    return 1;
}


/*
 *	Pmsg()		-- parse msg definition
 */
static int Pmsg (struct strlist **msgp)
{
int t;

    switch (t = yylex()) {
	case T_STRING :
	    *msgp = sec_malloc (sizeof(struct strlist)); /* may exit() */
	    (*msgp)->next = 0;
	    (*msgp)->str = make_string (yytext, yyleng, 1); /* may exit() */
	    return 0;
	case T_begin :
	    return Pslist (msgp);
	default :
	    break;
    }

    tesc_emerg (CHN_MSG, MF_ERR, "line %d: string or '{' expected\n",yylineno);
    return 1;
}


/*
 *	Plog()		-- parse logfile def
 */
static int Plog (const char **logp)
{
    if (yylex() != T_STRING) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: string expected\n", yylineno);
	return 1;
    }

    *logp = make_string (yytext, yyleng, 1);	/* may exit() */

    return 0;
}


/*
 *	Ptype		-- parse channel type
 */
static int Ptype (const char **t)
{
    switch (yylex()) {
	case T_type_vpnm :
	    *t = "VPNM";
	    break;
	case T_type_baci :
	    *t = "BACI";
	    break;
	case T_type_basd :
	    *t = "BASD";
	    break;
	case T_type_flrd :
	    *t = "FLRD";
	    break;
	case T_type_flwr :
	    *t = "FLWR";
	    break;
	case T_STRING :
	    *t = make_string (yytext, yyleng, 1);	/* may exit */
	    break;
	case T_EOF :
	    tesc_emerg (CHN_MSG, MF_ERR, "EOF while parsing channel type\n");
	    return 1;
	default :
	    tesc_emerg (CHN_MSG, MF_ERR,
				"line %d: expected channel type\n", yylineno);
	    return 1;
    }

    return 0;
}


/*
 *	Pm_xxx()	-- parse method def (for type != mtINET)
 */
static int Pm_xxx (struct method *m, mt_type_t type)
{
    m->type = type;

    if (yylex() != T_STRING) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: string expected\n", yylineno);
	return 1;
    }
    m->str = make_string (yytext, yyleng, 1);	/* may exit() */

    m->data = 0;	/* or -1 -- simply do not leave uninitialized */

    return 0;
}


/*
 *	Pm_inet()	-- parse (type == mtINET) method def
 */
static int Pm_inet (struct method *m)
{
    m->type = mtINET;

    if (yylex() != T_STRING) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: string expected\n", yylineno);
	return 1;
    }
    m->str = make_string (yytext, yyleng, 1);	/* may exit() */

    if (yylex() != T_NUM) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: number expected\n", yylineno);
	return 1;
    }
    m->data = atoi (yytext);	/* should be sufficient here */

    return 0;
}


/*
 *	Pmethod()	-- parse method definition
 */
static int Pmethod (struct method *m)
{
int r = 1;

    if (yylex() != T_begin) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: '{' expected\n", yylineno);
	return 1;
    }
    switch (yylex()) {
	case T_end :
	    tesc_emerg (CHN_MSG, MF_ERR,
			"line %d: empty method specification\n", yylineno);
	    return 1;
	case T_method_unix :
	    r = Pm_xxx (m, mtUNIX);
	    break;
	case T_method_popen :
	    r = Pm_xxx (m, mtPOPEN);
	    break;
	case T_method_read :
	    r = Pm_xxx (m, mtREAD);
	    break;
	case T_method_write :
	    r = Pm_xxx (m, mtWRITE);
	    break;
	case T_method_inet :
	    r = Pm_inet (m);
	    break;
	case T_EOF :
	    tesc_emerg (CHN_MSG, MF_ERR, "EOF in method specification\n");
	    return 1;
	default :
	    tesc_emerg (CHN_MSG, MF_ERR,
				"line %d: unexpected element\n", yylineno);
	    return 1;
    }
    if (yylex() != T_end) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: '}' expected\n", yylineno);
	return 1;
    }
    return r;
}


/*
 *	Pchannel()	-- parse channel definition
 */
static int Pchannel (struct chnlist **chlip)
{
int t;
int md = 0, ld = 0, mn = 0, tn = 0;
const char *tmp = 0;
struct channel *chan;

    t = yylex();
    if (t == T_begin) {
	tesc_emerg (CHN_MSG, 0,
			"line %d: missing label for channel\n", yylineno);
	tmp = "<no label specified>";		/* we never free the config! */
    }
    else if (t != T_STRING) {
	tesc_emerg (CHN_MSG, MF_ERR, "line %d: syntax error\n", yylineno);
	return 1;
    } else {
	tmp = make_string (yytext, yyleng, 1);	/* may exit() */
        if (yylex() != T_begin) {
	    tesc_emerg (CHN_MSG, MF_ERR, "line %d: '{' expected\n", yylineno);
	    return 1;
	}
    }

    /* allocate channel config */
    *chlip = (struct chnlist *) sec_malloc (sizeof(struct chnlist));
    (*chlip)->next = 0;
    chan = &(*chlip)->channel;
    chan->enabled = 0;
    chan->name = 0;
    chan->log = 0;
    chan->msg = 0;
    chan->type = "";	/* invalid */
    chan->method.type = -1;
    chan->method.str = 0;
    chan->method.data = 0;

    /* store label for channel */
    chan->name = tmp;

    while ((t = yylex()) != T_EOF)
	switch (t) {
	    case T_end :
		if (tn == 0) {
		    tesc_emerg (CHN_MSG, MF_ERR,
			"line %d: no channel type declared\n", yylineno);
		    return 1;
		}
		if (mn == 0) {
		    tesc_emerg (CHN_MSG, MF_ERR,
			"line %d: no channel method declared\n", yylineno);
		    return 1;
		}
		return 0;
	    case T_type :
		if (Ptype (&chan->type))
		    tesc_emerg (CHN_MSG, MF_ERR,
			"line %d: error in channel type definition\n",
								yylineno);
		else if (tn++)
		    tesc_emerg (CHN_MSG, 0,
			"line %d: channel type redefined\n", yylineno);
		break;
	    case T_method :
		if (Pmethod (&chan->method))
		    tesc_emerg (CHN_MSG, MF_ERR,
			"line %d: error in channel method definition\n",
								yylineno);
		else if (mn++)
		    tesc_emerg (CHN_MSG, 0,
			"line %d: channel method redefined\n", yylineno);
		break;
	    case T_msg :
		if (Pmsg (&chan->msg))
		    tesc_emerg (CHN_MSG, MF_ERR,
			"line %d: error in channel message definition\n",
								yylineno);
		else if (md++)
		    tesc_emerg (CHN_MSG, 0,
			"line %d: channel message redefined\n", yylineno);
		break;
	    case T_log :
		if (Plog (&chan->log))
		    tesc_emerg (CHN_MSG, MF_ERR,
			"line %d: error in channel logfile specification\n",
								yylineno);
		else if (ld++)
		    tesc_emerg (CHN_MSG, 0,
			"line %d: channel logfile redefined\n", yylineno);
		break;
	    default:
		tesc_emerg (CHN_MSG, MF_ERR,
				"line %d: unexpected element\n", yylineno);
		break;
	}

    tesc_emerg (CHN_MSG, MF_ERR, "EOF while parsing channel definition\n");
    return 1;
}


static void Pconfig (struct config *cf)
{
int t;
int md = 0, ld = 0, kd = 0, td = 0;
struct chnlist **chlip;

    chlip = &cf->channels;

    while ((t = yylex()) != T_EOF)
	switch (t) {
	    case T_kal :
		if (! Pnum (&cf->keepalive))
		    if (kd++)
			tesc_emerg (CHN_MSG, 0,
				"line %d: keepalive redefined\n", yylineno);
		break;
	    case T_timo :
		if (! Pnum (&cf->timeout))
		    if (td++)
			tesc_emerg (CHN_MSG, 0,
				"line %d: timeout redefined\n", yylineno);
		break;
	    case T_msg :
		if (Pmsg(&cf->msg))
		    tesc_emerg (CHN_MSG, 0,
				"error in top level message definition\n");
		else if (md++)
		    tesc_emerg (CHN_MSG, 0,
			"line %d: top level message redefined\n", yylineno);
		break;
	    case T_log :
		if (Plog (&cf->log))
		    tesc_emerg (CHN_MSG, 0,
				"error in top level logfile definition\n");
		else if (ld++)
		    tesc_emerg (CHN_MSG, 0,
			"line %d: top level logfile redefined\n", yylineno);
		break;
	    case T_channel :
		if (Pchannel(chlip)) {
		    tesc_emerg (CHN_MSG, 0,
				"error in channel definition\n");
		} else {
		    (*chlip)->channel.enabled = 1;
		    ++cf->nchan;
		}
		chlip = &(*chlip)->next;	/* prepare for next channel */
		break;
	    case T_ERROR:
		break;
	    default :
		tesc_emerg (CHN_MSG, MF_ERR,
					"line %d: syntax error\n", yylineno);
		break;
	}

    return;
}


/* ------------- */


/*
 *	conf_init()
 *
 *	parse config file and create internal config
 *	data from it
 *
 *	always returns valid ptr
 */
const struct config *conf_init (int fd)
{
struct config *cf;

    /* open config file and setup as flex input */
    if ((yyin = fdopen (fd, "r")) == NULL) {
	/* cannot open config */
	tesc_emerg (CHN_MSG, MF_ERR, "conf_init: %s", strerror(errno));
	tesc_emerg (CHN_MSG, MF_ERR, "cannot open config file\n");
	tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);		/* no use continuing */
    }

    /* allocate config root */
    cf = sec_malloc (sizeof (struct config));		/* may exit */

    cf->nchan = 0;
    cf->log = 0;
    cf->msg = 0;
    cf->channels = 0;
    cf->keepalive = UT_KEEPALIVE;		/* default */
    cf->timeout = UT_TIMEOUT;			/* default */

    /* parse config */
    Pconfig (cf);

    /* MUST NOT CLOSE the config fd - otherwise the lock would go */

    return cf;
}


/*** end ***/
