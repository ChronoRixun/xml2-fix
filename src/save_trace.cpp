#include "save_trace.hpp"
#include "save_format_rules.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <map>
#include <mutex>

namespace save_trace
{
	namespace
	{
		bool enabled = false;
		std::mutex lock;
		std::map<std::string,unsigned> captured, restored;
		using capture_t = bool(__fastcall*)(void*,void*,const char*,int,float);
		using restore_t = bool(__fastcall*)(void*,void*,void*);
		capture_t native_capture = reinterpret_cast<capture_t>(0x4664d0);
		restore_t native_restore = reinterpret_cast<restore_t>(0x465c10);

		bool safe_text(const char* source, char* out)
		{
			__try
			{
				for (unsigned i=0;i<127;++i) { out[i]=source[i]; if (!out[i]) return true; }
			}
			__except(EXCEPTION_EXECUTE_HANDLER) {}
			out[0]=0;
			return false;
		}

		bool __fastcall capture(void* record,void*,const char* name,int flag,float stamp)
		{
			char zone[128]{};
			safe_text(name,zone);
			const bool result=native_capture(record,nullptr,name,flag,stamp);
			std::lock_guard guard(lock);
			++captured[zone];
			logger::write("save trace: actual capture zone=%s return=%d stamp=%.9g",zone,result,static_cast<double>(stamp));
			return result;
		}

		bool __fastcall restore(void* record,void*,void* entity)
		{
			char zone[128]{};
			safe_text(reinterpret_cast<const char*>(0x72a758),zone);
			const bool result=native_restore(record,nullptr,entity);
			std::lock_guard guard(lock);
			const auto n=++restored[zone];
			if (n==1 || n%100==0) logger::write("save trace: actual restore zone=%s calls=%u",zone,n);
			return result;
		}
	}

	std::string status()
	{
		if (!enabled) return "";
		std::lock_guard guard(lock);
		std::string out="; actual captures";
		for (const auto& [zone,count]:captured) out+=" "+zone+"="+std::to_string(count);
		out+="; actual restores";
		for (const auto& [zone,count]:restored) out+=" "+zone+"="+std::to_string(count);
		return out;
	}

	void install(HMODULE game)
	{
		if (!ini::flag(L"Debug",L"SaveTrace",false)) return;
		const save_format_rules::guard sites[]={
			{0x466a44,"e887faffff","native zone capture call"},
			{0x486260,"e8abf9fdff","native per-entity restore call"}
		};
		if (!game || reinterpret_cast<std::uintptr_t>(game)!=save_format_rules::image_base) return;
		for (const auto& site:sites)
			if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(site.va),site.hex))
			{
				logger::write("save trace: site 0x%08lx differs; telemetry not installed",site.va);
				return;
			}
		const void* targets[]={reinterpret_cast<const void*>(&capture),reinterpret_cast<const void*>(&restore)};
		for (unsigned i=0;i<2;++i)
		{
			DWORD old=0;
			if (!VirtualProtect(reinterpret_cast<void*>(sites[i].va),5,PAGE_EXECUTE_READWRITE,&old)) return;
			const auto bytes=save_format_rules::call(sites[i].va,static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(targets[i])),5);
			std::memcpy(reinterpret_cast<void*>(sites[i].va),bytes.data(),5);
			DWORD ignored=0;
			VirtualProtect(reinterpret_cast<void*>(sites[i].va),5,old,&ignored);
			FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(sites[i].va),5);
		}
		enabled=true;
		logger::write("save trace: actual capture/restore telemetry installed (forward only)");
	}
}
