#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace profile_progress
{
    // LC seam only: published in the same guarded startup transaction.
    std::vector<std::uint8_t> notification_bytes();
    void activate();
    void reset_campaign();
    // Called around EVERY native load, before any native stats can notify.
    void begin_load();
    void finish_load(int mode, bool validated_owned_disk, bool native_ok);
    // Deferred personal profile persistence; no disk I/O from recompute.
    bool pending_profile(std::uint8_t* bytes, std::size_t size);
    void profile_written(const std::uint8_t* bytes, std::size_t size);
}
