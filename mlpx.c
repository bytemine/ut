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
 *	ut: mlpx.c			-- HR09
 *
 *	channels, mux/demux
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "conf.h"
#include "mlpx.h"
#include "data.h"
#include "tesc.h"
#include "cmdi.h"
#include "util.h"


static chn_t ch_main_in;		/* 'fake' channels for the	*/
static chn_t ch_main_out;		/*   main input/output fds	*/

static const struct config *cf = 0;	/* config data			*/
static chn_t *chmap[CHN_MAX + 1];	/* channel id -> chn_t		*/


void mux (msg_t *, const chn_t *);


/*
 *	mlpx_id2chn()
 *
 *	map channel id to chn_t
 *
 *	return ptr to chn_t or 0 channel does not exist
 */
chn_t *mlpx_id2chn (int id)
{
    if (id < 0 || id > CHN_MAX)
	return 0;	/* invalid id */

    return chmap[id];
}


/*
 *	mlpx_add_reader()
 *
 *	install input buffer and read function for
 *	(sub) channel
 */
void mlpx_add_reader (chn_t *ch)
{
buf_t *b;

    b = data_new_buf (mux, 0, 1);	/* !wait, plain */
    tesc_add_reader (ch, data_buf_input, b);

    return;
}


/*
 *	mlpx_prfxtc()
 *
 *	return framimg char for prefix type
 */
char mlpx_prfxtc (int flags)
{
int tc;

    if (flags & MF_EOF) 
	tc = '.';
    else if (flags & MF_ERR)
	tc = '!';
    else if (flags & MF_NONL)
	tc = '_';
    else
	tc = '>';

    return tc;
}


/*
 *	mlpx_printf()
 *
 *	formatted output to main out,
 *	prefix according to 'ch' (id) and 'flags'
 *	(flag MF_PLAIN will be set here)
 *
 *	(size is limited to MLPX_PRINTF_MAX bytes)
 */
#define MLPX_PRINTF_MAX		1024
void mlpx_printf (int id, int flags, const char *fmt, ...)
{
va_list ap;
msg_t *m;
int l;

    if (!chmap[id]) {
	/* invalid channel */
	if (id == CHN_MSG)		/* huh, must not happen */
	    exit (EXIT_FAILURE);	/* avoid inf. recursion */

	mlpx_printf (CHN_MSG, MF_ERR,
			"internal error: channel %02X does not exist\n", id);
	return;
    }

    va_start(ap, fmt);

    m = sec_malloc (sizeof(msg_t) + MLPX_PRINTF_MAX + 1);	/* may exit */
    m->next = 0;

    l = vsnprintf (m->data, MLPX_PRINTF_MAX + 1, fmt, ap);
    va_end(ap);

    m->len = l;
    m->flags = flags | MF_PLAIN;	/* no prefix yet */

    mux (m, chmap[id]);

    return;
}


/*
 *	mlpx_print_msg()
 *
 *	output a 'struct strlist' line by line
 *	prepend "# " to each line
 */
void mlpx_print_msg (int id, struct strlist *msg)
{
struct strlist *sl;

    for (sl = msg; sl; sl = sl->next)
	mlpx_printf (id, 0, "# %s\n", sl->str);

    return;
}


/*
 *	mlpx_setup_ch()
 *
 *	(after successful open) setup fdio
 *	for this channel, open logfile (if defined),
 *	output channel message/motd (if defined)
 */
void mlpx_setup_ch (chn_t *ch)
{
    if (ch->flags & CHN_F_RD)
	/* create the input buffer (and fdio) for this channel */
	mlpx_add_reader (ch);
    else {
	tesc_enq_wq (ch, 0);		/* 0 msg, to create fdio */
	tesc_keep (ch);			/* do not delete fdio */
    }

    /* open logfile if configured */
    if (ch->cf->log)
        if((ch->log = open(ch->cf->log,O_WRONLY|O_APPEND|O_CREAT,0644)) == -1)
            mlpx_printf (CHN_MSG, MF_ERR, "logfile open: %s: %s\n",
                                                ch->cf->log, strerror(errno));

    /* output channel message/motd here (if defined) */
    if (ch->cf->msg)
        mlpx_print_msg (CHN_CMD, ch->cf->msg);

    return;
}


/*
 *	mlpx_cleanup_ch()
 *
 *	perform all necessary cleanup for a channel
 *	after its file descriptor has been closed
 *
 *	does NOT close the fd itself!
 */
void mlpx_cleanup_ch (chn_t *ch)
{
    /* remove reader and write queue (if they exists) */
    (void) tesc_del_reader (ch);
    (void) tesc_del_wq (ch);		/* deletes fdio structure */

    /* mark channel inactive */
    ch->flags = 0;
    ch->fd = -1;

    /* close/free/mark inactive for logfile (if open) */
    if (ch->log != -1) {
	(void) close (ch->log);		/* FIXME? */
	ch->log = -1;
    }

    return;
}


/*
 *	mlpx_update()
 *
 *	called from tesc_main() when some exceptional
 *	condition for the channel needs to be handled
 *	(eof/hangup, read/write error)
 *
 *	currently only EOF is handled, errors are
 *	silently cleared
 */
void mlpx_update (chn_t *ch)
{
int id = ch->id;

    if (ch->flags & CHN_ERROR) {
	/* some read/write/logfile/poll error */
	/* currently we do not report this */

		/* FIXME: perhaps we need to close the channel	*/
		/* if we cannot write the logfile?		*/

	/* clear flags */
	ch->flags &= ~(CHN_ERROR);
    }

    if (ch->flags & CHN_EOF) {
	(void) close (ch->fd);

	if (id == CHN_MAIN) {
	    /* we take this as a 'quit' command */
	    /* the EOF was on the input side, atleast try to output */
	    tesc_emerg (CHN_MSG, 0, "EOF on main input\n");
	    tesc_emerg (CHN_MSG, MF_EOF, "\n");
	    tesc_emerg (CHN_CMD, MF_EOF, "\n");
	    exit (0);
	}

	/* special handling for 'popen' channels */
	if (ch->flags & CHN_F_PROC)
	    cmdi_inst_reaper (ch->pid);

	mlpx_printf (CHN_MSG, 0, "EOF on channel %02x\n", ch->id);
	mlpx_printf (ch->id, MF_EOF, "\n");

	/* channel is closed */
	mlpx_cleanup_ch (ch);		/* resets all flags */
    }

    return;
}


/*
 *	mux()
 *
 *	construct and add prefix, forward to main out
 */
void mux (msg_t *m, const chn_t *ch)
{
char tc;

#ifdef DEBUG
    if (! (m->flags & MF_PLAIN)) {
	/* cannot have prefix yet */
        tesc_emerg (CHN_MSG, MF_ERR, "mux(): cannot have prefix yet!\n");
	tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }
    if (ch->id < 0 || ch->id > CHN_MAX) {
	/* illegal id */
        tesc_emerg (CHN_MSG, MF_ERR, "mux(): illegal id\n");
	tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }
#endif

    /* 'type' of prefix */
    tc = mlpx_prfxtc (m->flags);

    /* fill in prefix */
    if (! chmap[ch->id]) {
	/* no such channel */
        tesc_emerg (CHN_MSG, MF_ERR, "mux(): no such channel\n");
	tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }
#ifdef DEBUG
    if (PRFXLEN != 5) {
	/* huh! prefix length does not match the following part */
        tesc_emerg (CHN_MSG, MF_ERR, "mux(): internal error\n");
	tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }
#endif
    m->prefix[0] = tc;
    m->prefix[1] = hexdigit (ch->id / 16);
    m->prefix[2] = hexdigit (ch->id % 16);
    m->prefix[3] = tc;
    m->prefix[4] = ' ';

    m->flags &= ~MF_PLAIN;
    m->len += PRFXLEN;

    /* and off to main out */
    if (tesc_enq_wq (&ch_main_out, m)) {
	/* failed */
        tesc_emerg (CHN_MSG, MF_ERR, "mux(): test_enq_wq() failed\n");
	tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }

    return;
}


/*
 *	demux()
 *
 *	analyse prefix and forward accordingly
 *	(to channel output queues or cmd input queue)
 */
void demux (msg_t *m, const chn_t *ch /* unused */)
{
int id;
(void)ch;/* avoid warnings */

#ifdef DEBUG
    if (PRFXLEN != 5) {
	/* huh! prefix length does not match the following part */
        tesc_emerg (CHN_MSG, MF_ERR, "demux(): internal error\n");
        tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }
    if (m->flags & MF_PLAIN) {
        tesc_emerg (CHN_MSG, MF_ERR, "demux(): plain message\n");
        tesc_emerg (CHN_MSG, MF_EOF, "\n");
	exit (1);
    }
#endif
    if (m->len < 6) {
	/* illegal input, expected at least prefix + '\n', i.e. len == 6 */
        mlpx_printf (CHN_MSG, MF_ERR,
		"demux(): illegal input (too short to have valid prefix)\n");
	free (m);
	return;
    }

    /* check prefix */
    if (m->prefix[0] != '<' || m->prefix[3] != '<' || m->prefix[4] != ' ') {
	/* illegal prefix, wrong framing chars for id */
        mlpx_printf (CHN_MSG, MF_ERR,
		"demux(): illegal prefix (wrong framing chars)\n");
	free (m);
	return;
    }

    /* check id part */
    if (! (ishexdigit (m->prefix[1]) && ishexdigit (m->prefix[2]))) { 
	/* illegal char for prefix */
        mlpx_printf (CHN_MSG, MF_ERR,
		"demux(): illegal prefix (garbled channel id)\n");
	free (m);
	return;
    }

    /* convert id */
    id = hexd2int (m->prefix[1]) * 16 + hexd2int (m->prefix[2]);

    /* check if channel is valid and open for writing */
    if (! chmap[id]) {
	/* channel does not exist */
        mlpx_printf (CHN_MSG, MF_ERR,
		"demux(): channel %02X does not exist\n", id);
	free (m);
	return;
    }
    if (chmap[id]->flags & CHN_F_IP) {
	/* open/connect still in progress */
	mlpx_printf (CHN_MSG, MF_ERR,
		"demux(): channel %02X not yet ready\n", id);
	free (m);
	return;
    }
    if (! (chmap[id]->flags & CHN_F_WR)) {
	/* channel is not writeable */
        mlpx_printf (CHN_MSG, MF_ERR,
		"demux(): channel %02X not open for writing\n", id);
	free (m);
	return;
    }

    /* ignore prefix from now on */
    m->flags |= MF_PLAIN;
    m->len -= PRFXLEN;

    /* forward to output or cmd */
    if (id == CHN_CMD) {
	cmdi_enque (m);
    } else if (id == CHN_MSG) {
	/* simply echo back */
	mux (m, chmap[CHN_MSG]);
    } else
        tesc_enq_wq (chmap[id], m);

    return;
}


/*
 *	mainout_stalled()	[private]
 */
static void mainout_stalled (chn_t *ch)
{
    if (ch != &ch_main_out) 	/* paranoia */
	tesc_emerg (CHN_MSG, MF_ERR, "mainout_stalled(): wrong channel\n");

		/* FIXME - once we have logfiles, record it there */

    /* for now we can only exit silently */
    exit (1);
}


/*
 *	mlpx_init()
 *
 *	initial setup multiplexer,
 *	'master' channels (stdin/stdout) only
 *
 *	output startup message declaring available channels
 *	(FIXED FORMAT)
 */
void mlpx_init (const struct config *conf)
{
int i;
int fdfl;
buf_t *b;
int logfd = -1;
struct chnlist *chli;

    cf = conf;


    /*
     *	if global logging is on, open the logfile
     */

    if (cf->log)
	if ((logfd = open (cf->log, O_WRONLY|O_APPEND|O_CREAT, 0644)) == -1)
	    tesc_emerg (CHN_MSG, MF_ERR, "logfile open: %s: %s\n",
						cf->log, strerror(errno));


    /*
     *	setup main input
     */

    /* construct channel info */
    ch_main_in.flags = CHN_F_RD;	/* read only */
    ch_main_in.id = CHN_MAIN;		/* fake / not in chmap */
    ch_main_in.fd = 0;			/* use stdin */
    ch_main_in.log = logfd;
    ch_main_in.pxfl = 0;
    ch_main_in.cf = 0;			/* no config */
    ch_main_in.timeout = 0;		/* no stall-detection */
    ch_main_in.stalled = 0;

    b = data_new_buf (demux, 1, 0);	/* wait, !plain */
    tesc_add_reader (&ch_main_in, data_buf_input, b);


    /*
     *	setup main output
     */

    /* construct channel info */
    ch_main_out.flags = CHN_F_WR;	/* write only */
    ch_main_in.id = CHN_MAIN;		/* fake / not in chmap */
    ch_main_out.fd = 1;			/* use stdout */
    ch_main_out.log = logfd;
    ch_main_out.pxfl = 0;
    ch_main_out.cf = 0;
    ch_main_out.timeout = cf->timeout;	/* config:timeout for stall-detect */
    ch_main_out.stalled = mainout_stalled;

    tesc_enq_wq (&ch_main_out, 0);	/* 0 msg, to create fdio */
    tesc_keep (&ch_main_out);		/* do not delete fdio */

    /* put fd into nonblocking mode (mainly for tesc_emerg()) */
    if ((fdfl = fcntl (ch_main_out.fd, F_GETFL)) == -1) {
	tesc_emerg (CHN_MSG, MF_ERR,
			"mlpx_init: F_GETFL failed: %s\n", strerror (errno));
	exit (EXIT_FAILURE);
    }
    fdfl |= O_NONBLOCK;
    if (fcntl (ch_main_out.fd, F_SETFL, fdfl) == -1) {
	tesc_emerg (CHN_MSG, MF_ERR,
			"mlpx_init: F_SETFL failed: %s\n", strerror (errno));
	exit (EXIT_FAILURE);
    }


    /*
     *	init channel map and insert all channels defined in
     *	config. here the channel ids are determined.
     */

    /* clear map */
    for (i = 0; i <= CHN_MAX; ++i)
	chmap[i] = 0;

    /* insert command channel (fake) - used only as a marker in chmap */
    chmap[CHN_CMD] = sec_malloc (sizeof(chn_t));	/* may exit */
    chmap[CHN_CMD]->flags = CHN_F_WR;	/* for demux() */
    chmap[CHN_CMD]->id = CHN_CMD;
    chmap[CHN_CMD]->fd = -1;		/* not used */
    chmap[CHN_CMD]->log = -1;		/* not used */
    chmap[CHN_CMD]->pxfl = 0;		/* not used */
    chmap[CHN_CMD]->cf = 0;		/* not used */
    chmap[CHN_CMD]->timeout = 0;	/* not used */
    chmap[CHN_CMD]->stalled = 0;	/* not used */

    /* insert msg channel (fake) - used only as a marker in chmap */
    chmap[CHN_MSG] = sec_malloc (sizeof(chn_t));	/* may exit */
    chmap[CHN_MSG]->flags = CHN_F_WR;	/* for demux() */
    chmap[CHN_MSG]->id = CHN_MSG;
    chmap[CHN_CMD]->fd = -1;		/* not used */
    chmap[CHN_MSG]->log = -1;		/* not used */
    chmap[CHN_MSG]->pxfl = 0;		/* not used */
    chmap[CHN_MSG]->cf = 0;		/* not used */
    chmap[CHN_CMD]->timeout = 0;	/* not used */
    chmap[CHN_CMD]->stalled = 0;	/* not used */

    /* insert all defined channels */
    for (i = 0, chli = cf->channels; chli; chli = chli->next) {
	if (i == CHN_CMD || i == CHN_MSG)	/* skip, already in use */
	    ++i;
	if (i > CHN_MAX) {
	    /* too many channels defined */
		/* FIXME */
	    break;	/* skip the rest */
	}
	if (! chli->channel.enabled)
	    continue;	/* incomplete / failed parse */

	/* allocate chn_t / assign channel id */
	chmap[i] = sec_malloc (sizeof(chn_t));		/* may exit */
	chmap[i]->flags = 0;		/* not active */
	chmap[i]->id = i;
	chmap[i]->fd = -1;		/* closed */
	chmap[i]->log = -1;		/* closed */
	chmap[i]->pxfl = 0;
	chmap[i]->cf = &chli->channel;
	chmap[i]->timeout = 0;		/* disable */
	chmap[i]->stalled = 0;
	++i;
    }


    /*
     *	report available channels with their id
     *	this is the FIXED FORMAT startup message
     */

    /* header */
    mlpx_printf (CHN_CMD, 0, "### UT VERSION %s ###\n", UT_VERSION);
    mlpx_printf (CHN_CMD, 0, "CMD %02X MSG %02X\n", CHN_CMD, CHN_MSG);

    /* available channels */
    mlpx_printf (CHN_CMD, 0, "CHANNELS:\n");
    for (i = 0; i <= CHN_MAX; ++i) {
	if (i == CHN_CMD || i == CHN_MSG)
	    continue;	/* do not report these, they appeared in line 2 */
	if (chmap[i])
	    mlpx_printf (CHN_CMD, 0, "%02X %s \"%s\"\n", i,
					chmap[i]->cf->type, 
					chmap[i]->cf->name);
    }
    mlpx_printf (CHN_CMD, 0, "\n");

    /* output message/motd here (if defined) */
    if (cf->msg)
	mlpx_print_msg (CHN_CMD, cf->msg);

    mlpx_printf (CHN_CMD, 0, "READY\n");

    return;
}


/*** end ***/
