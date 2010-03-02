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

#ifndef CONF_H
#define CONF_H
/*
	##   Author: Holger Rasch <rasch@bytemine.net>   ##
	##   http://www.bytemine.net                     ##
 */

/*
 *	ut: conf.h			-- HR09
 *
 *	definitions for config (file) parsing / config usage
 */


#ifndef UT_CONFIG_PATH
#define	UT_CONFIG_PATH		"ut.conf"
#endif

#ifndef UT_KEEPALIVE
#define UT_KEEPALIVE	0	/* off */
#endif

#ifndef UT_TIMEOUT
#define UT_TIMEOUT	0	/* off */
#endif


/* (internally obsolete) channel types - 	*/
/*	       can still be specified in config	*/
/* -------------------------------------------- */
/*	VPNM	openvpn management interface	*/
/*	BACI	ba-config interface		*/
/*	BASD	ba-statusd (output?)		*/
/*	FLRD	general file access READ	*/
/*	FLWR	general file access WRITE	*/
/* -------------------------------------------- */

/* method types */
typedef enum {
	mtUNIX,				/* unix domain socket		*/
	mtINET,				/* inet socket			*/
	mtPOPEN,			/* stdin/stdout of a process	*/
	mtREAD,				/* file (-system object) read	*/
	mtWRITE				/* file (-system object) write	*/
} mt_type_t;

struct strlist {
	struct strlist	*next;		/* next item (0 == end of list)	*/
	const char	*str;		/* actual string		*/
};

struct method {
	mt_type_t	type;		/* socket, pipe, file, etc.	*/
	const char	*str;		/* path, hostname, command, ...	*/
	int		data;		/* flags, port #, ...		*/
};

struct channel {
	int		enabled;	/* parser internal use		*/
	const char	*name;		/* channel name/label		*/
	const char	*log;		/* path to channel log file	*/
	struct strlist	*msg;		/* channel spec. startup msg	*/
	const char	*type;		/* 'what is on the other end'	*/
	struct method	method;		/* 'transport' spec		*/
};

struct chnlist {
	struct chnlist	*next;		/* next item (0 == end of list)	*/
	struct channel	channel;	/* config for one channel	*/
};

struct config {
	int		nchan;		/* total number of channels	*/
	const char	*log;		/* path to (main) log file	*/
	struct strlist	*msg;		/* (main) startup message	*/
	struct chnlist	*channels;	/* list of channel configs	*/
	int		keepalive;	/* keepalive interval (or 0)	*/
	int		timeout;	/* timeout (0: disable)		*/
};


extern void *sec_malloc (size_t);
extern const struct config *conf_init (int);


#endif /* ! CONF_H */
