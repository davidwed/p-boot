#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <ctype.h>

static char ln[128];
static int ln_len;

static bool next_line(bool require)
{
	char* r;

next:
	r = fgets(ln, sizeof ln, stdin);
	if (r == NULL) {
		if (ferror(stdin)) {
			fprintf(stderr, "Input error\n");
			exit(1);
		}
		
		if (require) {
			fprintf(stderr, "Expecting more input lines\n");
			exit(1);
		}

		return false;
	}
	
	int len = strlen(ln);
	if (len > 0 && ln[len - 1] == '\n')
		ln[--len] = 0;
	ln_len = len;

	for (int i = 0; i < ln_len; i++)
		if (!isblank(ln[i]))
			return true;

	goto next;
}

/*
 * rle.c -- by David Henry
 * last modification: dec. 28, 2004
 *
 * this code is free.
 */
long enc_size( uint8_t *in, long sizein )
{
	int packet_size = 0;
	long sizeout = 0;
	uint8_t *pin = in;

	while( pin < in + sizein ) {
		if( (pin[0] == pin[1]) && (packet_size < (0x7f-1)) ) {
			packet_size++;
			pin++;
		}
		else {
			if( packet_size > 0 ) {
				sizeout += 2;
				pin++;
			}

			packet_size = 0;

			while( pin[0] != pin[1] && !((pin >= in + sizein)
					|| (-packet_size > (0x7f-1))) ) {
				packet_size--;
				pin++;
			}

			sizeout += (-packet_size) + 1;
			packet_size = 0;
		}
	}

	return sizeout;
}

long rle( uint8_t *in, uint8_t *out, long sizein )
{
	uint8_t *pin = in;
	uint8_t *pout = out;
	uint8_t *ptmp;
	int packet_size = 0;
	int i;

	while( pin < in + sizein )
	{
		/* look for rle packet */
		if( (pin[0] == pin[1]) && (packet_size < (0x7f-1)) )
		{
			packet_size++;
			pin++;
		}
		else
		{
			if( packet_size > 0 )
			{
				/* write rle header and packet */
				*(pout++) = (1 << 7) + ((packet_size + 1) & 0x7f);
				*(pout++) = *(pin++);
			}

			packet_size = 0;
			ptmp = pin;

			/* look for next rle packet */
			while( pin[0] != pin[1] )
			{
				/* don't overflow buffer */
				if( (pin >= in + sizein) || (-packet_size > (0x7f-1)) )
					break;

				/* skip byte... */
				packet_size--;
				pin++;
			}

			/* write non-rle header */
			*(pout++) = (-packet_size) & 0x7f;

			/* write non-rle packet */
			for( i = 0; i < (-packet_size); i++, pout++, ptmp++ )
				*pout = *ptmp;

			packet_size = 0;
		}
	}

	return (pout - out);
}

uint8_t out[1024 * 16];
size_t out_len = 0;

int main(int ac, char* av[])
{
	next_line(true);

	unsigned ch_height;
	if (sscanf(ln, "%u", &ch_height) != 1) {
		fprintf(stderr, "Missing character height\n");
		return 1;
	}

	if (ch_height < 8 || ch_height > 16) {
		fprintf(stderr, "Invalid character height\n");
		return 1;
	}
	
	unsigned ch_index = 0;
	while (next_line(false)) {
		if (ln[0] == '0' && ln[1] == 'x' && isxdigit(ln[2]) && isxdigit(ln[3]) && ln[4] == 0) {
			unsigned ch;
			if (sscanf(ln, "%x", &ch) != 1) {
				fprintf(stderr, "Missing character code\n");
				return 1;
			}
			
			if (ch_index != ch) {
				fprintf(stderr, "Character 0x%02x is out of order\n", ch);
				return 1;
			}
		}

		for (int y = 0; y < ch_height; y++) {
			next_line(true);
		
			if (ln_len != 8) {
				fprintf(stderr, "Character data line must be 8 chars wide\n");
				return 1;
			}

			uint8_t ch_line = 0;
			for (unsigned x = 0; x < 8; x++)
				 ch_line |= (ln[x] == '#') ? 1 << (7 - x) : 0;
			
			out[out_len++] = ch_line;
		}
		
		ch_index++;
	}

	if (ac == 2 && !strcmp(av[1], "rle")) {
		uint8_t out_rle[1024 * 16];
		size_t out_rle_len = enc_size(out, out_len);
		rle(out, out_rle, out_len);

		return fwrite(out_rle, 1, out_rle_len, stdout) == out_rle_len ? 0 : 1;
	}

	if (ac == 2 && !strcmp(av[1], "h")) {
		uint8_t out_rle[1024 * 16];
		size_t out_rle_len = enc_size(out, out_len);
		rle(out, out_rle, out_len);

		printf("#define FONT_WIDTH 8\n");
		printf("#define FONT_HEIGHT %u\n", ch_height);
		printf("#define FONT_CHARS %u\n", ch_index);
		printf("__attribute__((unused))\nstatic uint8_t font_data[%u] = {\n", out_rle_len);
		for (int i = 0; i < out_rle_len; i++) {
			printf("0x%02x,%s", out_rle[i], ((i % 16) == 15) ? "\n" : "");
		}
		printf("};\n");
		return 0;
	}

	return fwrite(out, 1, out_len, stdout) == out_len ? 0 : 1;
}
