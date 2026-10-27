#pragma once
#include <Windows.h>
#include <string>
#include <cstdint>
namespace patch_transaction {class transaction;}

// Stage-2 diagnostics are explicitly opt-in. Probes remain in their original
// recovery scopes; only native operations pass through native_call.
namespace e4_observe
{
    void configure();
    void install(HMODULE game);
    // Core-only preparation, inside the shared protected startup transaction.
    bool contribute_core(patch_transaction::transaction&,HMODULE,std::string&);
    void core_checkpoint();
    void core_team_lifecycle(unsigned,bool);
    void core_fold_snapshot(unsigned,std::uint32_t,bool);
    void sample();
    std::string json();
#ifdef XML2_E4_TEST
    void test_emit(unsigned point, bool post, const unsigned* registers);
#endif
    bool enabled();
    [[noreturn]] void fatal(const char* site);
    int native_exception(const char* site, EXCEPTION_POINTERS* exception);

    template<class F> __declspec(noinline) auto native_call(const char* site, F&& call) -> decltype(call())
    {
        __try { return call(); }
        __except(native_exception(site, GetExceptionInformation()))
        {
            // The filter either continues searching (OFF) or terminates (ON).
            // Never translate an SEH record into a bare C++ rethrow.
            fatal(site);
        }
    }
}
