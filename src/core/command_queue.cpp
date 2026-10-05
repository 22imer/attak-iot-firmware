#include "command_queue.h"

CommandError CommandQueue::push(const EnqueuedCommand &request) {
    const WsCommand &command = request.command;

    if (command.error != CommandError::None) return command.error;

    const size_t module = static_cast<size_t>(command.module);
    if (!command.correlationValid || module >= stopQueued_.size()) return CommandError::InvalidCommand;

    const bool stop = command.cmd == CommandKind::Disable;
    if (size_ == items_.size() || (stop ? stopQueued_[module] : normalCount_ == kNormalCapacity)) {
        return CommandError::QueueFull;
    }

    items_[(head_ + size_) % items_.size()] = request;
    ++size_;
    if (stop) {
        stopQueued_[module] = true;
    } else {
        ++normalCount_;
    }
    return CommandError::None;
}

bool CommandQueue::pop(EnqueuedCommand &request) {
    if (size_ == 0) return false;

    request = items_[head_];
    head_ = (head_ + 1) % items_.size();
    --size_;
    if (request.command.cmd == CommandKind::Disable) {
        stopQueued_[static_cast<size_t>(request.command.module)] = false;
    } else {
        --normalCount_;
    }
    return true;
}
