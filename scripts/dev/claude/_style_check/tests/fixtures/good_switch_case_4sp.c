/* SPDX-License-Identifier: BSD-2-Clause */

/*
 * Compliant under `--indent 4`: case labels are indented one full
 * level (+4 columns) from `switch`. The same file is a VIOLATION
 * under `--indent 3`, which is exactly what the indent-aware test
 * asserts.
 */

int classify(int x)
{
    int rc;

    switch (x) {
        case 1:
            rc = 100;
            break;
        case 2:
            rc = 200;
            break;
        default:
            rc = 0;
            break;
    }

    return rc;
}
