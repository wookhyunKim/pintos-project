#include "tests/threads/tests.h"
#include <debug.h>
#include <string.h>
#include <stdio.h>

struct test 
  {
    const char *name;
    test_func *function;
  };

static const struct test tests[] = 
  {
    {"alarm-single", test_alarm_single}, // pass
    {"alarm-multiple", test_alarm_multiple}, // pass
    {"alarm-simultaneous", test_alarm_simultaneous}, // pass
    {"alarm-priority", test_alarm_priority}, // pass
    {"alarm-zero", test_alarm_zero}, // pass
    {"alarm-negative", test_alarm_negative}, // pass
    {"priority-change", test_priority_change}, // pass
    {"priority-donate-one", test_priority_donate_one}, // pass
    {"priority-donate-multiple", test_priority_donate_multiple}, // non-pass
    {"priority-donate-multiple2", test_priority_donate_multiple2}, // non-pass
    {"priority-donate-nest", test_priority_donate_nest}, // non-pass
    {"priority-donate-sema", test_priority_donate_sema}, // non-pass
    {"priority-donate-lower", test_priority_donate_lower}, // non-pass
    {"priority-donate-chain", test_priority_donate_chain}, // non-pass
    {"priority-fifo", test_priority_fifo}, // pass
    {"priority-preempt", test_priority_preempt}, // pass
    {"priority-sema", test_priority_sema}, // pass
    {"priority-condvar", test_priority_condvar}, // non-pass
    // {"mlfqs-load-1", test_mlfqs_load_1}, // pass
    // {"mlfqs-load-60", test_mlfqs_load_60}, // pass
    // {"mlfqs-load-avg", test_mlfqs_load_avg}, // pass
    // {"mlfqs-recent-1", test_mlfqs_recent_1}, // pass
    // {"mlfqs-fair-2", test_mlfqs_fair_2}, // pass
    // {"mlfqs-fair-20", test_mlfqs_fair_20}, // pass
    // {"mlfqs-nice-2", test_mlfqs_nice_2}, // pass
    // {"mlfqs-nice-10", test_mlfqs_nice_10}, // pass
    // {"mlfqs-block", test_mlfqs_block}, // pass
  };

static const char *test_name;

/* Runs the test named NAME. */
void
run_test (const char *name) 
{
  const struct test *t;

  for (t = tests; t < tests + sizeof tests / sizeof *tests; t++)
    if (!strcmp (name, t->name))
      {
        test_name = name;
        msg ("begin");
        t->function ();
        msg ("end");
        return;
      }
  PANIC ("no test named \"%s\"", name);
}

/* Prints FORMAT as if with printf(),
   prefixing the output by the name of the test
   and following it with a new-line character. */
void
msg (const char *format, ...) 
{
  va_list args;
  
  printf ("(%s) ", test_name);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  putchar ('\n');
}

/* Prints failure message FORMAT as if with printf(),
   prefixing the output by the name of the test and FAIL:
   and following it with a new-line character,
   and then panics the kernel. */
void
fail (const char *format, ...) 
{
  va_list args;
  
  printf ("(%s) FAIL: ", test_name);
  va_start (args, format);
  vprintf (format, args);
  va_end (args);
  putchar ('\n');

  PANIC ("test failed");
}

/* Prints a message indicating the current test passed. */
void
pass (void) 
{
  printf ("(%s) PASS\n", test_name);
}

