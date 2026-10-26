#pragma once

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

// Raw enumeration only: game callbacks, spoofing and virtual pads run on the caller.
namespace controller_scan_cache
{
    using enumerate_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIENUMDEVICESCALLBACKW, void*, DWORD);
    struct key
    {
        void* object;
        int version;
        bool wide;
        DWORD type, flags;
        bool operator==(const key&) const = default;
    };
    struct instance
    {
        alignas(DIDEVICEINSTANCEW) std::array<std::byte, sizeof(DIDEVICEINSTANCEW)> bytes{};
    };
    struct snapshot
    {
        HRESULT result = DI_OK;
        std::vector<instance> devices;
    };
    struct raw_device
    {
        std::uintptr_t handle;
        DWORD type;
        std::wstring name;
        auto operator<=>(const raw_device&) const = default;
    };
    using topology = std::vector<raw_device>;
    std::optional<topology> device_topology();

    // tick() is called by one background thread. request() only copies a published
    // snapshot; it never enumerates, detects topology or waits for a scan.
    class cache
    {
        struct entry
        {
            key query;
            enumerate_t real;
            std::shared_ptr<const snapshot> published;
            std::uint64_t used = 0, scanned = 0, generation = 0;
            entry(key k, enumerate_t fn) : query(k), real(fn) { static_cast<IUnknown*>(k.object)->AddRef(); }
            ~entry() { static_cast<IUnknown*>(query.object)->Release(); }
        };
        std::mutex mutex_;
        std::condition_variable changed_;
        std::vector<std::shared_ptr<entry>> entries_;
        std::optional<topology> topology_;
        std::uint64_t generation_ = 1;
        std::uint64_t real_ = 0, replayed_ = 0;
        static BOOL CALLBACK collect(LPCDIDEVICEINSTANCEW info, void* user);
    public:
        std::shared_ptr<const snapshot> request(key query, enumerate_t real, std::uint64_t now);
        // Cold queries keep DirectInput's complete-first-list contract. Only
        // their first publication can wait; cached and refreshing queries cannot.
        std::shared_ptr<const snapshot> wait_for_first(key query);
        void tick(std::uint64_t now, std::optional<topology> devices);
        std::array<std::uint64_t, 2> counts(); // real scans, snapshots served
    };

    // A process-lifetime worker, started outside DllMain on the first enumeration.
    // False on a worker setup failure: the hook retains its original path.
    bool start();
    std::shared_ptr<const snapshot> request(key query, enumerate_t real);
}
