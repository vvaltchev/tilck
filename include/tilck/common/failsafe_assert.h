
#pragma once

/*
 * This whole file is a hack allowing code in common/ to compile when its part
 * of some other code where ASSERT cannot be used. A specific example is the
 * EFI bootloader: ASSERT cannot be used because gnu-efi uses it and therefore
 * we defined NO_TILCK_ASSERT but, at the same time, we'd like code in common/
 * to be build-able with the compile options. A solution is to use a hack like
 * this one. Another possible one is to make the build system smarter and to
 * use different flags (allowing the Tilck's ASSERT) specifically for code in
 * common/, but not for the rest (of the bootloader in this case).
 */

#ifndef _TILCK_BASIC_DEFS_H
#error This header must be included AFTER common/basic_defs.h
#endif

#if defined(NO_TILCK_ASSERT) && !defined(ASSERT)
#define ASSERT(x)
#endif
