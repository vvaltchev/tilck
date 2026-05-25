/* SPDX-License-Identifier: BSD-2-Clause */

/* Multi-line enum WITHOUT trailing comma -- violation */
enum colour {
   RED,
   GREEN,
   BLUE
};

/* Multi-line enum WITH trailing comma -- ok */
enum shape {
   CIRCLE,
   SQUARE,
   TRIANGLE,
};

/* Single-line enum WITH trailing comma -- violation */
enum flag { ON, OFF, };

/* Single-line enum WITHOUT trailing comma -- ok */
enum bit { ZERO, ONE };
