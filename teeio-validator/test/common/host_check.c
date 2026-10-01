/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "host_check.h"

static jmp_buf *m_unwind;
static char m_failure[512];

const char *host_check_run(void (*body)(void *), void *argument)
{
  jmp_buf unwind;
  jmp_buf *outer = m_unwind;

  m_failure[0] = '\0';
  m_unwind = &unwind;
  if (setjmp(unwind) == 0) {
    body(argument);
  }
  m_unwind = outer;
  return m_failure[0] != '\0' ? m_failure : NULL;
}

void host_check_failed(const char *expression, const char *file, int line)
{
  snprintf(m_failure, sizeof(m_failure), "%s:%d: %s", file, line, expression);
  if (m_unwind == NULL) {
    fprintf(stderr, "%s\n", m_failure);
    abort();
  }
  longjmp(*m_unwind, 1);
}
