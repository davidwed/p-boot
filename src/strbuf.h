#pragma once

struct strbuf {
	char* start;
	char* end;
	char* cur;
};

/* supported formats %s %u %% %c */
struct strbuf* strbuf_new(unsigned buf_size);
void strbuf_putc(struct strbuf* b, char ch);
void strbuf_uint(struct strbuf* b, unsigned value);
void strbuf_printf(struct strbuf* b, const char* fmt, ...);

static inline char* strbuf_to_cstr(struct strbuf* b)
{
	return b->start;
}
