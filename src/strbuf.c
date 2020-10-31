#include <stddef.h>
#include <stdlib.h>
#include <stdarg.h>
#include "strbuf.h"

struct strbuf* strbuf_new(unsigned buf_size)
{
	struct strbuf* b;
	b = malloc(sizeof(*b));
	b->start = b->cur = malloc(buf_size);
	b->end = b->start + (buf_size - 1);
	return b;
}

void strbuf_putc(struct strbuf* b, char ch)
{
	if (b->cur < b->end) {
		*b->cur++ = ch;
		*b->cur = 0;
	}
}

void strbuf_uint(struct strbuf* b, unsigned value)
{
	const unsigned BUF_LEN = 11;
	char buf[BUF_LEN];
	char *p = &buf[BUF_LEN - 1];

	*p = '\0';
	do {
		*--p = '0' + value % 10;
		value /= 10;
	} while (value);

	while (*p)
		strbuf_putc(b, *p++);
}

void strbuf_printf(struct strbuf* b, const char* fmt, ...)
{
	va_list ap;
	const char* p = fmt;

	va_start(ap, fmt);

	while (*p) {
		if (*p == '%') {
			p++;

			switch (*p) {
				case 's': {
					char* in = va_arg(ap, char*);
					while (*in)
						strbuf_putc(b, *in++);
					break;
				}
				case 'u':
					strbuf_uint(b, va_arg(ap, unsigned));
					break;
				case 'c':
					strbuf_putc(b, va_arg(ap, int));
					break;
				case '%':
				add_char:
					strbuf_putc(b, *p);
					break;
			}
		} else {
			goto add_char;
		}

		p++;
	}

	va_end(ap);
}
