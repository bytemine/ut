# ut

multiplex uni-/bidirectional streams onto stdin/stdout

## Description

The ut utility performs multiplexing of upto 254 uni- (in or out) or
bidirectional channels onto its stdin/stdout pair of file descriptors.

The multiplexing is done by using special prefixes on the main
(stdin/stdout) channel, in order to identify the client channels, the
command channel and the debug/message channel.  Input on the main channel
is strictly line based, i.e., content extends from the end of the prefix
to, and including, the end-of-line (\n). For the output on the main channel,
this is mostly the same, except that a special prefix type exists,
which signals that the end-of-line has to be discarded.

ut is not meant to be used as a standalone tool, but mainly for use by
other (ut aware) programs, which too perform the prefix based mux/demux
scheme.

## Author

Holger Rasch <rasch@bytemine.net>

## License

Copyright (c) 2009, 2010 bytemine GmbH <info@bytemine.net>

Permission to use, copy, modify, and distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
