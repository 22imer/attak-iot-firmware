// Fixed-capacity FIFO for accepted dashboard commands. Capacity 13 =
// 8 ordinary (enable/action) slots + one reserved Stop (disable) slot per
// module. One shared FIFO order — Stops never overtake ordinary commands and a
// duplicate queued Stop for the same module is rejected instead of consuming
// another module's reserved slot. Pure: no threads, no heap, no JSON.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "command_types.h"
#include "ws_command.h"

struct EnqueuedCommand {
    uint32_t clientId = 0;
    uint64_t sessionToken = 0;
    WsCommand command;
};

class CommandQueue {
  public:
    // Returns None on admission, the command's own error for an already-invalid
    // command, InvalidCommand for an uncorrelated/unknown module, or QueueFull
    // when the relevant capacity is exhausted.
    CommandError push(const EnqueuedCommand &request);

    // Removes the oldest accepted request and frees its slot (not the backend).
    bool pop(EnqueuedCommand &request);

  private:
    static constexpr size_t kNormalCapacity = 8;
    static constexpr size_t kCapacity = kNormalCapacity + kModuleCount; // 13

    std::array<EnqueuedCommand, kCapacity> items_{};
    std::array<bool, kModuleCount> stopQueued_{}; // one pending Stop per module
    size_t head_ = 0;
    size_t size_ = 0;
    size_t normalCount_ = 0;
};
