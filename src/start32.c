#define SUNXI_RTC_BASE 0x01f00000

#define writel(v, a) ({ \
	*(volatile unsigned int *)(a) = (v); (v); })

#define readl(a) ({ \
	unsigned int __v = *(volatile unsigned int *)(a); \
	__v; })

__attribute__((noreturn))
extern void jump_to(unsigned int addr);

/*
 * Returning from main will run the aarch64 p-boot code.
 */
void main(void)
{
	unsigned target = 0;
	unsigned mode;

	mode = readl(SUNXI_RTC_BASE + 0x104);
	if (mode == 0xb0010fe1)
		target = 0x20;
	else if (mode == 0xb001e33c)
		target = 0x2f54;

	if (target) {
		writel(0, SUNXI_RTC_BASE + 0x104);
		jump_to(target);
	}
}
