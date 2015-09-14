#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "testutil/testutil.h"
#include "testutil_priv.h"

jmp_buf tu_case_jb;
int tu_case_reported;
int tu_case_failed;
int tu_case_fail_idx;
int tu_case_idx;

const char *tu_case_name;

#define TU_CASE_BUF_SZ      1024

static char tu_case_buf[TU_CASE_BUF_SZ];
static int tu_case_buf_len;

void
tu_case_abort(void)
{
    tu_case_write_pass_auto();
    longjmp(tu_case_jb, 1);
}

static int
tu_case_vappend_buf(const char *format, va_list ap)
{
    char *dst;
    int maxlen;
    int rc;

    dst = tu_case_buf + tu_case_buf_len;
    maxlen = TU_CASE_BUF_SZ - tu_case_buf_len;

    rc = vsnprintf(dst, maxlen, format, ap);
    tu_case_buf_len += rc;
    if (tu_case_buf_len >= TU_CASE_BUF_SZ) {
        tu_case_buf_len = TU_CASE_BUF_SZ - 1;
        return -1;
    }

    return 0;
}

static int
tu_case_append_buf(const char *format, ...)
{
    va_list ap;
    int rc;

    va_start(ap, format);
    rc = tu_case_vappend_buf(format, ap);
    va_end(ap);

    return rc;
}

static void
tu_case_set_name(const char *name)
{
    tu_case_name = name;
}

void
tu_case_init(const char *name)
{
    int rc;

    tu_case_reported = 0;
    tu_case_failed = 0;

    tu_case_set_name(name);
    rc = tu_report_mkdir_case();
    assert(rc == 0);

    tu_case_fail_idx = 0;
}

void
tu_case_complete(void)
{
    tu_case_idx++;
}

static void
tu_case_write_fail_buf(void)
{
    char filename[14];
    int rc;

    if (tu_config.tc_verbose) {
        printf("[FAIL] %s/%s %s", tu_suite_name, tu_case_name, tu_case_buf);
    }

    rc = snprintf(filename, sizeof filename, "fail-%04d.txt",
                  tu_case_fail_idx++);
    assert(rc < sizeof filename);

    rc = tu_report_write_file(filename, (uint8_t *)tu_case_buf,
                                    tu_case_buf_len);
    assert(rc == 0);

    tu_case_reported = 1;
    tu_case_failed = 1;
    tu_suite_failed = 1;
    tu_any_failed = 1;
}

static void
tu_case_append_file_info(const char *file, int line)
{
    int rc;

    rc = tu_case_append_buf("|%s:%d| ", file, line);
    assert(rc == 0);
}

static void
tu_case_append_assert_msg(const char *expr)
{
    int rc;

    rc = tu_case_append_buf("failed assertion: %s", expr);
    assert(rc == 0);
}

static void
tu_case_write_pass_buf(void)
{
    int rc;

    if (tu_config.tc_verbose) {
        printf("[pass] %s/%s\n", tu_suite_name, tu_case_name);
        if (tu_case_buf_len > 0) {
            printf("%s", tu_case_buf);
        }
    }

    rc = tu_report_write_file("pass.txt", (uint8_t *)tu_case_buf,
                              tu_case_buf_len);
    assert(rc == 0);

    tu_case_reported = 1;
}

static void
tu_case_append_manual_pass_msg(void)
{
    int rc;

    rc = tu_case_append_buf("manual pass");
    assert(rc == 0);
}

void
tu_case_write_pass_auto(void)
{
    if (!tu_case_reported) {
        tu_case_buf_len = 0;
        tu_case_write_pass_buf();
    }
}

void
tu_case_fail_assert(int fatal, const char *file, int line,
                    const char *expr, const char *format, ...)
{
    va_list ap;
    int rc;

    tu_case_buf_len = 0;

    tu_case_append_file_info(file, line);
    tu_case_append_assert_msg(expr);

    if (format != NULL) {
        rc = tu_case_append_buf("\n");
        assert(rc == 0);

        va_start(ap, format);
        rc = tu_case_vappend_buf(format, ap);
        assert(rc == 0);
        va_end(ap);
    }

    rc = tu_case_append_buf("\n");
    assert(rc == 0);

    tu_case_write_fail_buf();

    if (fatal) {
        tu_case_abort();
    }
}

void
tu_case_pass_manual(const char *file, int line, const char *format, ...)
{
    va_list ap;
    int rc;

    if (tu_case_reported) {
        return;
    }

    tu_case_buf_len = 0;

    tu_case_append_file_info(file, line);
    tu_case_append_manual_pass_msg();

    if (format != NULL) {
        rc = tu_case_append_buf("\n");
        assert(rc == 0);

        va_start(ap, format);
        rc = tu_case_vappend_buf(format, ap);
        assert(rc == 0);
        va_end(ap);
    }

    rc = tu_case_append_buf("\n");
    assert(rc == 0);

    tu_case_write_pass_buf();

    tu_case_abort();
}