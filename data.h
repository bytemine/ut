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

#ifndef DATA_H
#define DATA_H
/*
	##   Author: Holger Rasch <rasch@bytemine.net>   ##
	##   http://www.bytemine.net                     ##
 */

/*
 *	ut: data.h			-- HR09
 *
 *	buffer related stuff
 */

/* needs <sys/time.h> */


#define SBISIZ		0x2000			/* sub buffer size	*/
			/* actually less (- sizeof(struct sbuf))	*/
#define	SBIMIN		80			/* min read size	*/
#define	PRFXLEN		5			/* prefix length	*/

/* flags used in msg_t */
#define	MF_PLAIN	0x01			/* ignore prefix	*/
#define	MF_NONL		0x02			/* incomplete line	*/
#define MF_ERR		0x04			/* is error msg		*/
#define MF_EOF		0x08			/* close down message	*/


typedef struct buf_ buf_t;
typedef struct msg_ msg_t;

/* output function type (for mux/demux) */
typedef	void (*bfofun_t) (msg_t*, const chn_t*);

/* input function type (from fd) */
typedef	int (*bfifun_t) (int, buf_t*, chn_t*);


/*
 *	input buffer
 *	used for any input from file descriptors
 *	
 *	if set to wait, output function will only be called if
 *	a line is complete.
 */
struct buf_ {
	bfofun_t	out;			/* output function	*/
	int		wait;			/* no incomplete lines	*/
	int		plain;			/* non-prefixed input	*/
	struct sbuf	*sbh;			/* first internal buf	*/
	struct sbuf	*cur;			/* current buffer	*/
};

/* internal (sub) buffer for buf_t */
struct sbuf {
	struct sbuf	*next;			/* tail of list		*/
	char		*fdata;			/* start of data	*/
	char		*ffree;			/* start of free space	*/
	int		flen;			/* length of free space	*/
	char		data[];			/* actual buffer	*/
};


/*
 *	buffer structure for internal and outbound
 *	data - complete with linked list header and
 *	prefix support
 */
struct msg_ {
	msg_t		*next;			/* next msg (in queue)	*/
	int		flags;			/* PLAIN, NONL, 0	*/
	int		len;			/* data len (including	*/
						/*    prefix if !PLAIN) */
	char		prefix[PRFXLEN];	/* prefix and data MUST	*/
	char		data[];			/*    be continuous!	*/
};


extern int data_buf_input (int, buf_t*, chn_t*);
extern buf_t *data_new_buf (bfofun_t, int, int);
extern void data_del_buf (buf_t *b);


#endif /* ! DATA_H */
