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
 *	ut: tesc.c			-- HR09
 *
 *	event/timeout driven scheduler
 *
 *	once 'tesc_main()' is called, control resides here;
 *	that is, everything ut does has to be implemented
 *	in functions which are triggered (directly and
 *	indirectly) from here.
 */

/*
 *	control/data flows (data moves with control):
 *
 *	- from main input
 *		fd -> buf(wait) -> demux ->
 *				[ filter -> ] (cmd-in | writeq/channel-fd)
 *
 *	- from sub (channel) input
 *		fd -> buf -> [ filter -> ] mux -> writeq/main-fd
 *
 * 	- (any) writeq -> fd
 *
 *
 *	furthermore: timed events called from here
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <string.h>
#include <errno.h>

#include "conf.h"
#include "mlpx.h"
#include "data.h"
#include "tesc.h"
#include "cmdi.h"

#ifndef INFTIM
#define INFTIM -1
#endif


/*
 *	file descriptor IO
 *
 *	input buffer, output queue and input function
 *	depending on fd type one of rb/wq may be 0
 */
struct fdio {
	chn_t		*ch;			/* backptr to channel	*/
						/* (fd stored there)	*/
	buf_t		*rb;			/* input buffer		*/
	bfifun_t	rf;			/* from fd to buf	*/
	msg_t		*wq;			/* output queue		*/
	msg_t		*wt;			/* last element of outq	*/
	struct timeval	ts;			/* last change		*/
	int		bw;			/* #chars already out	*/
	int		kf;			/* 'keep' flag		*/
};


/* used for ring (doubly linked list) of active fdios */
struct fdiodli {
	struct fdiodli	*next;			/* next in ring		*/
	struct fdiodli	*prev;			/* prev in ring		*/
	struct fdio	*fdio;			/* ptr to active fdio	*/
};


/* each channel can have 2 fds (actual channel and logfile) and they're	*/
/* allocated from the bottom (filling up 'holes'). so we allow for	*/
/* 2 * (CHN_MAX + 1) plus a small amount (8) to get away with a static	*/
/* fd -> fdio mapping table						*/
#define	FDMAPSIZ	(2 * CHN_MAX + 10)


/*
 *	timed event queue (scheduler data)
 */
struct teqi {
	struct teqi	*next;			/* sorted by time 	*/
	struct timeval	when;			/* abs time of event	*/
	timedev_t	*evnt;			/* event data		*/
};

/*
 *	main scheduler data
 */
struct tesc {
	struct fdio	*fdio[FDMAPSIZ];	/* all possible fdios 	*/
	struct fdiodli	*ring;			/* ring of active fdios	*/
	int 		numact;			/* # of active fdios	*/
	struct teqi	*teq;			/* timed event queue	*/
	int		timeout;		/* main IO timeout -	*/
						/*  only for tesc_emerg	*/
};

/* the main data structure (internal) for everything 'tesc_' */
static struct tesc	schdat;


/*
 *	ring_insert()	-- insert element in ring of active fdios
 *	[private]
 */
static void ring_insert (struct fdio *fdio)
{
struct fdiodli *tmp;

    tmp = (struct fdiodli *)sec_malloc (sizeof(struct fdiodli)); /* may exit */

    if (schdat.ring) {
	/* insert 'last' in ring */
	tmp->fdio = fdio;
	tmp->next = schdat.ring;
	tmp->prev = schdat.ring->prev;
	tmp->prev->next = tmp;
	schdat.ring->prev = tmp;
    } else {
	tmp->fdio = fdio;
	tmp->next = tmp;
	tmp->prev = tmp;
	schdat.ring = tmp;
    }

    ++schdat.numact;
}


/*
 *	ring_remove()	-- remove element from ring of active fdios
 *	[private]
 */
static void ring_remove (struct fdio *fdio)
{
struct fdiodli *tmp;

    if (! schdat.ring)
	/* there is no ring */
	return;

    /* find this fdio */
    for (tmp = schdat.ring; tmp->fdio != fdio; tmp = tmp->next) {
	if (tmp->next == schdat.ring)
	    /* it is the 'last' one -> not found */
	    return;
    }

    /* tmp->fdio == fdio */
    if (tmp == tmp->next) {	/* only one in ring */
	free (tmp);
	schdat.ring = 0;
    } else {
	tmp->prev->next = tmp->next;
	tmp->next->prev = tmp->prev;
	if (tmp == schdat.ring)
	    schdat.ring = tmp->next;
	free (tmp);
    }

    --schdat.numact;
}


/*
 *	new_fdio()	-- allocate and initialize fdio
 *	[private]
 */
static struct fdio *new_fdio (chn_t *ch)
{
struct fdio *tmp;

    tmp = (struct fdio *) sec_malloc (sizeof(struct fdio));	/* may exit */

    tmp->ch = ch;
    tmp->rb = 0;
    tmp->rf = 0;
    tmp->wq = 0;
    tmp->wt = 0;
    tmp->bw = 0;
    tmp->kf = 0;

    return tmp;
}


/*
 *	tesc_emerg()
 *
 *	(try to) output a string on the main out, bypassing
 *	the scheduler, buffers and any dynamic allocation.
 *	intended for (fatal) error messages directly before
 *	exiting.
 *
 *	correctly prefixes the message and can cope with
 *	partially written messages in write queue.
 *
 *	despite the name, this function can be used
 *	even BEFORE tesc_init() (or any other initialization)!
 *
 *	only CHN_CMD and CHN_MSG are valid targets!
 */
void tesc_emerg (int id, int flags, const char *fmt, ...)
{
static char buf[1024];	/* output will be truncated if longer */
va_list ap;
char *cp;
int l;
size_t siz, off;
char tc;
time_t st, ct;

    if (id != CHN_CMD && id != CHN_MSG) {
	/* invalid channel, fatal */
	exit (EXIT_FAILURE);
    }

    siz = sizeof(buf);

    if (schdat.fdio[1] && schdat.fdio[1]->bw) {
	/* incomplete line written, insert '\n' before output */
	l = snprintf (buf, siz, "\n!%02X! output interrupted\n", CHN_MSG);
	cp = buf + l;
	siz -= l;
	/* now reset the buffer of the current output msg_t	*/
	/* this will avoid non prefixed output, but produce	*/
	/* duplicated output (the already written part of	*/
	/* the current message					*/
	schdat.fdio[1]->bw = 0;
    } else
	cp = buf;

    /* 'type' of prefix */
    tc = mlpx_prfxtc (flags);

    /* put prefix to buf */
    l = snprintf (cp, siz, "%c%02X%c ", tc, id, tc);
    cp += l;
    siz -= l;

    /* put the actual string to buf */
    va_start(ap, fmt);
    l = vsnprintf (cp, siz, fmt, ap);
    va_end(ap);
    siz -= l;

    /*
     * main output is in nonblocking mode, try to write
     * the (complete) message out for N (see below) seconds
     * if we fail to get the full message out in time,
     * we regard this as a fatal error and bail out!
     * if the stall-detection timeout is set (!= 0),
     * we use it, else we wait for 30 seconds only.
     */

    if ((st = time (0)) == -1)
	exit (EXIT_FAILURE);
    for (off = 0; /**/; /**/) {
	if ((ct = time (0)) == -1)
	    exit (EXIT_FAILURE);
	if (ct - st > (schdat.timeout ? schdat.timeout : 30))
	    exit (EXIT_FAILURE);

	/* channel id is used only for prefix, stdout is hardcoded */
	l = write (1, buf + off, sizeof(buf) - siz);
	if (l != -1) {
	    off += l;
	    if (off < sizeof(buf) - siz)
		continue;
	    else
		break;		/* we got the whole msg out */
	}

	if (errno != EAGAIN)	/* real error */
	    exit (EXIT_FAILURE);
    }

    return;	/* success */
}


/*
 *	tesc_add_reader()
 *
 *	register 'rf' to be called with arg 'b' whenever
 *	data is available for read on 'ch->fd'
 *
 *	buffer 'b' need not be kept elsewhere since
 *	it will be deleted in 'tesc_del_reader()'
 *
 *	returns 0 on success, -1 on error
 */
int tesc_add_reader (chn_t *ch, bfifun_t rf, buf_t *b)
{
int fd = ch->fd;

    if (fd < 0 || fd >= FDMAPSIZ) {
	/* illegal fd */
	return -1;
    }

    if (schdat.fdio[fd]) {
	/* fdio structure already exists */
	if (schdat.fdio[fd]->rf) {
	    /* huh, reader exists already */
	    return -1;
	}
    } else {
	/* allocate fdio structure */
	schdat.fdio[fd] = new_fdio (ch);		/* may exit */
    }

#ifdef DEBUG
    if (! (ch->flags & CHN_F_RD)) {
	/* not open for reading */
	if (schdat.fdio[fd]->kf) {
	    schdat.fdio[fd]->rf = 0;
	    schdat.fdio[fd]->rb = 0;
	}
	return -1;
    }
#endif

    /* store func and buf */
    schdat.fdio[fd]->rf = rf;
    schdat.fdio[fd]->rb = b;

    /* add it to the list of active fdios */
    ring_insert (schdat.fdio[fd]);			/* may exit */

    return 0;
}


/*
 *	tesc_del_reader()
 *
 *	unregisters the reader for 'fd'
 *	and delete the read buffer
 *
 *	returns 0 on success, -1 on error
 */
int tesc_del_reader (const chn_t *ch)
{
int fd = ch->fd;

    if (fd < 0 || fd >= FDMAPSIZ) {
	/* illegal fd */
	return -1;
    }

    if (! schdat.fdio[fd]) {
	/* no fdio */
	return -1;
    }
    if (! schdat.fdio[fd]->rf) {
	/* no reader */
	return -1;
    }

    /* delete the read buffer */
    data_del_buf (schdat.fdio[fd]->rb);

    if (! schdat.fdio[fd]->wq && ! schdat.fdio[fd]->kf) {
	/* remove it from the list of active fdios */
	ring_remove (schdat.fdio[fd]);
	/* delete the fdio structure */
	free (schdat.fdio[fd]);
	schdat.fdio[fd] = 0;
    } else {
	/* reset the reader related fields */
	schdat.fdio[fd]->rb = 0;
	schdat.fdio[fd]->rf = 0;
    }

    return 0;
}


/*
 *	tesc_keep()
 *
 *	set flag to not delete the corresponding fdio
 *	structure if no reader is set and write queue
 *	is empty.
 *
 *	does NOT allocate fdio structure (silent ignore)
 */
void tesc_keep (const chn_t *ch)
{
    if (ch->fd < 0 || ch->fd >= FDMAPSIZ)
	/* illegal fd */
	return;

    if (schdat.fdio[ch->fd])
	schdat.fdio[ch->fd]->kf = 1;

    return;
}


/*
 *	tesc_enq_wq()
 *
 *	enqueues the message 'm' for output via 'ch'
 *
 *	if queue was empty and channel requests
 *	stall-detection, record time of insertion
 *	in the respective fdio.
 *
 *	returns 0 on success, -1 on error
 */
int tesc_enq_wq (chn_t *ch, msg_t *m)
{
int fd = ch->fd;
struct fdio *fdio;

    if (fd < 0 || fd >= FDMAPSIZ) {
	/* illegal fd */
	return -1;
    }

#ifdef DEBUG
    if (! (ch->flags & CHN_F_WR)) {
	/* not open for writing */
	return -1;
    }
#endif

    if (! schdat.fdio[fd]) {
	/* allocate fdio structure */
	schdat.fdio[fd] = new_fdio (ch);		/* may exit */
	/* add it to the list of active fdios */
	ring_insert (schdat.fdio[fd]);			/* may exit */
    }

    if (!m) 		/* used by mlpx_init to (only) create fdio */
	return 0;

    /* finally append to 'wq' and record time if needed */
    fdio = schdat.fdio[fd];

    if (ch->timeout && !fdio->wq) {
	/* stall-detection timeout is set and queue was empty: record time */
	if (gettimeofday (&schdat.fdio[fd]->ts, 0) == -1) {
	    mlpx_printf (CHN_MSG, MF_ERR,
		"tesc_enq_wq(): cannot get current time: %s\n",
							strerror (errno));
	}
    }

    if (!fdio->wq) {
	fdio->wq = m;
	fdio->wt = m;
    } else {
	fdio->wt->next = m;
	fdio->wt = m;
    }

    return 0;
}


/*
 *	tesc_del_wq()
 *
 *	delete the write queue for the specified fd (via 'ch')
 *
 *	returns 0 on success, -1 on error
 */
int tesc_del_wq (const chn_t *ch)
{
int fd = ch->fd;
msg_t *cur, *nxt;

    if (fd < 0 || fd >= FDMAPSIZ) {
	/* illegal fd */
	return -1;
    }

    if (! schdat.fdio[fd]) {
	/* we don't even have an fdio */
	return -1;
    }

    /* delete all elements of 'wq' */ 
    cur = schdat.fdio[fd]->wq;
    while (cur) {
	nxt = cur->next;
	free (cur);
	cur = nxt;
    }

    schdat.fdio[fd]->wq = 0;
    schdat.fdio[fd]->wt = 0;
    schdat.fdio[fd]->kf = 0;

    /* if no reader, delete fdio structure (ignore keep flag) */
    if (! schdat.fdio[fd]->rf) {
	if (schdat.fdio[fd]->rb) {
	    /* hey! that must not be */
	    data_del_buf (schdat.fdio[fd]->rb);
	}
	/* remove from list of active fdios */
	ring_remove (schdat.fdio[fd]);
	/* delete the fdio structure */
	free (schdat.fdio[fd]);
	schdat.fdio[fd] = 0;
    }

    return 0;
}


/*
 *	tvless()
 *	[private]
 *
 *	compare two timevals, return 1 if first is
 *	smaller than second
 */
static int tvless (const struct timeval *t1, const struct timeval *t2)
{
    if (t1->tv_sec < t2->tv_sec)
	return 1;

    if (t1->tv_sec == t2->tv_sec && t1->tv_usec < t2->tv_usec)
	return 1;

    return 0;
}


/*
 *	tvdiff()
 *	[private]
 *
 *	return difference t1 and t2 in milli seconds (i.e., (t2-t1)ms)
 *	rounded down		** no overflow checking **
 */
static int tvdiff (const struct timeval *t1, const struct timeval *t2)
{
int r;

    r = (t2->tv_sec - t1->tv_sec) * 1000;
    r += (t2->tv_usec - t1->tv_usec) / 1000;

    return r;
}



/*
 *	tesc_timedev()
 *
 *	schedule a timed event (relative time)
 *
 *	returns 0 on success, -1 on error
 *
 */
int tesc_timedev (timedev_t *evnt)
{
struct teqi *ne, **ipos;

    /* allocate queue item */
    ne = sec_malloc (sizeof *ne);		/* may exit */
    ne->next = 0;

    /* get current time */
    if (gettimeofday (&ne->when, 0) == -1) {
	mlpx_printf (CHN_MSG, MF_ERR,
			"tesc_timedev(): cannot get current time: %s\n",
							strerror (errno));
	free (ne);
	return -1;
    }

    /* calculate time of event */
    ne->when.tv_sec += evnt->inms / 1000;
    ne->when.tv_usec += 1000 * (evnt->inms % 1000);
    ne->when.tv_sec += ne->when.tv_usec / 1000000;
    ne->when.tv_usec %= 1000000;

    /* set event data */
    ne->evnt = evnt;

    /* insert in queue (at the right place) */
    if (!schdat.teq)
	schdat.teq = ne;
    else {
	for (ipos = &schdat.teq; *ipos; ipos = &(*ipos)->next)
	    if (tvless (&ne->when, &(*ipos)->when)) {
		ne->next = *ipos;
		*ipos = ne;
		return 0;
	    }
	*ipos = ne;
    }

    return 0;
}


/*
 *	tesc_log()
 *
 *	output 'm' to the log fd of 'ch'
 *	prepend extra prefix to denote whether this
 *	was read from or written to the channel
 *
 *	no timeouts / no nonblocking writes - we assume
 *	that (in normal operation) we do not block
 *	on disk writes
 */
void tesc_log (msg_t *m, chn_t *ch, int dir)
{
const char pin[] = "<";
const char pout[] = ">";
struct iovec iov[2];

    iov[0].iov_base = (char*) (dir == LOG_DIR_IN ? pin : pout);
    iov[0].iov_len = (dir == LOG_DIR_IN ? sizeof(pin) : sizeof(pout)) - 1;

    iov[1].iov_base = (m->flags & MF_PLAIN ? m->data : m->prefix);
    iov[1].iov_len = m->len;

    if (writev (ch->log, iov, 2) == -1) {
	/* huh, record error so it can be handled in mlpx_update() */
	ch->flags |= CHN_ERR_L;
	ch->e_log = errno;
    }

    return;
}


/*
 *	tesc_main()
 *
 *	scheduler main loop -- this is the place where
 *	all actions are triggered.
 */
void tesc_main ()
{
int i, r, l;
struct pollfd *fds = 0;
int nfds = 0;
struct fdiodli *cur;
struct fdio *fdio;
char *cp;
msg_t *m;
int dlen;
struct teqi *te;
timedev_t *evnt;
struct timeval now;
int ptimo, stimo, to;
int gtoderr;
short rev;
orn_t orn;


    /*
     *	scheduler main loop, the heart of this program
     */

    while (1) {

	/*
	 *	1 -- setup for poll()
	 */

	if (schdat.numact > nfds) {
	    /* allocate a bigger array */
	    if (fds)
		free (fds);
	    nfds = schdat.numact;
	    fds = sec_malloc (nfds * sizeof(*fds));	/* may exit */
	}

	/* get the current time */
	if (gettimeofday (&now, 0) == -1) {
	    mlpx_printf (CHN_MSG, MF_ERR, "tesc_main(): "
			"cannot get current time: %s\n", strerror(errno));
	    gtoderr = 1;
	} else
	    gtoderr = 0;

	/*
	 * Go through the active fdios and prepare for the
	 * poll() step. For each one with a (non empty) write
	 * queue and stall-detection enabled: check if timeout
	 * is reached (and call the respective stalled() if so).
	 * Calculate the time until the next timeout.
	 */
	stimo = INFTIM;
	cur = schdat.ring;
	for (i = 0; i < schdat.numact; ++i) {
	    /* setup poll args for this fdio */
	    fds[i].fd = cur->fdio->ch->fd;
	    fds[i].events = 0;
	    if (cur->fdio->ch->flags & CHN_F_IP)
		fds[i].events |= POLLIN | POLLOUT;
	    else {
	      if (cur->fdio->rf)
		fds[i].events |= POLLIN;
	      if (cur->fdio->wq) {
		fds[i].events |= POLLOUT;

		/* if there stall-detection is enabled for this one ... */
		if ((to = cur->fdio->ch->timeout) && !gtoderr) {

		    /* ... check if timeout is reached */
		    to *= 1000;
		    if (tvdiff (&cur->fdio->ts, &now) > to) {

			/* call the stalled() func if set */
			if (cur->fdio->ch->stalled)
			    cur->fdio->ch->stalled (cur->fdio->ch);

		    } else {
			/* calculate next timeout */
			to -= tvdiff (&cur->fdio->ts, &now);
			if (to < stimo)
			    stimo = to;
		    }
		}

		/* if gtoderr is set, we cannot perform stall detection	*
		 * but we will not poll with INFTIM -- that is handled	*
		 * in the timed event section below			*/
	      }
	    }
	    cur = cur->next;
	}

#ifdef DEBUG
	if (cur != schdat.ring)
	    /* schdat.numact != elements in ring */
	    exit (1);
#endif

	/* timed event handling */
	if (schdat.teq) {
	    te = schdat.teq;

	    /* calculate time until next timed event */
	    if (gtoderr) {
		ptimo = 1000;	/* check at least once per second */
	    } else {
		if (tvless (&te->when, &now))
		    ptimo = 0;
		else {
		    ptimo = te->when.tv_sec - now.tv_sec;
		    ptimo *= 1000;
		    ptimo += (te->when.tv_usec - now.tv_usec) / 1000;
		}
	    }
	} else
	    ptimo = INFTIM;

	/* take the lesser of stimo/ptimo as timeout for poll() */
	if (stimo != INFTIM && stimo < ptimo)
	    ptimo = stimo;


	/*
	 *	2 -- poll()
	 */

	r = poll (fds, schdat.numact, ptimo);
	if (r == -1) {
	    /* poll() failed */
	    tesc_emerg (CHN_MSG, MF_ERR, "poll(): %s\n", strerror(errno));
	    tesc_emerg (CHN_MSG, 0, "ptimo = %d\n", ptimo);
	    sleep (1);	/* do not spam with errors */
	    continue;	/* start over */
	}


	/*
	 *	3 -- perform any timed event which is up now
	 */
	 
	/* timeout reached */
	if (gettimeofday (&now, 0) == -1) {
		mlpx_printf (CHN_MSG, MF_ERR,
			"tesc_main(): cannot get current time: %s\n",
							strerror(errno));
	} else {
	    for (te = schdat.teq; te; /**/) {
		if (! tvless (&te->when, &now))
		    break;

		evnt = te->evnt;
		/* remove and free this queue item */
		schdat.teq = te->next;
		free (te);
		te = schdat.teq;

		/* perform whatever is set */
		evnt->func (evnt);
	    }
	}

        /* if we had a timeout, skip rest of loop */
	if (r == 0)
	    continue;


	/*
	 *	4 -- perform the resp. actions for all reported events
	 */

	for (i = 0; i < schdat.numact; ++i) {
	    fdio = schdat.fdio[fds[i].fd];
#ifdef DEBUG
	    if (fdio->ch->fd != fds[i].fd)
		/* fd mismatch! */
		exit (1);
#endif
	    rev = fds[i].revents;

	    if (rev & (POLLERR|POLLHUP|POLLNVAL)) {
		/* special condition encountered	*/
		/* record it in fdio for later stages	*/
		fdio->ch->pxfl = rev;
		fdio->ch->flags |= CHN_ERR_P;
	    }
	    if (rev & POLLERR) {
		/* "error has occurred on the device or socket" */
	    }
	    if (rev & POLLHUP) {
		/* "device or socket has been disconnected" */
	    }
	    if (rev & POLLNVAL) {
		/* huh, the fd we passed is invalid! */
		continue;
	    }

	    if ((fdio->ch->flags & CHN_F_IP) && (rev & (POLLIN | POLLOUT))) {
		/* results for a open/connect which was in progess until now */
		orn.ch = fdio->ch;
		orn.revents = rev;
		cmdi_notify (orn);
		continue;		/* done for now */
	    }

	    if (rev & POLLIN) {		/* there's something to read */
		/* read it (and forward to next stage, if possible) */
		switch (fdio->rf (fds[i].fd, fdio->rb, fdio->ch)) {
		    case 0 :	/* ok */
			break;
		    case -1 : 	/* error */
			/* read failed .... */
			/* and no, can/must not be EAGAIN (we poll()ed) */
			fdio->ch->e_rd = errno;
			fdio->ch->flags |= CHN_ERR_R;
			break;
		    case -2 :	/* EOF */
			fdio->ch->flags |= CHN_EOF;
			break;
		    default :
			/* illegal return code */
			exit (2);
		}
	    } /* if POLLIN */

	    if (rev & POLLOUT) { /* write possible */

		dlen = fdio->wq->len - fdio->bw;
		if (fdio->wq->flags & MF_PLAIN)
		    cp = fdio->wq->data + fdio->bw;
		else
		    cp = fdio->wq->prefix + fdio->bw;
		
		l = write (fds[i].fd, cp, dlen);
		if (l == -1) {
		    /* write failed */
		    /* and no, can/must not be EAGAIN (we poll()ed) */
		    fdio->ch->e_wr = errno;
		    fdio->ch->flags |= CHN_ERR_W;
		} else {

		    /* we actually got something out ... */
		    if (fdio->ch->timeout) {
			/* ... and stall-detection is enabled */

			/* -> update timestamp for this fdio */
			fdio->ts = now;		/* should be new enough */
		    }

		    /* bookkeeping, cleanup, logging */
		    if (l == dlen) {
			/* head of queue completely out ... */ 
			m = fdio->wq;

			if (fdio->ch->log != -1)
			    /* ... log this one */
			    tesc_log (m, fdio->ch, LOG_DIR_OUT);

			/* ... remove it */
			fdio->wq = m->next;
			free (m);
			fdio->bw = 0;	/* reset */
			if (!fdio->wq)
			    fdio->wt = 0;
		    } else {
			fdio->bw += l;
		    }
		}
	    } /* if POLLOUT */

	    /*
	     * update channel if needed:
	     * - handle EOF condition
	     * - print error messages
	     * ...
	     */
	    if (fdio->ch->flags & CHN_NEED_UPD)
		mlpx_update (fdio->ch);

	} /* for (all reported events) */


	/*
	 *	5 -- final steps before we start over
	 */

	cmdi_cmd();	/* run the command interpreter */

	if (schdat.numact == 0) {
	    /* huh, no active fdios left -> we're done */
	    break;
	}

    } /* while(1) */


    /* no cleanup, we'll terminate soon */

    return;
}


/*
 *	tesc_init()
 *
 *	initialize the scheduler
 */
void tesc_init(const struct config *cf)
{
int i;

    /* initialize our private data */

    for (i = 0; i < FDMAPSIZ; ++i)
	schdat.fdio[i] = 0;

    schdat.ring = 0;
    schdat.numact = 0;
    schdat.teq = 0;
    schdat.timeout = cf->timeout;

    return;
}


/*** end ***/
