// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/backend.h"

namespace Common::Log {

void Initialize(std::string_view) {}
bool IsActive() { return false; }
void Start() {}
void Stop() {}
void Denitializer() {}
void SetGlobalFilter(const Filter&) {}
void SetColorConsoleBackendEnabled(bool) {}
void SetAppend() {}

} // namespace Common::Log
