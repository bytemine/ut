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
 *	ut: cmdi.c			-- HR09
 *
 *	command interface
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <poll.h>
#include <errno.h>

#include "conf.h"
#include "mlpx.h"
#include "data.h"
#include "tesc.h"
#include "cmdi.h"
#include "util.h"


static int cmdi_open (int, char **);
static int cmdi_close (int, char **);
static int cmdi_quit (int, char **);


/* type for command functions */
typedef int (*cmd_fun) (int, char**);

/* command table */
static struct {
	const char	*name;
	cmd_fun		func;
} cmd_commands[] = {
	{ "open", cmdi_open },
	{ "close", cmdi_close },
	{ "quit", cmdi_quit },
	{ 0, 0 }
};


static msg_t *cmd_input = 0;		/* cmd input queue	*/

/*
 *	cmdi_enque()
 *
 *	append a msg to input queue of command interpreter
 */
void cmdi_enque (msg_t *m)
{
msg_t **iqp = &cmd_input;

    while (*iqp)
	iqp = &(*iqp)->next; /* walk to end (normally queue should be empty) */

    *iqp = m;

    return;
}


/* list of notifications for cmdi_cmd() */
static struct ornli {
	struct ornli	*next;			/* next in list		*/
	orn_t		orn;			/* actual result	*/
} *orn_list = 0;

/*
 *	cmdi_notify()
 *
 *	store result from asynchronous operation
 *	use in cmdi_cmd()
 */
void cmdi_notify (orn_t orn)
{
struct ornli *new;

    /* allocate element */
    new = sec_malloc (sizeof(struct ornli));		/* may exit */

    /* fill in result and prepend to list */
    new->next = orn_list;
    new->orn = orn;
    orn_list = new;

    return;
}


/*
 *	cmdi_open_UNIX()	[private]
 *
 *	unix domain socket access
 *
 *	return value like cmdi_open()
 */
static int cmdi_open_UNIX (chn_t *ch)
{
size_t l;
struct sockaddr_un a;
int on = 1;

    if ((l = strlen (ch->cf->method.str)) > sizeof(a.sun_path)) {
	mlpx_printf (CHN_MSG, MF_ERR, "socket path to long (%d)\n", l);
	return -1;
    }

#ifndef __linux__
    a.sun_len = l;
#endif
    a.sun_family = AF_UNIX;
    strncpy (a.sun_path, ch->cf->method.str, sizeof(a.sun_path));

    /* open up the connection */
    if ((ch->fd = socket (AF_LOCAL, SOCK_STREAM, 0)) == -1) {
	mlpx_printf (CHN_MSG, MF_ERR, "socket(): %s\n", strerror(errno));
	return -1;
    }
    /* make it nonblocking */
    if (ioctl (ch->fd, FIONBIO, &on) == -1) {
	mlpx_printf (CHN_MSG, MF_ERR, "set FIONBIO: %s\n", strerror(errno));
	(void) close (ch->fd);
	ch->fd = -1;
	return -1;
    }
    if (connect (ch->fd, (struct sockaddr *)&a, sizeof a) == -1) {
	if (errno == EINPROGRESS) {
	    ch->flags |= CHN_F_RD | CHN_F_WR | CHN_F_CIP;
	    return -2;	/* cannot complete immediately */
	}

	mlpx_printf (CHN_MSG, MF_ERR, "connect(): %s\n", strerror(errno));
	(void) close (ch->fd);
	ch->fd = -1;
	return -1;
    }

    /* mark channel open R/W */
    ch->flags = CHN_F_RD | CHN_F_WR;

    /* buffers, logfile, motd */
    mlpx_setup_ch (ch);

    return 0;
}


/*
 *	cmdi_open_INET()	[private]
 *
 *	inet (v4)  socket access
 *
 *	return value like cmdi_open()
 */
static int cmdi_open_INET (chn_t *ch)
{
struct sockaddr_in a;
int on = 1;

    /* fill in sockaddr_in with required params */
    switch (inet_pton (AF_INET, ch->cf->method.str, &a.sin_addr)) {
	case -1 :
	    mlpx_printf (CHN_CMD, MF_ERR, "open %02X: %s\n",
						ch->id, strerror(errno));
	    return -1;

	case 0 :
	    mlpx_printf (CHN_CMD, MF_ERR, "open %02X: config error - "
						"invalid address\n", ch->id);
	    return -1;

	case 1 :
	    break;

	default :
	    mlpx_printf (CHN_CMD, MF_ERR,
				"open %02X: internal error\n", ch->id);
	    return -1;
    }
    a.sin_family = AF_INET;
    a.sin_port = htons(ch->cf->method.data);

    /* open up the connection */
    if ((ch->fd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
	mlpx_printf (CHN_CMD, MF_ERR, "open %02X: socket(): %s\n",
						ch->id, strerror(errno));
	return -1;
    }

    /* make it nonblocking */
    if (ioctl (ch->fd, FIONBIO, &on) == -1) {
	mlpx_printf (CHN_CMD, MF_ERR, "open %02X: set FIONBIO: %s\n",
						ch->id, strerror(errno));
	(void) close (ch->fd);
	ch->fd = -1;
	return -1;
    }

    /* try to establish connection */
    if (connect (ch->fd, (struct sockaddr *)&a, sizeof a) == -1) {
	if (errno == EINPROGRESS) {
	    ch->flags |= CHN_F_RD | CHN_F_WR | CHN_F_CIP;
	    return -2;	/* cannot complete immediately */
	}

	mlpx_printf (CHN_CMD, MF_ERR, "open %02X: connect(): %s\n",
						ch->id, strerror(errno));
	(void) close (ch->fd);
	ch->fd = -1;
	return -1;
    }

    /* mark channel open R/W */
    ch->flags = CHN_F_RD | CHN_F_WR;

    /* buffers, logfile, motd */
    mlpx_setup_ch (ch);

    return 0;
}


/*
 *	popen_child_setup()	[private]
 *
 *	close all fds except the argument 'fd'
 *	setup 'fd' as stdin/stdout/stderr
 *	exec '/bin/sh -c' with the configure string
 *
 *	does not return - exits on error
 */
static void popen_child_setup (chn_t *ch, int fd)
{
int i;
struct rlimit r;
FILE *out;
const char *cmd;

    cmd = ch->cf->method.str;

    /* the only way to output error messages is via the 'fd' */
    if ((out = fdopen (fd, "w")) == NULL)
	exit (EXIT_FAILURE);

    /* close all fds except 'fd' */
    if (getrlimit (RLIMIT_NOFILE, &r) == -1) {
	fprintf (out, "proc setup: getrlimit(): %s\n", strerror(errno));
	fflush (out);
	_exit (EXIT_FAILURE);
    }
    for (i = 0; i < (int) r.rlim_cur; ++i)
	if (i == fd)
	    continue;
	else
	    (void) close (i);

    /* setup stdin/stdout/stderr */
    for (i = 0; i < 3; ++i)
	if (dup2 (fd, i) == -1) {
	    fprintf (out, "proc setup: dup2(): %s\n", strerror(errno));
	    fflush (out);
	    _exit (EXIT_FAILURE);
	}
    if (fd > 2)		/* should always be the case */
	(void) fclose (out);

    /* everything is setup, now exec a shell with the configured cmd */
    if (execl ("/bin/sh", "[ut] sh", "-c", cmd, (char*) 0) == -1) {
	fprintf (stderr, "proc setup: execl(): %s\n", strerror(errno));
	exit (EXIT_FAILURE);
    }

    return;	/* not reached */
}


/*
 *	cmdi_open_POPEN()	[private]
 *
 *	similar to 'popen()', but:
 *	 - no STDIO streams
 *	 - using 'socketpair()'
 *	 - merge other process' stdout/stderr
 *
 *	return value like cmdi_open()
 */
static int cmdi_open_POPEN (chn_t *ch)
{
pid_t pid;
int sp[2];

    if (socketpair (AF_LOCAL, SOCK_STREAM, PF_UNSPEC, sp) == -1) {
	mlpx_printf (CHN_CMD, MF_ERR, "open %02X: socketpair(): %s\n",
						ch->id, strerror(errno));
	ch->fd = -1;
	return -1;
    }

    switch ((pid = fork())) {
	case -1:
	    mlpx_printf (CHN_CMD, MF_ERR, "open %02X: fork(): %s\n",
						ch->id, strerror(errno));
	    (void) close (sp[0]);
	    (void) close (sp[1]);
	    ch->fd = -1;
	    return -1;

	case 0:
	    (void) close (sp[0]);
	    popen_child_setup (ch, sp[1]);	/* does not return */
	    exit (EXIT_FAILURE); 

	default:
	    break;
    }

    /* parent side setup */
    (void) close (sp[1]);
    ch->fd = sp[0];
    ch->pid = pid;

    /* mark channel open R/W with process */
    ch->flags = CHN_F_RD | CHN_F_WR | CHN_F_PROC;

    /* buffers, logfile, motd */
    mlpx_setup_ch (ch);

    return 0;
}


/*
 *	arg2chn()		[private]
 *
 *	convert (hex) string to channel,
 *	return 0 on failure.
 *
 *	(outputs error msgs on failure)
 */
static chn_t *arg2chn (const char *s)
{
int id;
chn_t *ch;

    /* (try to) get channel */
    id = hexd2int (s[0]) * 16 + hexd2int (s[1]);
    ch = mlpx_id2chn (id);

    if (!ch) {
	mlpx_printf (CHN_MSG, MF_ERR, "no such channel\n");
	return 0;
    }

    return ch;
}


/*
 *	cmdi_open()		[private]
 *
 *	open command, args:
 *	1: channel id
 *
 *	returns: 0 ok, -1 error, -2 inprogress
 */
static int cmdi_open (int ac, char *av[])
{
int r;
chn_t *ch;

    if (ac < 2) {
	mlpx_printf (CHN_MSG, MF_ERR,
				"missing channel argument for %s\n", av[0]);
	return -1;
    } else if (ac > 2)
	mlpx_printf (CHN_MSG, 0, "extra args for command %s ignored\n", av[0]);

    /* (try to) get channel to open */
    if (! (ch = arg2chn (av[1])))
	return -1;

    if ((ch->flags & CHN_F_ACT)) {
	/* channel is already open */
	mlpx_printf (CHN_MSG, MF_ERR, "channel %02X is already open\n",ch->id);
	return -1;
    }

    /* now jump to the method specific part */
    switch (ch->cf->method.type) {
	case mtUNIX:
	    r = cmdi_open_UNIX (ch);
	    break;

	case mtINET:
	    r = cmdi_open_INET (ch);
	    break;

	case mtPOPEN:
	    r = cmdi_open_POPEN (ch);
	    break;

	case mtREAD:
	case mtWRITE:
	default:
	    mlpx_printf (CHN_MSG, MF_ERR, "the access method defined for"
				" this channel is not implemented, sorry\n");
	    return -1;
    }

    if (r == -2) {
	/* we need an fdio in order be noticed by scheduler */
	tesc_enq_wq (ch, 0);
	tesc_keep (ch);

    } else if (r == -1)
	mlpx_printf (CHN_MSG, MF_ERR, "open channel %s failed\n", av[1]);

    return r;	/* result from method specific part */
}


/* data for reaper() */
struct rdat {
	pid_t	pid;
	int	step;
};

/*
 *	reaper()		[private]
 *
 *	timed event to terminate process and
 *	collect its exit status
 */
static void reaper (timedev_t *me)
{
struct rdat *rd;
pid_t pid;
int status;
int done;

    done = 0;
    status = 0;
    rd = (struct rdat*) me->data;

    /* always check if process terminated */
    if ((pid = waitpid (rd->pid, &status, WNOHANG)) == -1) {
	mlpx_printf (CHN_MSG, MF_ERR, "waitpid(): %s\n", strerror(errno));

    } else if (pid) {
	if (WIFSTOPPED(status)) {
	    mlpx_printf (CHN_MSG, 0, "pid %u stopped\n", pid);
	} else if (WIFSIGNALED(status)) {
	    mlpx_printf (CHN_MSG, 0, "pid %u terminated by signal %d%s\n",
				pid, WTERMSIG(status),
				WCOREDUMP(status) ? " core dumped" : "");
	    done = 1;
        } else if (WIFEXITED(status)) {
	    mlpx_printf (CHN_MSG, 0, "pid %u exited (%d)\n",
						pid, WEXITSTATUS(status));
	    done = 1;
	} else {
	    /* should not happen */
	    mlpx_printf (CHN_MSG, MF_ERR, "unknown status code "
					"0x%08x for pid %u\n", status, pid);
	    done = 1; /* we ignore the pid from now on */
	}
    }

    if (!done) {
	/* process has not yet terminated */

	switch (rd->step) {
	    case 1:
		mlpx_printf (CHN_MSG, 0, "sending SIGHUP to %u\n", rd->pid);
		if (kill (rd->pid, SIGHUP) == -1) {	/* its /bin/sh */
		    if (errno == ESRCH && pid != 0) {
			/* process is gone */
			me->inms = 100;
			break;
		    } else {
			mlpx_printf (CHN_MSG, MF_ERR, "kill() pid %u: %s\n",
						rd->pid, strerror(errno));
		    }
		}

		/* try again in 10 seconds */
		me->inms = 10000;
		break;

	    case 2:
		mlpx_printf (CHN_MSG, 0, "sending SIGTERM to %u\n", rd->pid);
		if (kill (rd->pid, SIGTERM) == -1) {
		    if (errno == ESRCH && pid != 0) {
			/* process is gone */
			me->inms = 100;
			break;
		    } else {
			mlpx_printf (CHN_MSG, MF_ERR, "kill() pid %u: %s\n",
						rd->pid, strerror(errno));
		    }
		}

		/* try again in 20 seconds */
		me->inms = 20000;
		break;
		
	    case 3:
		mlpx_printf (CHN_MSG, 0, "sending SIGKILL to %u\n", rd->pid);
		if (kill (rd->pid, SIGKILL) == -1) {
		    if (errno == ESRCH && pid != 0) {
			/* process is gone */
			me->inms = 100;
			break;
		    } else {
			mlpx_printf (CHN_MSG, MF_ERR, "kill() pid %u: %s\n",
						rd->pid, strerror(errno));
		    }
		}

		/* fallthrough */

	    default:
		/* we already sent a KILL signal ... */
		rd->step = 3;		/* incremented below */
		me->inms = 10000;	/* check again every 10 seconds */
		break;
        }

	++rd->step;

	/* reschedule with updated params */
	tesc_timedev (me);

    } else {
	(void) free (rd);
	(void) free (me);
    }

    return;
}


/*
 *	cmdi_inst_reaper()
 *
 *	install a timed event, which will 'wait()' the
 *	specified pid and, use signals if it refuses to die.
 *
 *	[ this does not actually belong here as a 'cmdi_' func ]
 */
void cmdi_inst_reaper (pid_t pid)
{
timedev_t *te;
struct rdat *rd;

    te = sec_malloc (sizeof *te);		/* may exit */
    rd = sec_malloc (sizeof *rd);		/* may exit */

    te->inms = 1000;
    te->func = reaper;
    te->data = rd;
    rd->pid  = pid;
    rd->step = 1;

    tesc_timedev (te);

    return;
}


/*
 *	cmdi_close()		[private]
 *
 *	open command, args:
 *	1: channel id
 *
 *	returns: 0 ok, -1 error
 */
static int cmdi_close (int ac, char *av[])
{
chn_t *ch;

    if (ac < 2) {
	mlpx_printf (CHN_MSG, MF_ERR,
				"missing channel argument for %s\n", av[0]);
	return -1;
    } else if (ac > 2)
	mlpx_printf (CHN_MSG, 0, "extra args for command %s ignored\n", av[0]);

    /* (try to) get channel to close */
    if (! (ch = arg2chn (av[1])))
	return -1;

    if (! (ch->flags & (CHN_F_ACT | CHN_F_IP))) {
	/* channel is not open */
	mlpx_printf (CHN_MSG, MF_ERR, "channel %02X is not open\n", ch->id);
	return -1;
    }

    if (ch->id == CHN_CMD || ch->id == CHN_MSG) {
	mlpx_printf (CHN_MSG, MF_ERR, "cannot close channel %02X\n", ch->id);
	return -1;
    }

    /***
     *** FIXME: we should check if the output queue is empty
     *** and wait for it to drain (unless forced - 'close -f', or
     *** perhaps issue a second close command to force it)
     ***/

    /* close/free */
    (void) close (ch->fd);

    if (ch->flags & CHN_F_PROC)
	cmdi_inst_reaper (ch->pid);

    if (ch->flags & CHN_F_ACT)
	mlpx_cleanup_ch (ch);

    return 0;	/* ok */
}


/*
 *	cmdi_quit()		[private]
 *
 *	open command, args:
 *	-none-
 *
 *	returns: (never returns, terminates the program)
 */
static int cmdi_quit (int ac, char *av[])
{
    if (ac > 1)
	tesc_emerg (CHN_MSG, 0, "extra args for command %s ignored\n", av[0]);

    /***
     *** FIXME: perform cmd close for any open channels
     *** (including waiting for output queue)?
     ***/

    tesc_emerg (CHN_MSG, 0, "command quit\n");
    tesc_emerg (CHN_MSG, MF_EOF, "\n");
    tesc_emerg (CHN_CMD, MF_EOF, "\n");
    exit (EXIT_SUCCESS);

    return 0;	/* NOT REACHED */
}


#define TOKSEP " \t"		/* command input token seperator */

/* return value for build_av() */
struct avret {
	int	ac;		/* total number of tokens */
	char	**av;		/* token array */
};

/*
 *	build_av()		[private]
 *
 *	recursively collect tokens - on the way down
 *	allocate 'av' array - at the bottom
 *	fill in the tokens (back to front) - on the way up
 *
 *	uses static buffer
 */
struct avret build_av (size_t n)
{
char *tok;
struct avret r;

    if ((tok = strtok (0, TOKSEP))) {
	/* we got one - proceed further down */
	r = build_av (n + 1);
	r.av[n] = tok;
    } else {
	/* no more tokens, so 'n' is the total # of tokens */
	r.ac = n;
	r.av = sec_malloc (n * sizeof(char*));		/* may exit */
    }

    return r;
}


/*
 *	cmdi_handle_oip ()	[private]
 *
 *	get/check result from nonblocking open()
 *
 *	returns: 0 ok, -1 fail
 */
static int cmdi_handle_oip (orn_t orn)
{
char buf[8];

    /* get any open error by performing dummy read/write */
    if (orn.revents & POLLIN) {

	if (read (orn.ch->fd, buf, 0) == -1) {
	    mlpx_printf (CHN_CMD, MF_ERR, "open %02X: open(): %s\n",
						orn.ch->id, strerror(errno));
	    return -1;
	} else
	    return 0;

    } else if (orn.revents & POLLOUT) {

	if (write (orn.ch->fd, buf, 0) == -1) {
	    mlpx_printf (CHN_CMD, MF_ERR, "open %02X: open(): %s\n",
						orn.ch->id, strerror(errno));
	    return -1;
	} else
	    return 0;

    }

    /* huh, neither POLLIN nor POLLOUT set */
    mlpx_printf (CHN_MSG, MF_ERR, "cmdi_handle_oip(): unexpected poll() "
						"state: 0x%x\n", orn.revents);
    return -1;	/* cannot determine result -> fail */
}


/*
 *	cmdi_handle_cip ()	[private]
 *
 *	get/check result from nonblocking open()
 *
 *	returns: 0 ok, -1 fail
 */
static int cmdi_handle_cip (orn_t orn)
{
int e;
socklen_t el;

    e = 0;
    el = sizeof e;
    if (getsockopt (orn.ch->fd, SOL_SOCKET, SO_ERROR, &e, &el) == -1) {
	mlpx_printf (CHN_CMD, MF_ERR, "open %02X: getsockopt(): %s\n",
						orn.ch->id, strerror(errno));
	return -1;	/* cannot determine result -> fail */
    }

    if (e) {	/* connect failed */
	mlpx_printf (CHN_CMD, MF_ERR, "open %02X: connect(): %s\n",
						orn.ch->id, strerror(e));
	return -1;	/* connect failed */
    }

    return 0;	/* ok */
}


/*
 *	cmdi_handle_ntf ()	[private]
 *
 *	handle notification: check if open/connect
 *	was successful, finish channel setup
 */
static void cmdi_handle_ntf (orn_t orn)
{
int res;

    if (orn.ch->flags & CHN_F_OIP)		/* it was an open() call */
	res = cmdi_handle_oip (orn);

    else if (orn.ch->flags & CHN_F_CIP)		/* it was an connect() call */
	res = cmdi_handle_cip (orn);

    else {
	/* huh, unknown notification */
	mlpx_printf (CHN_MSG, MF_ERR, "cmdi_handle_ntf(): unknown/invalid "
				"notification, flags=0x%x\n", orn.ch->flags);
	res = -1;
    }

    if (res == -1) {
	/* handle open/connect failure */
	mlpx_printf (CHN_CMD, 0, "FAIL open %02X\n", orn.ch->id);

	(void) close (orn.ch->fd);
	mlpx_cleanup_ch (orn.ch);

    } else {
	/* finish channel setup */
	orn.ch->flags &= ~CHN_F_IP;
	mlpx_setup_ch (orn.ch);

	mlpx_printf (CHN_CMD, 0, "OK open %02X\n", orn.ch->id);
    }

    return;
}


/*
 *	cmdi_parse()		[private]
 *
 *	parse one line of command input,
 *	perferm the requested operation on success
 *
 *	that is, tokenize the input, use the first
 *	token as the command, the rest as arguments
 *	for the command. lookup function to call
 *	in cmd_commands
 */
static void cmdi_parse (msg_t *m)
{
char *tok;
struct avret r;
int i, v;
cmd_fun func = 0;

    /* prepare m for strtok() - replace \n with \0 */
    if (m->data[m->len - 1] != '\n') {
	mlpx_printf (CHN_MSG, MF_ERR, "cmdi_parse(): malformed buffer"
						" - end of line missing\n");
	mlpx_printf (CHN_CMD, 0, "FAIL\n");
	return;
    }
    m->data[m->len - 1] = 0;

    /* ignore empty lines */
    if (!m->data[0])
	return;

    /* tokenize the message */
    tok = strtok (m->data, TOKSEP);
    r = build_av (1);
    r.av[0] = tok;

    /* (try to) find function for command name */
    for (i = 0; cmd_commands[i].name; ++i)
	if (! strcmp (cmd_commands[i].name, r.av[0])) {
	    func = cmd_commands[i].func;
	    break;
	}

    if (func) {
	/* command name ok - call the command function */
	
	switch ((v = func (r.ac, r.av))) {
	    case -2:
		if (r.ac > 1)
		    mlpx_printf (CHN_CMD, 0, "WAIT %s %s\n", r.av[0], r.av[1]);
		else
		    mlpx_printf (CHN_CMD, 0, "WAIT %s\n", r.av[0]);
		break;
	    case -1:
		if (r.ac > 1)
		    mlpx_printf (CHN_CMD, 0, "FAIL %s %s\n", r.av[0], r.av[1]);
		else
		    mlpx_printf (CHN_CMD, 0, "FAIL %s\n", r.av[0]);
		break;
	    case 0:
		if (r.ac > 1)
		    mlpx_printf (CHN_CMD, 0, "OK %s %s\n", r.av[0], r.av[1]);
		else
		    mlpx_printf (CHN_CMD, 0, "OK %s\n", r.av[0]);
		break;
	    default:
		mlpx_printf (CHN_MSG, MF_ERR, "cmdi_cmd(): "
				"invalid command return value %d\n", v);
		break;
	}

    } else {
	/* unknown command / syntax error */
	mlpx_printf (CHN_CMD, MF_ERR, "unknown command: %s\n", r.av[0]);
	mlpx_printf (CHN_CMD, 0, "FAIL %s\n", r.av[0]);
    }

    return;
}


/*
 *	cmdi_cmd()
 *
 *	parse input line for command interface
 *	and call the actual command functions
 *
 *	handle notifications for results from
 *	nonblocking open/connect calls
 */
void cmdi_cmd()
{
void *tmp;


    /*
     *	process notifications for nonblocking open/connect results
     */

    while (orn_list) {
	/* handle notification */
	cmdi_handle_ntf (orn_list->orn);

	/* next one (if any), free this notification */
	tmp = orn_list;
	orn_list = orn_list->next;
	free (tmp);
    }


    /*
     *	process input (command) queue
     */

    while (cmd_input) {

	/* parse (and execute) one line */
	cmdi_parse (cmd_input);

	/* next one (if any), free this message */
	tmp = cmd_input;
	cmd_input = cmd_input->next;
	free (tmp);
    }

    return;
}



/*** end ***/
