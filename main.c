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
 *	ut: main.c			-- HR09
 *
 *	[ channel based service multiplexer ]
 *
 *	usage:
 *	ut [ -c configfile ]
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef __linux__
# include <sys/file.h>
# include <getopt.h>
#endif

#include <errno.h>
#include <string.h>

#include "conf.h"
#include "mlpx.h"
#include "data.h"
#include "tesc.h"


#ifndef UT_KASTRING
#define UT_KASTRING	"-keepalive-"
#endif


/*
 *	keepalive()
 *	[private, used for tesc_timedev()]
 *
 *	print keepalive message and reschedule this event
 */
static void keepalive (timedev_t *me)
{
    /* print */
    mlpx_printf (CHN_MSG, 0, "%s\n", UT_KASTRING);

    /* reschedule, 'inms' is untouched */
    tesc_timedev (me);

    return;
}


/*
 *	acquire_lock()
 *
 *	provide global locking for ut. We use an advisory
 *	exclusive lock (via flock) on our config.
 *	Using this type of lock has the advantage that
 *	we do not have to explicitly unlock before exiting -
 *	the lock vanished with this process. Furthermore,
 *	since ut refuses to run without a config, we do not
 *	have to create an extra file for locking.
 *
 *	returns:
 *	  1	succes
 *	  0	already locked
 *	 -1	some error occurred
 */
int acquire_lock (const char *path, int *fdptr)
{
    if ((*fdptr = open (path, O_RDONLY)) == -1) {
	tesc_emerg (CHN_MSG, MF_ERR, "%s: %s\n", path, strerror (errno));
	return -1;	/* error */
    }

    if (flock (*fdptr, LOCK_EX | LOCK_NB) == -1) {
	if (errno == EWOULDBLOCK)
	    return 0;	/* already locked */
	else {
	    tesc_emerg (CHN_MSG, MF_ERR, "%s: flock(): %s\n",
						path, strerror (errno));
	    return -1;	/* error */
	}
    }

    return 1;		/* success */
}


/*
 *	main()
 *
 *	general setup before the we enter the scheduler
 */
int main (int ac, char *av[])
{
int i;
const char *cfpath;
const struct config *cf;
timedev_t ka;
int cffd;	/* must stay open until ut exits */


    /*
     *	process args
     */

    cfpath = UT_CONFIG_PATH;		/* conf.h or Makefile override */

    opterr = 0;		/* no extra error msgs from getopt() */
    while ((i = getopt (ac, av, "c:")) != -1) {
	switch (i) {
	    case 'c' :			/* -c configfile */
		cfpath = optarg;
		break;
	    case '?' :
		/* optopt contains opt */
		tesc_emerg (CHN_MSG, MF_ERR, "unknown option '%c'\n", optopt);
		break;
	    default :
		tesc_emerg (CHN_MSG, MF_ERR, "getopt() returned %d\n", i);
		break;
	}
    }


    /*
     *	initialize 
     */

    /* check if we are the only instance */
    switch (acquire_lock (cfpath, &cffd)) {
	case 0 :
	    tesc_emerg (CHN_MSG, MF_ERR,
		"another instance of ut is already running, exiting\n");
	    tesc_emerg (CHN_MSG, MF_EOF, "\n");
	    exit (EXIT_FAILURE);
	case 1 :
	    break;
	default :
	    tesc_emerg (CHN_MSG, MF_ERR, "failed to acquire lock, exiting\n"); 
	    tesc_emerg (CHN_MSG, MF_EOF, "\n");
	    exit (EXIT_FAILURE);
    }

    /* read config file */
    cf = conf_init (cffd);

    /* setup scheduler */
    tesc_init (cf);

    /* initialize mux/demux */
    mlpx_init (cf);

    /* activate keepalive */
    ka.inms = cf->keepalive * 1000;
    ka.func = keepalive;
    ka.data = 0; /* not used */
    tesc_timedev (&ka);

    /* want errno set instead of signal */
    if (signal (SIGPIPE, SIG_IGN) == SIG_ERR)
	tesc_emerg (CHN_MSG, MF_ERR, "signal(): %s\n", strerror(errno));


    /*
     *	enter the scheduler
     */

    tesc_main();


    return EXIT_SUCCESS;
}


/*** end ***/
