#include <ktest.h>
#include <stdc.h>
#include <malloc.h>

/* Borrowed from mips/malta.c */
char *kenv_get(const char *key);

/* Stores currently running test data. */
static test_entry_t *current_test = NULL;
/* A null-terminated array of pointers to the tested test list. */
static test_entry_t **autorun_tests;

/* We only need this pool to allocate some memory for performing operations on
 * the list of all tests. */
static MALLOC_DEFINE(test_pool, "test data pool");

static uint32_t init_seed = 0; /* The initial seed, as set from command-line. */
static uint32_t seed = 0;      /* Current seed */
static uint32_t rand() {
  /* Just a standard LCG */
  seed = 1664525 * seed + 1013904223;
  return seed;
}

void ktest_failure() {
  if (current_test == NULL)
    panic("current_test == NULL in ktest_failure! This is most likely a bug in "
          "the test framework!\n");
  kprintf(TEST_FAILED_STRING);
  if (autorun_tests) {
    kprintf("Failure while running multiple tests.\n");
    for (test_entry_t **ptr = autorun_tests; *ptr != NULL; ptr++) {
      test_entry_t *t = *ptr;
      kprintf("  %s", t->test_name);
      if (t == current_test) {
        kprintf("  <---- FAILED\n");
        break;
      } else {
        kprintf("\n");
      }
    }
    kprintf("The seed used for this test order was: %ld. Start kernel with "
            "`test=all seed=%ld` to reproduce this test case.\n",
            init_seed, init_seed);
  } else {
    kprintf("Failure while running single test.\n");
    kprintf("Failing test: %s\n", current_test->test_name);
  }
  panic("Halting kernel on failed test.\n");
}

static test_entry_t *find_test_by_name(const char *test) {
  SET_DECLARE(tests, test_entry_t);
  test_entry_t **ptr;
  SET_FOREACH(ptr, tests) {
    if (strcmp((*ptr)->test_name, test) == 0) {
      return *ptr;
    }
  }
  return NULL;
}

static int run_test(test_entry_t *t) {
  /* These are messages to the user, so I intentionally use kprintf instead of
   * log. */
  kprintf("Running test %s.\n", t->test_name);
  if (t->flags & KTEST_FLAG_NORETURN)
    kprintf("WARNING: This test will never return, it is not possible to "
            "automatically verify its success.\n");
  if (t->flags & KTEST_FLAG_USERMODE)
    kprintf("WARNING: This test will enters usermode.\n");
  if (t->flags & KTEST_FLAG_DIRTY)
    kprintf("WARNING: This test will break kernel state. Kernel reboot will be "
            "required to run any other test.\n");

  current_test = t;
  int result = t->test_func();
  if (result == KTEST_FAILURE)
    ktest_failure();
  return result;
}

inline static int test_is_autorunnable(test_entry_t *t) {
  return !(t->flags & KTEST_FLAG_NORETURN) && !(t->flags & KTEST_FLAG_DIRTY) &&
         !(t->flags & KTEST_FLAG_BROKEN);
}

static int test_name_compare(const void *a_, const void *b_) {
  const test_entry_t *a = *(test_entry_t **)a_;
  const test_entry_t *b = *(test_entry_t **)b_;
  return strncmp(a->test_name, b->test_name, KTEST_NAME_MAX);
}

static void run_all_tests() {
  kmalloc_init(test_pool);
  kmalloc_add_pages(test_pool, 1);

  /* First, count the number of tests that may be run in any order. */
  unsigned int n = 0;
  SET_DECLARE(tests, test_entry_t);
  test_entry_t **ptr;
  SET_FOREACH(ptr, tests) {
    if (test_is_autorunnable(*ptr))
      n++;
  }
  /* Now, allocate memory for test we'll be running. */
  autorun_tests = kmalloc(test_pool, (n + 1) * sizeof(test_entry_t *), M_ZERO);
  /* Collect test pointers. */
  int i = 0;
  SET_FOREACH(ptr, tests) {
    if (test_is_autorunnable(*ptr))
      autorun_tests[i++] = *ptr;
  }
  autorun_tests[i] = NULL;

  /* Sort tests alphabetically by name, so that shuffling may be deterministic
   * and not affected by build/link order. */
  qsort(autorun_tests, n, sizeof(autorun_tests), test_name_compare);

  /* TODO: Shuffle autorun_tests pointers using seed from command line! */
  const char *seed_str = kenv_get("seed");
  if (seed_str)
    init_seed = strtoul(seed_str, NULL, 10);

  if (init_seed != 0) {
    /* Initialize LCG with seed.*/
    seed = init_seed;
    /* Yates-Fisher shuffle. */
    for (i = 0; i <= n - 2; i++) {
      int j = i + rand() % (n - i);
      register test_entry_t *swap = autorun_tests[i];
      autorun_tests[i] = autorun_tests[j];
      autorun_tests[j] = swap;
    }
  }

  kprintf("Found %d automatically runnable tests.\n", n);
  kprintf("Planned test order:\n");
  for (i = 0; i < n; i++)
    kprintf("  %s\n", autorun_tests[i]->test_name);

  for (i = 0; i < n; i++) {
    current_test = autorun_tests[i];
    /* If the test fails, run_test will not return. */
    run_test(current_test);
  }

  /* If we've managed to get here, it means all tests passed with no issues. */
  kprintf(TEST_PASSED_STRING);
}

void ktest_main(const char *test) {
  if (strncmp(test, "all", 3) == 0) {
    run_all_tests();
  } else {
    /* Single test mode */
    test_entry_t *t = find_test_by_name(test);
    if (!t) {
      kprintf("Test \"%s\" not found!", test);
    }
    int result = run_test(t);
    if (result == KTEST_SUCCESS)
      kprintf(TEST_PASSED_STRING);
  }
}
