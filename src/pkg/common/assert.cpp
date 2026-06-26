// SPDX-FileCopyrightText: Copyright 2021 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <stdexcept>
#include "common/arch.h"
#include "common/assert.h"

#if defined(_MSC_VER)
#include <intrin.h>
#define Crash() __debugbreak()
#elif defined(ARCH_X86_64)
#define Crash() __asm__ __volatile__("int $3")
#elif defined(ARCH_ARM64)
#define Crash() __asm__ __volatile__("brk 0")
#else
#error "Missing Crash() implementation for target CPU architecture."
#endif

void assert_fail_impl() {
    std::fflush(stdout);
    Crash();
}

[[noreturn]] void unreachable_impl() {
    std::fflush(stdout);
    Crash();
    throw std::runtime_error("Unreachable code");
}
