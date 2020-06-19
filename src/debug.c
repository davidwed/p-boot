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

#include <common.h>
#include <asm/io.h>
#include <asm/arch/gpio.h>
#include "debug.h"
#include "ccu.h"
#include "vidconsole.h"

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

void console_init(void)
{
	/* open the clock for uart */
	setbits_le32(CCU_BUS_CLK_GATE3, 1u << 16);
	/* deassert uart reset */
	setbits_le32(CCU_BUS_SOFT_RST4, 1u << 16);

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

#ifdef VIDEO_CONSOLE
struct vidconsole* sys_console;
#endif

#if defined(SERIAL_CONSOLE) || defined(PBOOT_FDT_LOG) || defined(VIDEO_CONSOLE)

void real_putc(char c)
{
#ifdef SERIAL_CONSOLE
	while (!TX_READY);

	writel(c, UART0_THR);
#endif
#ifdef PBOOT_FDT_LOG
	append_log(c);
#endif
#ifdef VIDEO_CONSOLE
	vidconsole_putc(sys_console, c);
#endif
}

void real_puts(const char* s)
{
	while (*s) {
		if (*s == '\n')
			real_putc('\r');
		real_putc(*s++);
	}
}

#define BUF_LEN 30
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
		real_putc(pad0 ? '0' : ' ');

	real_puts(p);
}

static void put_int(long long v, int align, int pad0)
{
	if (v < 0) {
		real_putc('-');
		v = -v;
		align--;
	}

	put_uint(v, align, 0);
}

static void put_uint_div_by_1000(unsigned int value)
{
	if (value >= 1000)
		put_uint(value / 1000, 0, 0);
	else
		real_putc('0');

	real_putc('.');
	put_uint(value % 1000, 0, 0);
}

void real_put_hex(unsigned long long hex, int align, int pad0)
{
	int i;
	int skip_nibbles = hex > 0 ? __builtin_clzll(hex) / 4 : 15;

	for (i = 0; i < align - (16 - skip_nibbles); i++)
		real_putc(pad0 ? '0' : ' ');

	for (i = skip_nibbles; i < 16; i++) {
		unsigned nibble = (hex >> (4 * (15 - i))) & 0xf;

		if (nibble < 10)
			real_putc('0' + nibble);
		else
			real_putc('a' + (nibble - 10));
	}
}

void real_printf(const char* fmt, ...)
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
					real_putc(va_arg(ap, int));
					break;
				case 's':
					real_puts(va_arg(ap, char*));
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
						real_put_hex(val, align, pad0);
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
					real_put_hex(va_arg(ap, unsigned long long), 8, 1);
					break;
				case '%':
					real_putc(*p);
					break;
				case '\0':
					goto end;
				default:
					real_putc('%');
					real_putc(*p);
			}
		} else if (*p == '\n') {
			real_putc('\r');
			real_putc(*p);
		} else if (*p == '\0') {
			goto end;
		} else {
			real_putc(*p);
		}

		p++;
	}

end:
	va_end(ap);
}

#endif

void dump_regs(uint32_t start, uint32_t len, const char* name)
{
	unsigned off = 0;

        printf("\n# %s (0x%08x - 0x%08x):\n\n", name, start, start + len - 4);

        for (; start < start + len; start += 4, off += 4) {
		uint32_t val = readl((unsigned long)start);
		if (val) {
			//printf("%08x : %08x # +%04x\n", start, val, off);
			printf("0x%08x : %08x\n", start, val);
		}
	}
}

#define PIO(n, a) readl(0x01C20800ul + (n) * 0x24 + a)
#define RPIO(a) readl(0x01f02c00ul + a)

void dump_pio(void)
{
        printf("\n# PIO:\n\n");

        for (unsigned i = 1; i <= 7; i++) {
                printf("P%c_CFG0 = %08x\n", 'A' + i, PIO(i, 0x00));
                printf("P%c_CFG1 = %08x\n", 'A' + i, PIO(i, 0x04));
                printf("P%c_CFG2 = %08x\n", 'A' + i, PIO(i, 0x08));
                printf("P%c_CFG3 = %08x\n", 'A' + i, PIO(i, 0x0c));
                printf("P%c_DAT  = %08x\n", 'A' + i, PIO(i, 0x10));
                printf("P%c_DRV0 = %08x\n", 'A' + i, PIO(i, 0x14));
                printf("P%c_DRV1 = %08x\n", 'A' + i, PIO(i, 0x18));
                printf("P%c_PUL0 = %08x\n", 'A' + i, PIO(i, 0x1c));
                printf("P%c_PUL1 = %08x\n", 'A' + i, PIO(i, 0x20));
        }

        printf("PL_CFG0 = %08x\n", RPIO(0x00));
        printf("PL_CFG1 = %08x\n", RPIO(0x04));
        printf("PL_CFG2 = %08x\n", RPIO(0x08));
        printf("PL_CFG3 = %08x\n", RPIO(0x0c));
        printf("PL_DAT  = %08x\n", RPIO(0x10));
        printf("PL_DRV0 = %08x\n", RPIO(0x14));
        printf("PL_DRV1 = %08x\n", RPIO(0x18));
        printf("PL_PUL0 = %08x\n", RPIO(0x1c));
        printf("PL_PUL1 = %08x\n", RPIO(0x20));
}

void dump_de2_registers(void)
{
	dump_regs(0x01000000, 0x14, "de2");
        dump_regs(0x01100000 + 0x0000, 0x10, "mixer0 GLB");
        dump_regs(0x01100000 + 0x1000, 0x100, "mixer0 BLD");
        dump_regs(0x01100000 + 0x2000, 0x100, "mixer0 VI");
        dump_regs(0x01100000 + 0x3000, 0x8c, "mixer0 UI1");
        dump_regs(0x01100000 + 0x4000, 0x8c, "mixer0 UI2");
        dump_regs(0x01100000 + 0x5000, 0x8c, "mixer0 UI3");
}

void dump_dsi_registers(void)
{
	dump_regs(0x01c0c000, 0x200, "tcon");
	dump_regs(0x01ca1000, 0x100, "dphy");
	dump_regs(0x01ca0000, 0x1000, "dsi");
}

void dump_ccu_registers(void)
{
	dump_regs(0x01c20000, 0x1000, "ccu");
	//dump_regs(0x01c0f000, 0x200, "mmc0");
	//dump_regs(0x01c10000, 0x200, "mmc1");
	//dump_regs(0x01c11000, 0x200, "mmc2");
	//dump_regs(0x01c21400, 0x100, "pwm");
	//dump_regs(0x01c21400, 0x100, "r_pwm");

}
