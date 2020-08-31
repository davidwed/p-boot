/*
 * Copyright © 2005-2019 Rich Felker, et al.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <common.h>

__attribute__((externally_visible))
void *memcpy(void *dest, const void *src, size_t n)
{
	const uint8_t *s = src;
	uint8_t *d = dest;

	for (size_t i = 0; i < n; i++)
		d[i] = s[i];

	return dest;
}

__attribute__((externally_visible))
void *memset(void *dest, int c, size_t n)
{
	unsigned char *s = dest;

	for (; n; n--, s++) *s = c;

	return dest;
}

int memcmp(const void *vl, const void *vr, size_t n)
{
        const unsigned char *l=vl, *r=vr;
        for (; n && *l == *r; n--, l++, r++);
        return n ? *l-*r : 0;
}


void *memmove(void *dest, const void *src, size_t n)
{
        char *d = dest;
        const char *s = src;

        if (d==s) return d;
        if ((uintptr_t)s-(uintptr_t)d-n <= -2*n) return memcpy(d, s, n);

        if (d<s) {
                for (; n; n--) *d++ = *s++;
        } else {
                while (n) n--, d[n] = s[n];
        }

        return dest;
}

size_t strlen(const char *s)
{
        const char *a = s;
        for (; *s; s++);
        return s-a;
}

void *memchr(const void *src, int c, size_t n)
{
        const unsigned char *s = src;
        c = (unsigned char)c;
        for (; n && *s != c; s++, n--);
        return n ? (void *)s : 0;
}

int strcmp(const char *l, const char *r)
{
        for (; *l==*r && *l; l++, r++);
        return *(unsigned char *)l - *(unsigned char *)r;
}
