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
 *	ut: util.c			-- HR09
 *
 *	misc. utility functions
 */


/*
 *	ishexdigit()
 *
 *	check if char is 0-9 or A-F
 */
int ishexdigit (char c)
{
    /* expects ASCII */
    if (c < '0' || c > 'F')
	return 0;
    if (c > '9' && c < 'A')
	return 0;
    return 1;
}


/*
 *	hexd2int()
 *
 *	convert hexdigit (char) to int
 */
int hexd2int (char c)
{
int r;

    if (c <= '9')
	r = c - '0';
    else
	r = c - 'A' + 10;

    return r;
}


/*
 *	hexdigit()
 *
 *	(int) 0-15 to hex digit
 */
char hexdigit (int d)
{
char c;

    /* probably fails for non ASCII encoding */
    if (d < 10)
	c = '0' + d;
    else
	c = 'A' + (d - 10);

    return c;
}


/*** end ***/
