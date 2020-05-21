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

int rsb_init(void);

int pmic_write(uint8_t reg, uint8_t val);
int pmic_read(uint8_t reg_addr);
int pmic_clrsetbits(uint8_t reg, uint8_t clr_mask, uint8_t set_mask);

#define pmic_clrbits(reg, clr_mask) pmic_clrsetbits(reg, clr_mask, 0)
#define pmic_setbits(reg, set_mask) pmic_clrsetbits(reg, 0, set_mask)

void pmic_poweroff(void);
void pmic_reboot(void);

/* access persistent data registers */
void pmic_write_data(unsigned off, uint8_t data);
int pmic_read_data(unsigned off);

/* dump all registers to log */
void pmic_dump_registers(void);
void pmic_dump_status(void);

/* initialize PMIC  */
void pmic_init(void);
