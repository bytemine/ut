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
 *	ut: data.c			-- HR09
 *
 *	buffer related stuff
 */

#include <stdio.h>	/* FIXME */
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "conf.h"
#include "mlpx.h"
#include "data.h"
#include "tesc.h"


/*
 *	new_sbuf()	-- allocate and initialize struct sbuf
 *	[private]
 */
static struct sbuf *new_sbuf()
{
struct sbuf *sb;

    sb = sec_malloc (SBISIZ);		/* may exit */

    /* initialize */
    sb->next = 0;
    sb->fdata = sb->data;
    sb->ffree = sb->fdata;
    sb->flen = SBISIZ - sizeof(struct sbuf);

    return sb;
}


/*
 *	new_msg()	-- allocate and initialize msg_t
 *	[private]
 */
static msg_t *new_msg(size_t dsiz)
{
int i;
msg_t *m;

    if ((int)dsiz < 0)	/* malformed line (shorter than prefix) */
	dsiz = 0;	/* always allocate full prefix */

    m = sec_malloc (sizeof(msg_t) + dsiz);	/* may exit */

    /* initialize */
    m->next = 0;
    m->flags = 0;
    m->len = 0;	/* actual size is 'dsiz', but data is not yet init'ed */
    for (i = 0; i < PRFXLEN; ++i)
	m->prefix[i] = 0;
    /* data is left uninitialized */

    return m;
}


/*
 *	do_output()
 *	[private]
 *
 *	output a msg_t with data size 'len', remove
 *	the data from the buffer 'b'
 *
 *	if flag MF_PLAIN is not set, data from buffer is expected to
 *	contain prefix (and be copied accordingly)
 *
 *	return 1, if buffer was reset or freed
 */
static int do_output (buf_t *b, chn_t *ch, size_t len, int flags)
{
msg_t *m;
int dlen;
size_t i;
char *from, *to;
struct sbuf *sb;
int rff = 0;

    dlen = flags & MF_PLAIN ? len : len - PRFXLEN;

    if (flags & MF_NONL)
	++dlen;		/* add space for '\n' */

    m = new_msg (dlen);				/* may exit */

    sb = b->sbh;
    to = flags & MF_PLAIN ? m->data : m->prefix;

    /* copy the data (and free emptied buffers) */
    for (i = 0, from = sb->fdata; i < len; ++i) {
	if (from == sb->ffree) {
	    /* we reached the end of this buffer */
	    if (sb->next) {
		/* advance to the next one */
		sb = sb->next;
		from = sb->fdata;

		/* and delete the empty one (it is always the head) */
		free (b->sbh);
		b->sbh = sb;	/* and replace head */
		rff = 1;

		continue;	/* continue with next buffer */
	    } else {
		tesc_emerg (CHN_MSG, MF_ERR, "do_output(): internal error\n");
		tesc_emerg (CHN_MSG, MF_EOF, "\n");
		exit (1);
	    }
	}

	*to++ = *from++;
    }

    if (flags & MF_NONL) {
	*to = '\n';
	++len;
    }

    /* complete the message */
    m->len = len;	/* NOT dlen */
    m->flags = flags;

    /* adjust buffer */
    if (from == sb->ffree) {
	/* we have output all the buffer held, i.e., either */
	/* - the buffer contained exactly one complete line */
	/*   (rather typical pattern) */
        /* -  we output from a non-wait buffer */
	if (! sb->next) {
	    /* this is the last buffer, reset it, but do not delete */
	    sb->fdata = sb->data;
	    sb->ffree = sb->fdata;
	    sb->flen = SBISIZ - sizeof(struct sbuf);
	} else {
	    /* delete it and replace the head */
	    free (b->sbh);
	    b->sbh = sb;
        }
	rff = 1;
    } else {
	/* from now points to the first not-yet-copied char */
	/* its now the new 'start of data', but other fields don't change */
	sb->fdata = from;
    }

    /* log this msg (as input) if this channel has a logfile */
    if (ch->log != -1)
	tesc_log (m, ch, LOG_DIR_IN);

    /* and onward to the next stage */
    b->out (m, ch);

    return rff;
}

/*
 *	try_output()
 *	[private]
 *
 *	check if there are lines to wrap up in msg_t
 *	and forward to next stage (if any)
 *
 *	if data is output, it is removed from the buffer
 */
static void try_output (buf_t *b, chn_t *ch)
{
size_t len;
char *cp;
struct sbuf *sb;

    len = 0;
    sb = b->sbh;

    /* search for a '\n', count length */
    for (cp = sb->fdata; /**/; ++cp) {
	if (cp == sb->ffree) {
	    /* we reached the end of this buffer */
	    if (sb->next) {
		sb = sb->next;
		cp = sb->fdata;
		continue;	/* continue with next buffer */
	    } else {
		/* done */
		break; /* for */
	    }
	}

	++len;	/* valid char */

	if (*cp == '\n') {
	    /* found a complete line -> forward it */
	    /* len includes the '\n' */
	    if (do_output (b, ch, len, b->plain ? MF_PLAIN : 0)) {
		/* buffer was reset/freed, need to adjust ptrs */
		sb = b->sbh;
		cp = sb->fdata - 1;	/* ++cp on end of loop */
	    }
	    len = 0;	/* reset */
	}
    }

    if (b->wait)
	return;		/* all complete lines were output (poss. none) */

    if (len) {
	/* there's still something left in the buffer */
	/* forward all we got */
	(void) do_output (b, ch, len, (b->plain ? MF_PLAIN : 0) | MF_NONL);
    }

    return;
}


/*
 *	data_buf_input()
 *
 *	read from fd and store in buffer
 *	if 'wait' is false, immediately call the 'out' function,
 *	else check if there are complete lines in the buffer
 *	and output them (if any). 'ch' is used to identify the
 *	channel (passed from fdio structure / tesc_main())
 *	
 *	the 'out' function might be called multiple times, if
 *	there is more than one complete line in the buffer.
 *
 *	(if wait is false, we cannot have more than one sub buffer)
 *
 *	retval: 0 ok, -1 error, -2 EOF
 */
int data_buf_input (int fd, buf_t *b, chn_t *ch)
{
int l;

    if (!b->cur)
	b->cur = new_sbuf();				/* may exit */
    else if (b->cur->flen < SBIMIN) {
	/* we want to be able to read at least SBIMIN bytes */
	/*     --> allocate new sub buffer */
#ifdef DEBUG
	if (b->cur->next) {
	    /* next non 0, buffer corruption */
	    tesc_emerg (CHN_MSG, MF_ERR, "data_buf_input(): buffer corrupt\n");
	    tesc_emerg (CHN_MSG, MF_EOF, "\n");
	    exit (1);
	}
#endif 
	b->cur->next = new_sbuf();		/* may exit */
	b->cur = b->cur->next;
    }

    /* do a maximum size read */
    if ((l = read (fd, b->cur->ffree, b->cur->flen)) == -1) {
	/*
	 * read error on 'fd' -- since we should have come here
	 * from a successful poll() for 'fd' this probably means
	 * we fumbled in tesc_main(), or the params were rotten
	 * (i.e., the buffer is corrupted), hmm
	 */
	mlpx_printf (ch->id, MF_ERR, "read(): %s\n", strerror(errno));
	return -1;
    }

    if (l == 0) {
	/* end of file */
	return -2;
    }

    /* adjust the buffer params accordingly */
    b->cur->ffree += l;
    b->cur->flen -= l;

    /* try to forward some/all data */
    try_output (b, ch);

    return 0;
}


/*
 *	data_new_buf()
 *
 *	allocate and initialize a buf_t
 */
buf_t *data_new_buf (bfofun_t out, int wait, int plain)
{
buf_t *b;

    b = sec_malloc (sizeof(buf_t));		/* may exit */

    b->out = out;
    b->wait = wait;
    b->plain = plain;

    b->sbh = new_sbuf();			/* may exit */
    b->cur = b->sbh;

    return b;
}


/*
 *	data_del_buf()
 *
 *	delete the buffer
 */
void data_del_buf (buf_t *b)
{
struct sbuf *sbp, *tmp;

    if (!b)
	return;

    for (sbp = b->sbh; sbp; /**/) {
	tmp = sbp;
	sbp = sbp->next;
	free (tmp);
    }

    return;
}


/*** end ***/
