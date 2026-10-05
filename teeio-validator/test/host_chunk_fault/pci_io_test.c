/* Copyright 2026 Intel. SPDX-License-Identifier: BSD-3-Clause */
#include <errno.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "host_chunk_fault.h"
#include "../common/host_check.h"

static jmp_buf failed_transfer;
static ssize_t transfer_result;
static unsigned calls;
static bool writing, interrupted, expect_failure;
static uint32_t test_word = 0xa5b6c7d8;
static off_t position;

static off_t test_lseek(int fd, off_t offset, int whence)
{
    assert(fd == 7 && whence == SEEK_SET);
    position = offset;
    return offset;
}

static ssize_t transfer(int fd, size_t count, bool write_op)
{
    assert(fd == 7 && count == 4 && position == 0x110 && write_op == writing);
    position = -1;
    calls++;
    if (interrupted && calls == 1) {
        errno = EINTR;
        return -1;
    }
    errno = EIO;
    return transfer_result;
}

static ssize_t test_read(int fd, void *buffer, size_t count)
{
    ssize_t result = transfer(fd, count, false);
    if (result == 4) memcpy(buffer, &test_word, 4);
    return result;
}

static ssize_t test_write(int fd, const void *buffer, size_t count)
{
    assert(memcmp(buffer, &test_word, 4) == 0);
    return transfer(fd, count, true);
}

static _Noreturn void test_exit(int code)
{
    assert(expect_failure && code == EXIT_FAILURE);
    longjmp(failed_transfer, 1);
}

#define lseek test_lseek
#define read test_read
#define write test_write
#define exit test_exit
#include "../../library/helperlib/pcie_helper.c"
#undef exit
#undef write
#undef read
#undef lseek

bool g_pci_log = false;
void teeio_debug_print(int level, const char *format, ...) {}
void teeio_assert(const char *file, int line, const char *description) { abort(); }
#include "../common/host_check.h"

void pci_io_test_check(bool write_op, ssize_t result, bool interrupt)
{
    writing = write_op;
    transfer_result = result;
    interrupted = interrupt;
    expect_failure = result != 4;
    calls = 0;
    if (setjmp(failed_transfer) == 0) {
        if (writing) device_pci_write_32(0x110, test_word, 7);
        else assert(device_pci_read_32(0x110, 7) == test_word);
        assert(!expect_failure);
    } else {
        assert(expect_failure);
    }
    assert(calls == (interrupted ? 2 : 1));
}
