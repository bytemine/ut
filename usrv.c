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
 *	usrv -- setup unix domain socket (server/listen)
 *	usage: usrv <socketpath>
 *
 *	for testing ut via method 'unix'
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#define ERRXIT(a)	{ perror(a); exit (EXIT_FAILURE); }

int main (int ac, char **av)
{
int fd, m, p;
socklen_t len;
int l;
struct sockaddr_un a;
char buf[1024];

    if (ac != 2) {
	fprintf (stderr, "usage: %s <socketpath>\n", av[0]);
	return EXIT_FAILURE;
    }
    if ((len = strlen (av[1])) > sizeof(a.sun_path)) {
	fprintf (stderr, "error: path too long (>%lu)\n", sizeof(a.sun_path));
	return EXIT_FAILURE;
    }

#ifndef __linux__
    a.sun_len = len;
#endif

    a.sun_family = AF_UNIX;
    strncpy (a.sun_path, av[1], sizeof(a.sun_path));
    if ((m = socket (AF_LOCAL, SOCK_STREAM, 0)) == -1)
	ERRXIT("socket()")
    if (bind (m, (struct sockaddr*)&a, sizeof a) == -1)
	ERRXIT("bind()")
    if (listen (m, 1) == -1)
	ERRXIT("listen()")
    if ((fd = accept (m, (struct sockaddr*)&a, &len)) == -1)
	ERRXIT("accept()")
    if ((p = fork()) == -1)
	ERRXIT("fork()")
    while (1) 
        switch (l = read (p ? 0 : fd, buf, sizeof buf)) {
	    case -1:
		ERRXIT("read()")
	    case 0:
		fprintf (stderr, "\n[EOF].\n");
		kill (p ? p : getppid(), SIGTERM);
		(void) unlink (av[1]);
		_exit (EXIT_SUCCESS);
	    default:
		if ((l = write (p ? fd : 1, buf, l)) != l) {
		    if (l == -1) {
			ERRXIT ("write()")
		    } else {
			fprintf (stderr, "write() failed.\n");
			kill (p ? p : getppid(), SIGTERM);
			(void) unlink (av[1]);
			exit (EXIT_FAILURE);
		    }
		}
		break;
	}
    return EXIT_SUCCESS;
}


/*** end ***/
