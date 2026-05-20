/* SPDX-License-Identifier: BSD-2-Clause */

enum colour {
   RED,
   GREEN,
   BLUE,    /* trailing comma -- violation */
};

enum number {
   ONE,
   TWO,
   THREE    /* ok -- no trailing comma */
};
