#include "gamepad_fix.hpp"
#include "log.hpp"
#include "pad_profile.hpp"
#include "xinput_pad.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gamepad_fix
{
	namespace
	{
		constexpr std::uint16_t microsoft_vid = 0x045E;

		const pad_profile::profile* active = &pad_profile::logitech_dual_action;

		// COM vtable slots. Every IDirectInput version shares the first ones, as do the device interfaces.
		constexpr int slot_create_device = 3;
		constexpr int slot_enum_devices = 4;
		constexpr int slot_create_device_ex = 9; // IDirectInput7 only
		constexpr int slot_get_capabilities = 3;
		constexpr int slot_enum_objects = 4;
		constexpr int slot_get_property = 5;
		constexpr int slot_set_property = 6;
		constexpr int slot_get_device_state = 9;
		constexpr int slot_get_device_info = 15;

		// Per spoofed DirectInput device.
		struct device_record
		{
			bool spoofed = false;
			int ordinal = -1; // n-th spoofed device -> n-th connected XInput pad
			bool axes_loaded = false;
			pad_profile::axis_ranges axes{};
			std::array<bool, pad_profile::axis_count> set_by_game{};
		};

		struct vtable_record
		{
			std::unordered_map<int, void*> originals; // by slot
			bool wide = false;                        // a W interface
		};

		using create_device_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, void**, LPUNKNOWN);
		using create_device_ex_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, REFIID, void**, LPUNKNOWN);
		using enum_devices_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPDIENUMDEVICESCALLBACKW, LPVOID, DWORD);
		using get_capabilities_t = HRESULT(STDMETHODCALLTYPE*)(void*, LPDIDEVCAPS);
		using enum_objects_t = HRESULT(STDMETHODCALLTYPE*)(void*, void*, LPVOID, DWORD);
		using get_property_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, LPDIPROPHEADER);
		using set_property_t = HRESULT(STDMETHODCALLTYPE*)(void*, REFGUID, LPCDIPROPHEADER);
		using get_device_info_t = HRESULT(STDMETHODCALLTYPE*)(void*, LPDIDEVICEINSTANCEW);
		using get_device_state_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, LPVOID);

		std::mutex mutex;
		std::unordered_map<void**, vtable_record> vtables;

		std::mutex devices_mutex;
		std::unordered_map<void*, device_record> device_records;
		int spoofed_count = 0;

		std::uint16_t vid_of(const GUID& product) { return static_cast<std::uint16_t>(product.Data1 & 0xFFFF); }
		std::uint16_t pid_of(const GUID& product) { return static_cast<std::uint16_t>(product.Data1 >> 16); }
		DWORD presented_vidpid() { return (static_cast<DWORD>(active->pid) << 16) | active->vid; }
		bool presents_objects() { return !active->objects.empty(); }

		// VID/PID pairs of XInput-capable HID devices ("IG_" in the device path), found via Raw Input.
		std::set<std::pair<std::uint16_t, std::uint16_t>> xinput_devices()
		{
			std::set<std::pair<std::uint16_t, std::uint16_t>> result;

			UINT count = 0;
			GetRawInputDeviceList(nullptr, &count, sizeof(RAWINPUTDEVICELIST));
			std::vector<RAWINPUTDEVICELIST> devices(count);
			count = GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST));
			if (count == static_cast<UINT>(-1))
			{
				return result;
			}

			for (UINT i = 0; i < count; ++i)
			{
				if (devices[i].dwType != RIM_TYPEHID)
				{
					continue;
				}

				UINT length = 0;
				GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICENAME, nullptr, &length);
				std::wstring name(length, L'\0');
				if (GetRawInputDeviceInfoW(devices[i].hDevice, RIDI_DEVICENAME, name.data(), &length) == static_cast<UINT>(-1))
				{
					continue;
				}

				std::ranges::transform(name, name.begin(), [](const wchar_t c) { return static_cast<wchar_t>(towupper(c)); });
				if (name.find(L"IG_") == std::wstring::npos)
				{
					continue;
				}

				const auto vid_pos = name.find(L"VID_");
				const auto pid_pos = name.find(L"PID_");
				if (vid_pos != std::wstring::npos && pid_pos != std::wstring::npos)
				{
					const auto vid = static_cast<std::uint16_t>(std::wcstoul(name.substr(vid_pos + 4, 4).c_str(), nullptr, 16));
					const auto pid = static_cast<std::uint16_t>(std::wcstoul(name.substr(pid_pos + 4, 4).c_str(), nullptr, 16));
					result.emplace(vid, pid);
				}
			}

			return result;
		}

		bool should_spoof(const GUID& product)
		{
			const auto vid = vid_of(product);
			const auto pid = pid_of(product);
			if (vid == microsoft_vid && std::ranges::find(active->native_microsoft_pids, pid) != active->native_microsoft_pids.end())
			{
				return false;
			}

			return xinput_devices().contains({vid, pid});
		}

		bool is_wide_instance(const DWORD size)
		{
			return size == sizeof(DIDEVICEINSTANCEW) || size == sizeof(DIDEVICEINSTANCE_DX3W);
		}

		// Rewrites the product GUID (and name, if the profile has one) in a DIDEVICEINSTANCE A or W;
		// the fields up to dwDevType sit at the same offsets in both.
		void spoof_instance(void* instance, const char* where)
		{
			auto* header = static_cast<LPDIDEVICEINSTANCEA>(instance);
			if (!header || header->dwSize < offsetof(DIDEVICEINSTANCEA, dwDevType) || !should_spoof(header->guidProduct))
			{
				return;
			}

			logger::write_once(std::string("spoof:") + where, "dinput: %s reports %04X:%04X -> presenting as %s (%04X:%04X)",
			                   where, vid_of(header->guidProduct), pid_of(header->guidProduct), active->description, active->vid, active->pid);
			header->guidProduct.Data1 = presented_vidpid();

			if (!active->product_name)
			{
				return;
			}
			if (is_wide_instance(header->dwSize))
			{
				auto* wide = static_cast<LPDIDEVICEINSTANCEW>(instance);
				std::swprintf(wide->tszInstanceName, MAX_PATH, L"%hs", active->product_name);
				std::swprintf(wide->tszProductName, MAX_PATH, L"%hs", active->product_name);
			}
			else if (header->dwSize >= offsetof(DIDEVICEINSTANCEA, tszProductName) + MAX_PATH)
			{
				std::snprintf(header->tszInstanceName, MAX_PATH, "%s", active->product_name);
				std::snprintf(header->tszProductName, MAX_PATH, "%s", active->product_name);
			}
		}

		void* original(void* self, const int slot)
		{
			std::lock_guard lock(mutex);
			return vtables[*static_cast<void***>(self)].originals[slot];
		}

		bool is_wide(void* self)
		{
			std::lock_guard lock(mutex);
			return vtables[*static_cast<void***>(self)].wide;
		}

		void patch_vtable(void* object, const int slot, void* replacement, const bool wide)
		{
			std::lock_guard lock(mutex);
			auto** vtable = *static_cast<void***>(object);
			auto& record = vtables[vtable];
			record.wide = wide;
			if (record.originals.contains(slot))
			{
				return;
			}

			DWORD old_protect = 0;
			VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old_protect);
			record.originals[slot] = vtable[slot];
			vtable[slot] = replacement;
			VirtualProtect(&vtable[slot], sizeof(void*), old_protect, &old_protect);
		}

		// The real device info, whichever of the A or W interfaces `device` is.
		bool real_device_info(void* device, DIDEVICEINSTANCEW& info)
		{
			const auto real_info = reinterpret_cast<get_device_info_t>(original(device, slot_get_device_info));
			for (const DWORD size : {static_cast<DWORD>(sizeof(DIDEVICEINSTANCEW)), static_cast<DWORD>(sizeof(DIDEVICEINSTANCEA))})
			{
				info = {};
				info.dwSize = size;
				if (SUCCEEDED(real_info(device, &info)))
				{
					return true;
				}
			}
			return false;
		}

		device_record& record_for(void* device)
		{
			static std::unordered_map<std::string, int> ordinals; // by instance GUID: one per physical pad

			std::lock_guard lock(devices_mutex);
			const auto [it, inserted] = device_records.try_emplace(device);
			if (inserted)
			{
				DIDEVICEINSTANCEW info{};
				if (real_device_info(device, info) && should_spoof(info.guidProduct))
				{
					const std::string key(reinterpret_cast<const char*>(&info.guidInstance), sizeof(GUID));
					const auto [ordinal, first_time] = ordinals.try_emplace(key, spoofed_count);
					if (first_time)
					{
						++spoofed_count;
						logger::write("dinput: building %04X:%04X state from XInput as %s #%d",
						              vid_of(info.guidProduct), pid_of(info.guidProduct), active->description, ordinal->second + 1);
					}
					it->second.spoofed = true;
					it->second.ordinal = ordinal->second;
				}
			}
			return it->second;
		}

		void forget_device(void* device)
		{
			std::lock_guard lock(devices_mutex);
			device_records.erase(device); // a new device may reuse a released one's address
		}

		// Reads the ranges and deadzones the game configured on the real device, for the axes it
		// didn't configure through a presented object, so synthesized values match what DirectInput would report.
		void load_axes(void* device, device_record& record)
		{
			const auto real_get_property = reinterpret_cast<get_property_t>(original(device, slot_get_property));
			for (int i = 0; i < pad_profile::axis_count; ++i)
			{
				if (record.set_by_game[i])
				{
					continue;
				}

				const auto offset = static_cast<DWORD>(i * sizeof(LONG));
				DIPROPRANGE range{{sizeof(DIPROPRANGE), sizeof(DIPROPHEADER), offset, DIPH_BYOFFSET}};
				if (SUCCEEDED(real_get_property(device, DIPROP_RANGE, &range.diph)) && range.lMax > range.lMin)
				{
					record.axes[i].min = range.lMin;
					record.axes[i].max = range.lMax;
				}

				DIPROPDWORD deadzone{{sizeof(DIPROPDWORD), sizeof(DIPROPHEADER), offset, DIPH_BYOFFSET}};
				if (SUCCEEDED(real_get_property(device, DIPROP_DEADZONE, &deadzone.diph)))
				{
					record.axes[i].deadzone = std::min<DWORD>(deadzone.dwData, 10000);
				}
			}
			record.axes_loaded = true;
		}

		// The DIJOYSTATE axes (0-5) a property header addresses.
		std::vector<int> axes_addressed(const DIPROPHEADER& header)
		{
			switch (header.dwHow)
			{
			case DIPH_DEVICE:
				return {0, 1, 2, 3, 4, 5};
			case DIPH_BYOFFSET:
				if (header.dwObj < pad_profile::axis_count * sizeof(LONG) && header.dwObj % sizeof(LONG) == 0)
				{
					return {static_cast<int>(header.dwObj / sizeof(LONG))};
				}
				return {};
			case DIPH_BYID:
				if ((DIDFT_GETTYPE(header.dwObj) & DIDFT_AXIS) && DIDFT_GETINSTANCE(header.dwObj) < pad_profile::axis_count)
				{
					return {static_cast<int>(DIDFT_GETINSTANCE(header.dwObj))};
				}
				return {};
			default:
				return {};
			}
		}

		bool is_axis_property(REFGUID property)
		{
			return &property == &DIPROP_RANGE || &property == &DIPROP_DEADZONE;
		}

		HRESULT STDMETHODCALLTYPE get_device_info(void* self, LPDIDEVICEINSTANCEW instance)
		{
			const auto result = reinterpret_cast<get_device_info_t>(original(self, slot_get_device_info))(self, instance);
			if (SUCCEEDED(result))
			{
				spoof_instance(instance, "GetDeviceInfo");
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE get_property(void* self, REFGUID property, LPDIPROPHEADER header)
		{
			const auto real = reinterpret_cast<get_property_t>(original(self, slot_get_property));

			// Axes of a presented object layout answer from what the game set.
			if (presents_objects() && header && is_axis_property(property))
			{
				auto& record = record_for(self);
				const auto axes = axes_addressed(*header);
				if (record.spoofed && axes.size() == 1)
				{
					if (!record.axes_loaded)
					{
						load_axes(self, record);
					}
					const auto& axis = record.axes[axes[0]];
					if (&property == &DIPROP_RANGE && header->dwSize >= sizeof(DIPROPRANGE))
					{
						reinterpret_cast<LPDIPROPRANGE>(header)->lMin = axis.min;
						reinterpret_cast<LPDIPROPRANGE>(header)->lMax = axis.max;
						return DI_OK;
					}
					if (&property == &DIPROP_DEADZONE && header->dwSize >= sizeof(DIPROPDWORD))
					{
						reinterpret_cast<LPDIPROPDWORD>(header)->dwData = axis.deadzone;
						return DI_OK;
					}
				}
			}

			const auto result = real(self, property, header);
			if (FAILED(result) || !header)
			{
				return result;
			}

			if (&property == &DIPROP_VIDPID)
			{
				auto* value = reinterpret_cast<LPDIPROPDWORD>(header);
				GUID product{};
				product.Data1 = value->dwData;
				if (should_spoof(product))
				{
					value->dwData = presented_vidpid();
				}
			}
			else if ((&property == &DIPROP_PRODUCTNAME || &property == &DIPROP_INSTANCENAME) && active->product_name && record_for(self).spoofed)
			{
				std::swprintf(reinterpret_cast<LPDIPROPSTRING>(header)->wsz, MAX_PATH, L"%hs", active->product_name);
			}
			return result;
		}

		// Records the ranges and deadzones the game sets, so presented axes the real device lacks
		// (e.g. the Dual Action's Rz) behave like real ones.
		HRESULT STDMETHODCALLTYPE set_property(void* self, REFGUID property, LPCDIPROPHEADER header)
		{
			const auto result = reinterpret_cast<set_property_t>(original(self, slot_set_property))(self, property, header);
			if (!header || !is_axis_property(property))
			{
				return result;
			}

			auto& record = record_for(self);
			if (!record.spoofed)
			{
				return result;
			}

			const auto axes = axes_addressed(*header);
			for (const int i : axes)
			{
				if (&property == &DIPROP_RANGE && header->dwSize >= sizeof(DIPROPRANGE))
				{
					const auto* range = reinterpret_cast<LPCDIPROPRANGE>(header);
					record.axes[i].min = range->lMin;
					record.axes[i].max = range->lMax;
				}
				else if (&property == &DIPROP_DEADZONE && header->dwSize >= sizeof(DIPROPDWORD))
				{
					record.axes[i].deadzone = std::min<DWORD>(reinterpret_cast<LPCDIPROPDWORD>(header)->dwData, 10000);
				}
				record.set_by_game[i] = true;
			}
			return axes.empty() ? result : DI_OK;
		}

		HRESULT STDMETHODCALLTYPE get_capabilities(void* self, LPDIDEVCAPS caps)
		{
			const auto result = reinterpret_cast<get_capabilities_t>(original(self, slot_get_capabilities))(self, caps);
			if (FAILED(result) || !caps || !record_for(self).spoofed)
			{
				return result;
			}

			caps->dwAxes = caps->dwButtons = caps->dwPOVs = 0;
			for (const auto& object : active->objects)
			{
				if (DIDFT_GETTYPE(object.id) & DIDFT_AXIS) ++caps->dwAxes;
				else if (DIDFT_GETTYPE(object.id) & DIDFT_BUTTON) ++caps->dwButtons;
				else if (DIDFT_GETTYPE(object.id) & DIDFT_POV) ++caps->dwPOVs;
			}
			caps->dwFlags &= ~static_cast<DWORD>(DIDC_FORCEFEEDBACK);
			return result;
		}

		template <typename ObjectInstance>
		void describe(ObjectInstance& out, const pad_profile::object& object)
		{
			out = {};
			out.dwSize = sizeof(out);
			out.guidType = *object.type;
			out.dwOfs = object.offset;
			out.dwType = object.id;
			out.dwFlags = object.flags;
			out.wUsagePage = object.usage_page;
			out.wUsage = object.usage;
			if constexpr (std::is_same_v<ObjectInstance, DIDEVICEOBJECTINSTANCEW>)
			{
				std::swprintf(out.tszName, MAX_PATH, L"%hs", object.name);
			}
			else
			{
				std::snprintf(out.tszName, MAX_PATH, "%s", object.name);
			}
		}

		HRESULT STDMETHODCALLTYPE enum_objects(void* self, void* callback, LPVOID user, const DWORD flags)
		{
			const auto real = reinterpret_cast<enum_objects_t>(original(self, slot_enum_objects));
			if (!callback || !record_for(self).spoofed)
			{
				return real(self, callback, user, flags);
			}

			logger::write_once("enum-objects:" + std::to_string(flags), "dinput: game lists the device's objects (filter %08lX) -> presenting the %s layout",
			                   flags, active->description);

			// Presented objects have no attributes (force feedback, output, ...), so an attribute filter matches none.
			const DWORD types = DIDFT_GETTYPE(flags);
			constexpr DWORD attributes = DIDFT_FFACTUATOR | DIDFT_FFEFFECTTRIGGER | DIDFT_OUTPUT | DIDFT_VENDORDEFINED | DIDFT_ALIAS;
			if (flags & attributes)
			{
				return DI_OK;
			}

			const bool wide = is_wide(self);
			for (const auto& object : active->objects)
			{
				if (types != DIDFT_ALL && !(DIDFT_GETTYPE(object.id) & types))
				{
					continue;
				}

				BOOL more = DIENUM_CONTINUE;
				if (wide)
				{
					DIDEVICEOBJECTINSTANCEW instance;
					describe(instance, object);
					more = reinterpret_cast<LPDIENUMDEVICEOBJECTSCALLBACKW>(callback)(&instance, user);
				}
				else
				{
					DIDEVICEOBJECTINSTANCEA instance;
					describe(instance, object);
					more = reinterpret_cast<LPDIENUMDEVICEOBJECTSCALLBACKA>(callback)(&instance, user);
				}
				if (more == DIENUM_STOP)
				{
					break;
				}
			}
			return DI_OK;
		}

		HRESULT STDMETHODCALLTYPE get_device_state(void* self, const DWORD size, LPVOID data)
		{
			const auto result = reinterpret_cast<get_device_state_t>(original(self, slot_get_device_state))(self, size, data);
			if (!data || (size != sizeof(DIJOYSTATE) && size != sizeof(DIJOYSTATE2)))
			{
				return result;
			}

			auto& record = record_for(self);
			if (!record.spoofed)
			{
				return result;
			}
			if (FAILED(result))
			{
				// The pad's own DirectInput read can fail while its state comes from XInput anyway - e.g. in a
				// borderless/windowed game the device isn't acquired (in game 2026-09-27: the pad was never read
				// during play, XInput first loaded when the Options menu made a new DirectInput instance). Its
				// state is filled from XInput regardless.
				logger::write_once("pad-read-failed", "dinput: the pad's own read failed (%08lX) - its state comes from XInput anyway",
				                   static_cast<unsigned long>(result));
				std::memset(data, 0, size);
			}
			if (!record.axes_loaded)
			{
				load_axes(self, record);
			}

			const auto connected = xinput_pad::connected_indices();
			xinput_pad::raw_state pad{};
			if (record.ordinal < 0 || record.ordinal >= static_cast<int>(connected.size()) || !xinput_pad::read_raw(connected[record.ordinal], pad))
			{
				return result;
			}

			active->fill_state(*static_cast<DIJOYSTATE*>(data), pad, record.axes);
			return DI_OK;
		}

		struct interface_id
		{
			const IID* iid;
			bool wide;
		};

		constexpr std::array<interface_id, 8> device_interfaces{{
			{&IID_IDirectInputDevice8A, false}, {&IID_IDirectInputDevice8W, true},
			{&IID_IDirectInputDevice7A, false}, {&IID_IDirectInputDevice7W, true},
			{&IID_IDirectInputDevice2A, false}, {&IID_IDirectInputDevice2W, true},
			{&IID_IDirectInputDeviceA, false}, {&IID_IDirectInputDeviceW, true},
		}};

		// Hooks every interface of a new device, so the hooks apply whichever one the game
		// uses (or QueryInterfaces to later).
		void hook_device(void* device)
		{
			for (const auto& [iid, wide] : device_interfaces)
			{
				void* view = nullptr;
				if (FAILED(static_cast<IUnknown*>(device)->QueryInterface(*iid, &view)) || !view)
				{
					continue;
				}

				forget_device(view);
				patch_vtable(view, slot_get_device_info, reinterpret_cast<void*>(&get_device_info), wide);
				patch_vtable(view, slot_get_property, reinterpret_cast<void*>(&get_property), wide);
				patch_vtable(view, slot_get_device_state, reinterpret_cast<void*>(&get_device_state), wide);
				if (presents_objects())
				{
					patch_vtable(view, slot_get_capabilities, reinterpret_cast<void*>(&get_capabilities), wide);
					patch_vtable(view, slot_enum_objects, reinterpret_cast<void*>(&enum_objects), wide);
					patch_vtable(view, slot_set_property, reinterpret_cast<void*>(&set_property), wide);
				}
				static_cast<IUnknown*>(view)->Release();
			}
			forget_device(device);
		}

		HRESULT STDMETHODCALLTYPE create_device(void* self, REFGUID instance, void** device, LPUNKNOWN outer)
		{
			const auto result = reinterpret_cast<create_device_t>(original(self, slot_create_device))(self, instance, device, outer);
			if (SUCCEEDED(result) && device && *device)
			{
				hook_device(*device);
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE create_device_ex(void* self, REFGUID instance, REFIID iid, void** device, LPUNKNOWN outer)
		{
			const auto result = reinterpret_cast<create_device_ex_t>(original(self, slot_create_device_ex))(self, instance, iid, device, outer);
			if (SUCCEEDED(result) && device && *device)
			{
				hook_device(*device);
			}
			return result;
		}

		struct enum_context
		{
			LPDIENUMDEVICESCALLBACKW callback; // or the A version; only the instance layout differs
			LPVOID user;
		};

		BOOL CALLBACK enum_callback(LPCDIDEVICEINSTANCEW instance, LPVOID ref)
		{
			const auto* context = static_cast<enum_context*>(ref);
			if (!instance || instance->dwSize < offsetof(DIDEVICEINSTANCEW, guidProduct) + sizeof(GUID))
			{
				return context->callback(instance, context->user);
			}

			// Copy exactly what the caller was given (A or W, current or DirectX 3 size).
			alignas(DIDEVICEINSTANCEW) std::byte copy[sizeof(DIDEVICEINSTANCEW)]{};
			std::memcpy(copy, instance, std::min<size_t>(instance->dwSize, sizeof(copy)));

			logger::write_once(std::string("enum:") + std::to_string(instance->guidProduct.Data1),
			                   "dinput: game sees device %04X:%04X (type %08lX)", vid_of(instance->guidProduct), pid_of(instance->guidProduct), instance->dwDevType);

			spoof_instance(copy, "EnumDevices");
			return context->callback(reinterpret_cast<LPCDIDEVICEINSTANCEW>(copy), context->user);
		}

		HRESULT STDMETHODCALLTYPE enum_devices(void* self, DWORD type, LPDIENUMDEVICESCALLBACKW callback, LPVOID user, DWORD flags)
		{
			const auto real = reinterpret_cast<enum_devices_t>(original(self, slot_enum_devices));
			if (!callback)
			{
				return real(self, type, callback, user, flags);
			}

			enum_context context{callback, user};
			return real(self, type, &enum_callback, &context, flags);
		}

		struct direct_input_id
		{
			const IID* iid;
			bool wide;
			bool has_create_device_ex;
			int version;
		};

		constexpr std::array<direct_input_id, 8> direct_input_interfaces{{
			{&IID_IDirectInput8A, false, false, 8}, {&IID_IDirectInput8W, true, false, 8},
			{&IID_IDirectInput7A, false, true, 7}, {&IID_IDirectInput7W, true, true, 7},
			{&IID_IDirectInput2A, false, false, 5}, {&IID_IDirectInput2W, true, false, 5},
			{&IID_IDirectInputA, false, false, 3}, {&IID_IDirectInputW, true, false, 3},
		}};
	}

	void use_profile(const pad_profile::profile& profile)
	{
		active = &profile;
	}

	void hook_direct_input(void* direct_input)
	{
		int newest = 0;
		for (const auto& [iid, wide, has_create_device_ex, version] : direct_input_interfaces)
		{
			void* view = nullptr;
			if (FAILED(static_cast<IUnknown*>(direct_input)->QueryInterface(*iid, &view)) || !view)
			{
				continue;
			}

			newest = std::max(newest, version);
			patch_vtable(view, slot_create_device, reinterpret_cast<void*>(&create_device), wide);
			patch_vtable(view, slot_enum_devices, reinterpret_cast<void*>(&enum_devices), wide);
			if (has_create_device_ex)
			{
				patch_vtable(view, slot_create_device_ex, reinterpret_cast<void*>(&create_device_ex), wide);
			}
			static_cast<IUnknown*>(view)->Release();
		}
		logger::write("dinput: hooked a DirectInput %d instance", newest);
	}
}
