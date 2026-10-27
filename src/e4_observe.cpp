#include "e4_observe.hpp"
#include "entity_capacity.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"
#include "e4_core_state.hpp"

namespace e4_observe
{
    namespace {
        bool active = false;
        // Only logging is recoverable here. The native operation is outside
        // these scopes, and native_exception unconditionally fail-stops after it.
        void log_cpp(const char* site,DWORD code,void* address) noexcept {
            try { logger::write("E4: native lifecycle exception at %s: code=%08lx address=%p",site,code,address); }
            catch(...) {}
        }
        void log_exception(const char* site,EXCEPTION_POINTERS* exception) {
            __try { log_cpp(site,exception->ExceptionRecord->ExceptionCode,exception->ExceptionRecord->ExceptionAddress); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }
    void configure() { active = ini::flag(L"Test", L"E4Observe", false) || e4_core_state::eligible; patch_transaction::record_verified_writes = active; }
    bool enabled() { return active; }
    [[noreturn]] void fatal(const char* site) { entity_capacity::fail_stop_lifecycle(site); }
    int native_exception(const char* site, EXCEPTION_POINTERS* exception)
    {
        if (active)
        {
            log_exception(site,exception);
            fatal(site);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
}
