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

#ifndef MLPX_H
#define MLPX_H
/*
	##   Author: Holger Rasch <rasch@bytemine.net>   ##
	##   http://www.bytemine.net                     ##
 */

/*
 *	ut: mlpx.h			-- HR09
 *
 *	channels, mux/demux, command-interpreter
 */

/* need conf.h */


/* prefixes (fixed):					*/
/*	"<XX< "		input to demux			*/
/*	">XX> "		output from mux			*/
/*	"_XX_ "		output from mux (no eol)	*/
/*	"!XX! "		output from mux (error)		*/
/*	".XX. "		output from mux (closed)	*/
/* XX can be anything from 00 to (hex for) CHN_MAX	*/
/* but must fit in 2 chars				*/

/* CHN_MAX must must be < 0x100, since prefix allows only 2 digits (hex) */
#define	CHN_MAX		0xff
#define CHN_MSG		CHN_MAX			/* debug/msg channel	*/
#define CHN_CMD		0x00			/* command channel	*/
#define CHN_MAIN	-1			/* fake - main in/out	*/


/* timeout function (for stall-detection) */
typedef struct chn_ chn_t;
typedef void (*tofun_t) (chn_t *);


/*
 *	(logical) channel
 *
 */
struct chn_ {
#define CHN_F_RD	0x0001			/* fd open for reading	*/
#define CHN_F_WR	0x0002			/* fd open for writing	*/
#define	CHN_F_ACT	(CHN_F_RD|CHN_F_WR)	/* channel is active	*/
#define CHN_F_OIP	0x0010			/* open in progress	*/
#define CHN_F_CIP	0x0020			/* connect in progress	*/
#define CHN_F_IP	(CHN_F_OIP|CHN_F_CIP)	/* ^^ in progress	*/
#define CHN_F_PROC	0x0040			/* channel to process	*/
#define CHN_ERR_R	0x0100			/* read error on fd	*/
#define CHN_ERR_W	0x0200			/* write error on fd	*/
#define CHN_ERR_L	0x0400			/* write error on log	*/
#define CHN_ERR_P	0x0800			/* see pxfl		*/
#define CHN_ERROR	(CHN_ERR_R | CHN_ERR_W | CHN_ERR_L | CHN_ERR_P)
#define CHN_EOF		0x1000			/* EOF on fd		*/
#define CHN_NEED_UPD	(CHN_ERROR | CHN_EOF)	/* update required	*/
	int	flags;			/* (see above)			*/
	int	id;			/* 0 upto CHN_MAX		*/
	int	fd;			/* passed to tesc/fdio		*/
	int	log;			/* logfile (if enabled)		*/
	pid_t	pid;			/* pid from method popen	*/
	int	e_rd;			/* errno from read()		*/
	int	e_wr;			/* errno from write()		*/
	int	e_log;			/* errno from log write		*/
	int	pxfl;			/* poll execpt. cond.		*/
	struct channel	*cf;		/* config			*/
	int		timeout;	/* s-d timeout, enable if > 0	*/
	tofun_t		stalled;	/* called is above is reached	*/
	/* filter hook? */
};


extern chn_t *mlpx_id2chn (int);
extern void mlpx_add_reader (chn_t *);
extern char mlpx_prfxtc (int);
extern void mlpx_cmd ();
extern void mlpx_update (chn_t *);
extern void mlpx_init (const struct config *cf);
//extern void chan_err (int, const char *, ...);
//extern void chan_eof ();
extern void mlpx_printf (int, int, const char *, ...);
extern void mlpx_print_msg (int, struct strlist *);
extern void mlpx_setup_ch (chn_t *);
extern void mlpx_cleanup_ch (chn_t *);


#endif /* ! MLPX_H */
