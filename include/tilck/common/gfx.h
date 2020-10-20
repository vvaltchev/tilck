/* SPDX-License-Identifier: BSD-2-Clause */

#pragma once

#define TILCK_MIN_RES_X                            640
#define TILCK_MIN_RES_Y                            480

bool is_tilck_known_resolution(u32 w, u32 h);
bool is_tilck_default_resolution(u32 w, u32 h);
bool is_tilck_usable_resolution(u32 w, u32 h);
