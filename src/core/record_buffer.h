// Fixed-size RAM record store shared by modules that retain the latest captured
// record (IR record, later RF record/replay). One buffer per ModuleRuntime;
// `empty()` is the availability signal the runtime mirrors into
// status.hasBuffer. Portable: no Arduino, no allocation, no JSON dependency, so
// replace/clear/empty and the capacity boundary are native-testable.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

class RecordBuffer {
  public:
    // Largest record the wire path may retain (spec §7.3 payload cap).
    static constexpr size_t capacity = 4096;

    // Replaces the whole record atomically. A null pointer, zero length or an
    // over-capacity length is rejected and the previous record is left fully
    // intact, so an invalid capture can never corrupt a stored record.
    bool replace(const uint8_t *data, size_t length) {
        if (data == nullptr || length == 0 || length > capacity) return false;
        std::memcpy(data_, data, length);
        size_ = length;
        return true;
    }

    // Makes the buffer unavailable; the previous bytes are never observable
    // afterwards (data() returns nullptr, size() is 0).
    void clear() { size_ = 0; }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }

    // Non-null exactly while !empty(), valid for size() bytes. Never
    // dereference when empty() — there is no record to read.
    const uint8_t *data() const { return size_ == 0 ? nullptr : data_; }

  private:
    uint8_t data_[capacity] = {};
    size_t size_ = 0;
};
