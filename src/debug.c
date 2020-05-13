/**
 * p-boot - pico sized bootloader
 *
 * Copyright (C) 2020  Ondřej Jirman <megi@xff.cz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#if defined(SERIAL_CONSOLE) || defined(PBOOT_FDT_LOG)

#include <common.h>
#include <asm/io.h>
#include <asm/arch/gpio.h>
#include <asm/arch/clock.h>
#include "debug.h"

#define UART0_BASE	0x01c28000

#define UART0_RBR	(UART0_BASE + 0x0)    /* receive buffer register */
#define UART0_THR	(UART0_BASE + 0x0)    /* transmit holding register */
#define UART0_DLL	(UART0_BASE + 0x0)    /* divisor latch low register */
#define UART0_DLH	(UART0_BASE + 0x4)    /* divisor latch high register */
#define UART0_IER	(UART0_BASE + 0x4)    /* interrupt enable reigster */
#define UART0_IIR	(UART0_BASE + 0x8)    /* interrupt identity register */
#define UART0_FCR	(UART0_BASE + 0x8)    /* fifo control register */
#define UART0_LCR	(UART0_BASE + 0xc)    /* line control register */
#define UART0_LSR	(UART0_BASE + 0x14)   /* line status register */

#define BAUD_115200	(0xd) /* 24 * 1000 * 1000 / 16 / 115200 = 13 */
#define NO_PARITY	(0)
#define ONE_STOP_BIT	(0)
#define DAT_LEN_8_BITS	(3)
#define LC_8_N_1	(NO_PARITY << 3 | ONE_STOP_BIT << 2 | DAT_LEN_8_BITS)
#define TX_READY	(readl(UART0_LSR) & BIT(6))

#ifdef SERIAL_CONSOLE
void console_init(void)
{
	clock_init_uart();

	/* uart0 pinctrl */
	sunxi_gpio_set_cfgpin(SUNXI_GPB(8), SUN50I_GPB_UART0);
	sunxi_gpio_set_cfgpin(SUNXI_GPB(9), SUN50I_GPB_UART0);
	sunxi_gpio_set_pull(SUNXI_GPB(9), SUNXI_GPIO_PULL_UP);

	/* select dll dlh */
	writel(0x80, UART0_LCR);
	/* set baudrate */
	writel(0, UART0_DLH);
	writel(BAUD_115200, UART0_DLL);
	/* set line control */
	writel(LC_8_N_1, UART0_LCR);
}
#endif

__attribute__((section(".textlow")))
void putc(char c)
{
#ifdef SERIAL_CONSOLE
	while (!TX_READY);

	writel(c, UART0_THR);
#endif
#ifdef PBOOT_FDT_LOG
	append_log(c);
#endif
}

__attribute__((section(".textlow")))
void puts(const char* s)
{
	while (*s) {
		if (*s == '\n')
			putc('\r');
		putc(*s++);
	}
}

#define BUF_LEN 30
__attribute__((section(".textlow")))
static void put_uint(unsigned long long value, int align, int pad0)
{
	char buf[BUF_LEN];
	char *p = &buf[BUF_LEN - 1];

	*p = '\0';

	if (!value)
		*--p = '0';

	while (value) {
		*--p = '0' + value % 10;
		value /= 10;
	}

	int len = &buf[BUF_LEN - 1] - p;
	for (int i = 0; i < align - len; i++)
		putc(pad0 ? '0' : ' ');

	puts(p);
}

__attribute__((section(".textlow")))
static void put_int(long long v, int align, int pad0)
{
	if (v < 0) {
		putc('-');
		v = -v;
		align--;
	}

	put_uint(v, align, 0);
}

__attribute__((section(".textlow")))
static void put_uint_div_by_1000(unsigned int value)
{
	if (value >= 1000)
		put_uint(value / 1000, 0, 0);
	else
		putc('0');

	putc('.');
	put_uint(value % 1000, 0, 0);
}

__attribute__((section(".lowdata")))
static char hexstr[] = "0123456789abcdef";

__attribute__((section(".textlow")))
void put_hex(unsigned long long hex, int align, int pad0)
{
	int i;
	int skip_nibbles = __builtin_clzll(hex) / 4;

	for (i = 0; i < align - (16 - skip_nibbles); i++)
		putc(pad0 ? '0' : ' ');

	for (i = skip_nibbles; i < 16; i++) {
		unsigned nibble = (hex >> (4 * (15 - i))) & 0xf;

		putc(hexstr[nibble]);
	}
}

__attribute__((section(".textlow")))
void printf(const char* fmt, ...)
{
	va_list ap;
	const char* p = fmt;

	va_start(ap, fmt);

	while (1) {
		if (*p == '%') {
			int l = 0;
			int pad0 = 0;
			int align = 0;

			p++;

			while (*p == '0') {
				pad0 = 1;
				p++;
			}

			// skip alignement
			while (*p >= '0' && *p <= '9') {
				align = align * 10;
				align += *p - '0';
				p++;
			}

			while (*p == 'l')
				l++, p++;
			while (*p == 'h') // ignore
				p++;

			if (l > 2)
				goto end;

			switch (*p) {
				case 'c':
					putc(va_arg(ap, int));
					break;
				case 's':
					puts(va_arg(ap, char*));
					break;
				case 'u':
				case 'x': {
					unsigned long long val = 0;

					if (l == 0)
						val = va_arg(ap, unsigned int);
					else if (l == 1)
						val = va_arg(ap, unsigned long);
					else if (l == 2)
						val = va_arg(ap, unsigned long long);

					if (*p == 'u')
						put_uint(val, align, pad0);
					else
						put_hex(val, align, pad0);
					break;
				}
				case 'd': {
					long long val = 0;

					if (l == 0)
						val = va_arg(ap, int);
					else if (l == 1)
						val = va_arg(ap, long);
					else if (l == 2)
						val = va_arg(ap, long long);

					put_int(val, align, pad0);
					break;
				}
				case 'f':
					put_uint_div_by_1000(va_arg(ap, uint32_t));
					break;
				case 'p':
					put_hex(va_arg(ap, unsigned long long), 8, 1);
					break;
				case '%':
					putc(*p);
					break;
				case '\0':
					goto end;
				default:
					putc('%');
					putc(*p);
			}
		} else if (*p == '\n') {
			putc('\r');
			putc(*p);
		} else if (*p == '\0') {
			goto end;
		} else {
			putc(*p);
		}

		p++;
	}

end:
	va_end(ap);
}

#endif
