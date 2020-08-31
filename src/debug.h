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

#pragma once

#if defined(SERIAL_CONSOLE) || defined(PBOOT_FDT_LOG) || defined(VIDEO_CONSOLE)

void putc(char c);
void puts(const char* s);
void printf(const char* fmt, ...);
void put_hex(unsigned long long value, int align, int pad0);

#else

static inline void putc(char c) { }
static inline void puts(const char* s) { }
static inline void printf(const char* fmt, ...) { }
static inline void put_hex(unsigned long long value, int align, int pad0) { }

#endif

#ifdef PBOOT_FDT_LOG
void append_log(char c);
#endif
#ifdef VIDEO_CONSOLE
extern struct vidconsole* sys_console;
#endif

void console_init(void);

#ifdef DEBUG

#define debug(a...) printf(a)
#define pr_debug(a...) printf(a)
#define pr_info(a...) printf(a)
#define pr_warn(a...) printf(a)
#define pr_err(a...) printf(a)

#else

#define debug(a...)
#define pr_debug(a...)
#define pr_info(a...)
#define pr_warn(a...)
#define pr_err(a...)

#endif

void panic_shutdown(uint32_t code);
#define panic(code, a...) { printf(a); panic_shutdown(code); }
#define hang() \
        do { \
                __asm__ __volatile__("wfi"); \
	} while (1)

void dump_regs(uint32_t start, uint32_t len, const char* name);
void dump_pio(void);
void dump_ccu_registers(void);
void dump_de2_registers(void);
void dump_dsi_registers(void);
