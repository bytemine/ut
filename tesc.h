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

#ifndef TESC_H
#define TESC_H
/*
	##   Author: Holger Rasch <rasch@bytemine.net>   ##
	##   http://www.bytemine.net                     ##
 */

/*
 *	ut: tesc.h			-- HR09
 *
 *	t/e scheduler interface
 */

/* need "data.h", "mlpx.h" */


/*
 *	interface for tesc_timedev()
 */
typedef struct timedev_ timedev_t;
typedef void (*tevfun_t) (timedev_t *);
struct timedev_ {
	unsigned	inms;		/* do it after 'inms' ms	*/
	tevfun_t	func;		/* function to execute		*/
	void		*data;		/* client data (for func)	*/
};


/* direction flags for logging */
#define LOG_DIR_IN      0x01
#define LOG_DIR_OUT     0x02


extern void tesc_emerg (int, int, const char *, ...);
extern int tesc_add_reader (chn_t *, bfifun_t, buf_t*);
extern int tesc_del_reader (const chn_t *);
extern void tesc_keep (const chn_t *);
extern int tesc_enq_wq (chn_t *, msg_t*);
extern int tesc_del_wq (const chn_t *);
extern int tesc_timedev (timedev_t *);
extern void tesc_log (msg_t *, chn_t *, int);
extern void tesc_main ();
extern void tesc_init (const struct config *cf);


#endif /* ! TESC_H */
