#include "controller_scan_cache.hpp"
#include "log.hpp"

#include <algorithm>
#include <cstring>

namespace controller_scan_cache
{
    std::optional<topology> device_topology()
    {
        UINT count = 0;
        if (GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST)) == UINT(-1)) return {};
        std::vector<RAWINPUTDEVICELIST> list(count);
        if (count)
        {
            const UINT got = GetRawInputDeviceList(list.data(), &count, sizeof(RAWINPUTDEVICELIST));
            if (got == UINT(-1)) return {}; // includes a plug during the two calls: retry next tick
            list.resize(got);
        }
        topology result;
        for (const auto& item : list)
        {
            UINT length = 0;
            if (GetRawInputDeviceInfoW(item.hDevice, RIDI_DEVICENAME, nullptr, &length) == UINT(-1)) return {};
            std::wstring name(length, L'\0');
            if (length && GetRawInputDeviceInfoW(item.hDevice, RIDI_DEVICENAME, name.data(), &length) == UINT(-1)) return {};
            name.resize(length);
            result.push_back({reinterpret_cast<std::uintptr_t>(item.hDevice), item.dwType, std::move(name)});
        }
        std::ranges::sort(result);
        return result;
    }

    BOOL CALLBACK cache::collect(LPCDIDEVICEINSTANCEW info, void* user)
    {
        auto& out = *static_cast<snapshot*>(user);
        if (!info || info->dwSize < sizeof(DWORD) || info->dwSize > sizeof(DIDEVICEINSTANCEW))
        {
            out.result = DIERR_GENERIC;
            return DIENUM_STOP;
        }
        instance copy;
        std::memcpy(copy.bytes.data(), info, info->dwSize);
        out.devices.push_back(copy);
        return DIENUM_CONTINUE; // always collect the complete list, even if a later caller stops early
    }

    std::shared_ptr<const snapshot> cache::request(key query, enumerate_t real, std::uint64_t now)
    {
        std::lock_guard lock(mutex_);
        for (const auto& item : entries_)
            if (item->query == query)
            {
                item->used = now;
                replayed_ += item->published != nullptr;
                return item->published;
            }
        auto item = std::make_shared<entry>(query, real);
        item->used = now;
        entries_.push_back(std::move(item));
        return {};
    }

    std::shared_ptr<const snapshot> cache::wait_for_first(key query)
    {
        std::unique_lock lock(mutex_);
        const auto at = std::ranges::find_if(entries_, [&](const auto& item) { return item->query == query; });
        if (at == entries_.end()) return {};
        const auto item = *at;
        changed_.wait(lock, [&] { return item->published != nullptr; });
        return item->published;
    }

    void cache::tick(std::uint64_t now, std::optional<topology> devices)
    {
        // Destruction/Release and every external call stay outside the publication lock.
        std::vector<std::shared_ptr<entry>> work, retired;
        {
            std::lock_guard lock(mutex_);
            if (!devices || !topology_ || *devices != *topology_) ++generation_;
            topology_ = std::move(devices);
            for (auto it = entries_.begin(); it != entries_.end();)
            {
                const auto& item = *it;
                const auto idle = now >= item->used ? now - item->used : 0;
                if (idle >= 60000)
                {
                    retired.push_back(item);
                    it = entries_.erase(it);
                    continue;
                }
                // One-shot engine/menu queries expire without polling a dead COM object forever.
                if (idle <= 2000 && (!item->published ||
                    (FAILED(item->published->result) ? now - item->scanned >= 1000 :
                     (item->generation != generation_ && (topology_ || now - item->scanned >= 1000)) ||
                     now - item->scanned >= 30000))) work.push_back(item);
                ++it;
            }
        }
        for (const auto& item : work)
        {
            auto next = std::make_shared<snapshot>();
            const auto result = item->real(item->query.object, item->query.type, &collect, next.get(), item->query.flags);
            if (SUCCEEDED(next->result)) next->result = result;
            if (FAILED(next->result)) next->devices.clear(); // never publish a partial failed enumeration
            {
                std::lock_guard lock(mutex_);
                item->published = std::move(next);
                item->scanned = now;
                item->generation = generation_;
                ++real_;
            }
            changed_.notify_all();
        }
    }

    std::array<std::uint64_t, 2> cache::counts()
    {
        std::lock_guard lock(mutex_);
        return {real_, replayed_};
    }

    namespace
    {
        // The worker's module is pinned; neither CRT teardown nor FreeLibrary can
        // destroy its cache while a driver call is in progress. The OS reclaims it at exit.
        cache& shared() { static auto* value = new cache; return *value; }
        HANDLE wake = nullptr;
        DWORD WINAPI worker(void*)
        {
            ULONGLONG next_log = 0;
            for (;;)
            {
                shared().tick(GetTickCount64(), device_topology());
                const auto now = GetTickCount64();
                const auto counts = shared().counts();
                if (counts[0] && now >= next_log)
                {
                    logger::write("dinput: controller scans served from the device list (%llu real, %llu cached)", counts[0], counts[1]);
                    next_log = now + 30000;
                }
                WaitForSingleObject(wake, 250);
            }
        }
    }
    bool start()
    {
        static const bool ready = []
        {
            HMODULE pinned = nullptr;
            if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&worker), &pinned)) return false;
            wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            if (!wake) return false;
            const HANDLE thread = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
            if (!thread) { CloseHandle(wake); wake = nullptr; return false; }
            CloseHandle(thread);
            logger::write("dinput: controller scan cache enabled (background scans, 30 s safety refresh)");
            return true;
        }();
        return ready;
    }
    std::shared_ptr<const snapshot> request(key query, enumerate_t real)
    {
        const auto result = shared().request(query, real, GetTickCount64());
        if (result) return result;
        SetEvent(wake);
        return shared().wait_for_first(query);
    }
}
