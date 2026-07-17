// SPDX-License-Identifier: GPL-2.0-only
/*
 * Unit-test runner.
 *
 * Walks every case registered in the alloy_tests section (see tests/test.h).
 * Per-driver cases live in tests/drivers/, the core cases in tests/core/;
 * Both trees are wildcarded by the Makefile, so this file never changes when
 * new mouse or test file is added.
 *
 * No hardware involved - the HID layer is mocked (tests/mock_hid.c).
 */
#include <stdio.h>

#include "test.h"

/* Section bounds emitted by the linker for the alloy_tests section */
extern const struct alloy_test __start_alloy_tests[];
extern const struct alloy_test __stop_alloy_tests[];

int alloy_test_failures;

int main(void)
{
	const struct alloy_test *t;

	for (t = __start_alloy_tests; t < __stop_alloy_tests; t++)
		t->fn();

	if (alloy_test_failures) {
		printf("%d failure(s)\n", alloy_test_failures);
		return 1;
	}
	printf("all tests passed\n");
	return 0;
}
