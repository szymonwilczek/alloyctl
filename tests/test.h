/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Minimal unit-test harness.
 *
 * Each test case registers itself into the alloy_tests ELF section with ALLOY_TEST();
 * Runner in tests/main.c walks the whole section.
 *
 * New test file is therefore picked up by the Makefile wildcard alone - main.c never
 * lists the cases, exactly like the driver registry does for drivers/
 */
#ifndef ALLOY_TESTS_TEST_H
#define ALLOY_TESTS_TEST_H

#include <stdio.h>

/* running failure count, defined in tests/main.c */
extern int alloy_test_failures;

struct alloy_test {
	const char *name;
	void (*fn)(void);
};

/*
 * Define and register test case in one go:
 *
 *	ALLOY_TEST(test_something)
 *	{
 *		ASSERT_EQ(1 + 1, 2);
 *	}
 */
#define ALLOY_TEST(fn)                                                       \
	static void fn(void);                                                \
	static const struct alloy_test __alloy_test_##fn                     \
		__attribute__((used, section("alloy_tests"))) = { #fn, fn }; \
	static void fn(void)

#define ASSERT_TRUE(cond)                                                      \
	do {                                                                   \
		if (!(cond)) {                                                 \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
			alloy_test_failures++;                                 \
		}                                                              \
	} while (0)

#define ASSERT_EQ(a, b)                                               \
	do {                                                          \
		long __a = (long)(a);                                 \
		long __b = (long)(b);                                 \
		if (__a != __b) {                                     \
			printf("FAIL %s:%d: %s == %s (%ld != %ld)\n", \
			       __FILE__, __LINE__, #a, #b, __a, __b); \
			alloy_test_failures++;                        \
		}                                                     \
	} while (0)

#endif /* ALLOY_TESTS_TEST_H */
