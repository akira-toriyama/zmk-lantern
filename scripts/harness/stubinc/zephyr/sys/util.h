
/* zephyr/sys/util_macro.h */
#define BIT(n) (1UL << (n))
#ifndef DIV_ROUND_UP
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#endif
