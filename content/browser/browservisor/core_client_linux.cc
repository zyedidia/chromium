// Copyright 2026 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/browser/browservisor/core_client_linux.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/pickle.h"
#include "base/posix/unix_domain_socket.h"
#include "base/strings/string_number_conversions.h"
#include "base/synchronization/lock.h"
#include "content/common/zygote/zygote_communication_linux.h"
#include "sandbox/policy/switches.h"

namespace content::browservisor {

namespace {

// The descriptor of the core's launch socket.
constexpr char kCoreFdSwitch[] = "browservisor-core-fd";

constexpr int kCommandLaunchZygote = 0;

// The kinds of launcher the core starts, as numbered on the wire.
enum class LauncherKind : int {
  kSandboxed = 0,
  kUnsandboxed = 1,
  kDirect = 2,
};

int CoreFd() {
  static const int fd = [] {
    int value = -1;
    const auto& command_line = *base::CommandLine::ForCurrentProcess();
    if (!base::StringToInt(command_line.GetSwitchValueASCII(kCoreFdSwitch),
                           &value)) {
      return -1;
    }
    return value;
  }();
  return fd;
}

// Requests and replies on the launch socket must not interleave.
base::Lock& CoreLock() {
  static base::NoDestructor<base::Lock> lock;
  return *lock;
}

pid_t Launch(LauncherKind kind,
             const std::vector<std::string>& argv,
             const base::FileHandleMappingVector& fds,
             base::ScopedFD* control_fd) {
  base::Pickle request;
  request.WriteInt(kCommandLaunchZygote);
  request.WriteInt(static_cast<int>(kind));
  request.WriteInt(static_cast<int>(argv.size()));
  for (const std::string& arg : argv) {
    request.WriteString(arg);
  }
  request.WriteInt(static_cast<int>(fds.size()));
  std::vector<int> raw_fds;
  for (const auto& [fd, target] : fds) {
    raw_fds.push_back(fd);
    request.WriteUInt32(static_cast<uint32_t>(target));
  }

  base::AutoLock lock(CoreLock());
  CHECK(base::UnixDomainSocket::SendMsg(CoreFd(), request.AsBytes(), raw_fds))
      << "Cannot send a launch request to the Browservisor core";
  std::array<uint8_t, 64> reply;
  std::vector<base::ScopedFD> received;
  const ssize_t len =
      base::UnixDomainSocket::RecvMsg(CoreFd(), reply, &received);
  CHECK_GT(len, 0) << "No reply from the Browservisor core";
  base::PickleIterator iter = base::PickleIterator::WithData(
      base::span(reply).first(static_cast<size_t>(len)));
  int status = -1;
  int pid = -1;
  CHECK(iter.ReadInt(&status) && iter.ReadInt(&pid))
      << "Malformed reply from the Browservisor core";
  CHECK_EQ(status, 0) << "The Browservisor core refused to start a launcher";
  CHECK_EQ(received.size(), 1u);
  *control_fd = std::move(received[0]);
  return pid;
}

}  // namespace

bool IsCoreEnabled() {
  return CoreFd() >= 0;
}

pid_t LaunchZygote(const base::CommandLine& cmd_line,
                   const base::FileHandleMappingVector& additional_remapped_fds,
                   base::ScopedFD* control_fd) {
  const LauncherKind kind =
      cmd_line.HasSwitch(sandbox::policy::switches::kNoZygoteSandbox)
          ? LauncherKind::kUnsandboxed
          : LauncherKind::kSandboxed;
  return Launch(kind, cmd_line.argv(), additional_remapped_fds, control_fd);
}

ZygoteCommunication* GetDirectLauncher() {
  static base::NoDestructor<std::unique_ptr<ZygoteCommunication>> launcher([] {
    auto direct = std::make_unique<ZygoteCommunication>(
        ZygoteCommunication::ZygoteType::kUnsandboxed);
    direct->Init(base::BindOnce(
        [](base::CommandLine* cmd_line,
           base::ScopedFD* control_fd) -> ZygoteLaunchCompletionCallback {
          const pid_t pid = Launch(LauncherKind::kDirect, {}, {}, control_fd);
          return base::BindOnce([](pid_t pid) { return pid; }, pid);
        }));
    return direct;
  }());
  return launcher->get();
}

}  // namespace content::browservisor
