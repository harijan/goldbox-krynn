#ifndef COK_TEST_TEMP_H
#define COK_TEST_TEMP_H

#include <stdio.h>
#include <stdlib.h>

/* A new folder for a test's files, NAME and six random characters
 * (mkdtemp), in TMPDIR (make test's TEST_TMPDIR, build/tmp), else in /tmp.
 * DIR gets its path; NULL when it could not be made. */
static inline char *test_temp_dir(char *dir, size_t size, const char *name)
{
    const char *temp = getenv("TMPDIR");
    int n = snprintf(dir, size, "%s/%s-XXXXXX", temp != NULL && temp[0] != '\0' ? temp : "/tmp",
                     name);
    if (n < 0 || (size_t)n >= size) return NULL;
    return mkdtemp(dir);
}

#endif
