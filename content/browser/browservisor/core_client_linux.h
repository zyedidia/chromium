// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_BROWSERVISOR_CORE_CLIENT_LINUX_H_
#define CONTENT_BROWSER_BROWSERVISOR_CORE_CLIENT_LINUX_H_

#include <sys/types.h>

#include "base/files/scoped_file.h"
#include "base/process/launch.h"

namespace base {
class CommandLine;
}

namespace content {

class ZygoteCommunication;

namespace browservisor {

// Returns true when the browser runs under the Browservisor core, which
// launches the zygotes and every other child process.
bool IsCoreEnabled();

// Asks the core to launch the zygote described by `cmd_line`. The core gives
// the zygote each descriptor in `additional_remapped_fds` at its target number.
// Sets `control_fd` to the zygote's control socket, which leads to the core,
// and returns the zygote's pid.
pid_t LaunchZygote(const base::CommandLine& cmd_line,
                   const base::FileHandleMappingVector& additional_remapped_fds,
                   base::ScopedFD* control_fd);

// Returns the launcher for children that would otherwise be launched without a
// zygote. It speaks the zygote protocol, and the core starts each child with a
// fresh exec.
ZygoteCommunication* GetDirectLauncher();

}  // namespace browservisor
}  // namespace content

#endif  // CONTENT_BROWSER_BROWSERVISOR_CORE_CLIENT_LINUX_H_
