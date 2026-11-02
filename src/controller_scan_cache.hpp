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

    // True on the scan worker's thread, and on any thread while it runs a cache scan's
    // native enumeration (tick()). The hook then passes the call straight to the original:
    // under Wine, IDirectInput8A/7A::EnumDevices calls the W interface through its patched
    // vtable, so the worker's own raw scan re-enters the hook. Waiting on the cache there
    // waited for itself (black screen under Proton, 2026-11-02).
    bool scanning_here();

    // The longest a cold query waits for its first background publication. After that the
    // caller enumerates synchronously and the cache stays off for the session: only a scan
    // that would otherwise hang the game (or a first DirectInput scan this slow) reaches it.
    constexpr DWORD first_scan_timeout_ms = 10000;

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
        // their first publication can wait (at most timeout_ms; nullptr then);
        // cached and refreshing queries cannot.
        std::shared_ptr<const snapshot> wait_for_first(key query, DWORD timeout_ms);
        void tick(std::uint64_t now, std::optional<topology> devices);
        std::array<std::uint64_t, 2> counts(); // real scans, snapshots served
    };

    // A process-lifetime worker, started outside DllMain on the first enumeration.
    // False on a worker setup failure: the hook retains its original path.
    bool start();
    // nullptr: the caller enumerates synchronously - a first publication took longer than
    // first_scan_timeout_ms (logged once; every later query of the session is synchronous
    // too), or the caller is the scan itself (scanning_here()).
    std::shared_ptr<const snapshot> request(key query, enumerate_t real);
}
