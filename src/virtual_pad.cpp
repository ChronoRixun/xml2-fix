#include "virtual_pad.hpp"
#include "gamepad_fix.hpp"
#include "log.hpp"
#include "pad_input.hpp"
#include "pad_profile.hpp"
#include "virtual_pad_rules.hpp"
#include "xinput_pad.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace virtual_pad
{
	namespace
	{
		using namespace virtual_pad_rules;

		int pads = 0;

		const pad_profile::profile& profile() { return gamepad_fix::profile(); }

		// ---- The device -------------------------------------------------------------------------------
		//
		// One object, two interface pointers: an A view and a W view, each a vtable pointer. Every
		// IDirectInputDevice version extends the one before (8 = 7 + BuildActionMap, SetActionMap,
		// GetImageInfo; 7 = 2 + EnumEffectsInFile, WriteEffectToFile; 2 = 1 + force feedback, Poll,
		// SendDeviceData), so one 32-slot vtable per character set answers them all.

		struct device;

		struct view
		{
			const void* const* vtable;
			device* owner;
		};

		struct device
		{
			view a{};
			view w{};
			std::atomic<ULONG> refs{1};
			int index = 0;
			bool di8 = false;

			std::mutex mutex; // the rest; never held while calling out
			bool acquired = false;
			bool has_format = false;
			data_layout layout;
			pad_profile::axis_ranges axes{}; // DirectInput's own defaults: 0..65535, no deadzone
			DWORD buffer_size = 0;
			HANDLE event = nullptr;
			std::atomic<bool> read_logged{false};
		};

		device& owner(void* self) { return *static_cast<view*>(self)->owner; }

		template <bool Wide>
		using instance_t = std::conditional_t<Wide, DIDEVICEINSTANCEW, DIDEVICEINSTANCEA>;
		template <bool Wide>
		using instance_dx3_t = std::conditional_t<Wide, DIDEVICEINSTANCE_DX3W, DIDEVICEINSTANCE_DX3A>;
		template <bool Wide>
		using object_instance_t = std::conditional_t<Wide, DIDEVICEOBJECTINSTANCEW, DIDEVICEOBJECTINSTANCEA>;
		template <bool Wide>
		using object_instance_dx3_t = std::conditional_t<Wide, DIDEVICEOBJECTINSTANCE_DX3W, DIDEVICEOBJECTINSTANCE_DX3A>;

		template <bool Wide>
		void fill_instance(instance_t<Wide>& out, const int index, const bool di8)
		{
			const DWORD size = out.dwSize;
			out = {};
			out.dwSize = size;
			out.guidInstance = instance_guid(index);
			out.guidProduct = product_guid(profile().vid, profile().pid);
			out.dwDevType = device_type(di8);
			if constexpr (Wide)
			{
				std::swprintf(out.tszInstanceName, MAX_PATH, L"%hs", profile().product_name ? profile().product_name : profile().description);
				std::swprintf(out.tszProductName, MAX_PATH, L"%hs", profile().product_name ? profile().product_name : profile().description);
			}
			else
			{
				std::snprintf(out.tszInstanceName, MAX_PATH, "%s", profile().product_name ? profile().product_name : profile().description);
				std::snprintf(out.tszProductName, MAX_PATH, "%s", profile().product_name ? profile().product_name : profile().description);
			}
			out.guidFFDriver = GUID_NULL;
			out.wUsagePage = 0x01; // generic desktop
			out.wUsage = 0x05;     // game pad
		}

		// Where object `i` lands in the caller's data format (its standard offset before one is set).
		DWORD offset_of(const device& d, const int i)
		{
			if (d.has_format)
			{
				for (const auto& f : d.layout.fields)
				{
					if (f.object == i)
					{
						return f.to;
					}
				}
			}
			return profile().objects[i].offset;
		}

		// ---- The real pad behind a virtual one -----------------------------------------------------------

		bool game_has_focus()
		{
			DWORD process = 0;
			if (const HWND foreground = GetForegroundWindow())
			{
				GetWindowThreadProcessId(foreground, &process);
			}
			return process == GetCurrentProcessId();
		}

		// XInput pad `index` (the index-th connected one), while the game has the focus: a test game in
		// the background never takes a pad someone plays another game with.
		void real_input(const int index, xinput_pad::raw_state& out)
		{
			out = {};
			if (!game_has_focus())
			{
				return;
			}
			static std::mutex connected_mutex;
			static std::vector<int> connected;
			static ULONGLONG next_check = 0;
			int xinput_index = -1;
			{
				std::lock_guard lock(connected_mutex);
				const ULONGLONG now = GetTickCount64();
				if (now >= next_check)
				{
					connected = xinput_pad::connected_indices(); // four XInputGetState calls: once a second, not per read
					next_check = now + 1000;
				}
				if (index < static_cast<int>(connected.size()))
				{
					xinput_index = connected[index];
				}
			}
			if (xinput_index >= 0 && !xinput_pad::read_raw(xinput_index, out))
			{
				out = {};
			}
		}

		// ---- IUnknown ---------------------------------------------------------------------------------------

		bool is_one_of(REFIID riid, std::initializer_list<const IID*> ids)
		{
			for (const IID* id : ids)
			{
				if (IsEqualIID(riid, *id))
				{
					return true;
				}
			}
			return false;
		}

		HRESULT STDMETHODCALLTYPE query_interface(void* self, REFIID riid, void** out)
		{
			if (!out)
			{
				return E_POINTER;
			}
			auto& d = owner(self);
			const bool a = is_one_of(riid, {&IID_IUnknown, &IID_IDirectInputDeviceA, &IID_IDirectInputDevice2A, &IID_IDirectInputDevice7A, &IID_IDirectInputDevice8A});
			const bool w = is_one_of(riid, {&IID_IDirectInputDeviceW, &IID_IDirectInputDevice2W, &IID_IDirectInputDevice7W, &IID_IDirectInputDevice8W});
			if (!a && !w)
			{
				*out = nullptr;
				return E_NOINTERFACE;
			}
			*out = w ? &d.w : &d.a;
			++d.refs;
			return S_OK;
		}

		ULONG STDMETHODCALLTYPE add_ref(void* self)
		{
			return ++owner(self).refs;
		}

		ULONG STDMETHODCALLTYPE release(void* self)
		{
			auto* d = &owner(self);
			const ULONG left = --d->refs;
			if (left == 0)
			{
				if (d->di8)
				{
					pad_input::game_device(d->index, false);
				}
				delete d;
			}
			return left;
		}

		// ---- IDirectInputDevice -------------------------------------------------------------------------------

		HRESULT STDMETHODCALLTYPE get_capabilities(void* self, LPDIDEVCAPS caps)
		{
			if (!caps)
			{
				return E_POINTER;
			}
			const DWORD size = caps->dwSize;
			if (size != sizeof(DIDEVCAPS) && size != sizeof(DIDEVCAPS_DX3))
			{
				return DIERR_INVALIDPARAM;
			}
			DIDEVCAPS full{};
			full.dwSize = size;
			full.dwFlags = DIDC_ATTACHED;
			full.dwDevType = device_type(owner(self).di8);
			for (const auto& object : profile().objects)
			{
				if (object_kind(object.id) & DIDFT_AXIS) ++full.dwAxes;
				else if (object_kind(object.id) & DIDFT_BUTTON) ++full.dwButtons;
				else if (object_kind(object.id) & DIDFT_POV) ++full.dwPOVs;
			}
			std::memcpy(caps, &full, size);
			return DI_OK;
		}

		template <bool Wide>
		HRESULT STDMETHODCALLTYPE enum_objects(void* self, void* callback, LPVOID user, const DWORD flags)
		{
			if (!callback)
			{
				return DIERR_INVALIDPARAM;
			}
			// The objects have no attributes (force feedback, output, ...), so an attribute filter matches none.
			constexpr DWORD attributes = DIDFT_FFACTUATOR | DIDFT_FFEFFECTTRIGGER | DIDFT_OUTPUT | DIDFT_VENDORDEFINED | DIDFT_ALIAS;
			if (flags & attributes)
			{
				return DI_OK;
			}
			const DWORD types = DIDFT_GETTYPE(flags);
			const auto objects = profile().objects;

			std::vector<DWORD> offsets(objects.size());
			{
				auto& d = owner(self);
				std::lock_guard lock(d.mutex);
				for (size_t i = 0; i < objects.size(); ++i)
				{
					offsets[i] = offset_of(d, static_cast<int>(i));
				}
			}
			for (size_t i = 0; i < objects.size(); ++i)
			{
				if (types != DIDFT_ALL && !(DIDFT_GETTYPE(objects[i].id) & types))
				{
					continue;
				}
				object_instance_t<Wide> instance;
				pad_profile::describe(instance, objects[i], offsets[i]);
				using callback_t = std::conditional_t<Wide, LPDIENUMDEVICEOBJECTSCALLBACKW, LPDIENUMDEVICEOBJECTSCALLBACKA>;
				if (reinterpret_cast<callback_t>(callback)(&instance, user) == DIENUM_STOP)
				{
					break;
				}
			}
			return DI_OK;
		}

		// The property's number: DIPROP_* are MAKEDIPROP(n), a GUID reference at address n.
		std::uintptr_t property_id(REFGUID property) { return reinterpret_cast<std::uintptr_t>(&property); }
		template <typename T>
		std::uintptr_t id_of(const T& property) { return reinterpret_cast<std::uintptr_t>(&property); }

		// The axes (0-5) a range or deadzone header addresses, or empty.
		std::vector<int> addressed_axes(const device& d, const DIPROPHEADER& header, const bool device_allowed)
		{
			if (header.dwHow == DIPH_DEVICE)
			{
				if (!device_allowed || header.dwObj != 0)
				{
					return {};
				}
				return {0, 1, 2, 3, 4, 5};
			}
			const int axis = addressed_axis(profile().objects, d.has_format ? &d.layout : nullptr, header.dwObj, header.dwHow);
			return axis >= 0 ? std::vector<int>{axis} : std::vector<int>{};
		}

		HRESULT STDMETHODCALLTYPE get_property(void* self, REFGUID property, LPDIPROPHEADER header)
		{
			if (!header)
			{
				return E_POINTER;
			}
			if (header->dwHeaderSize != sizeof(DIPROPHEADER))
			{
				return DIERR_INVALIDPARAM;
			}
			auto& d = owner(self);
			const auto id = property_id(property);
			const auto dword_value = [&](const DWORD value) -> HRESULT
			{
				if (header->dwSize < sizeof(DIPROPDWORD))
				{
					return DIERR_INVALIDPARAM;
				}
				reinterpret_cast<LPDIPROPDWORD>(header)->dwData = value;
				return DI_OK;
			};

			std::lock_guard lock(d.mutex);
			if (id == id_of(DIPROP_RANGE) || id == id_of(DIPROP_DEADZONE) || id == id_of(DIPROP_SATURATION))
			{
				const auto axes = addressed_axes(d, *header, false);
				if (axes.size() != 1)
				{
					return header->dwHow == DIPH_DEVICE ? DIERR_INVALIDPARAM : DIERR_OBJECTNOTFOUND;
				}
				const auto& axis = d.axes[axes[0]];
				if (id == id_of(DIPROP_RANGE))
				{
					if (header->dwSize < sizeof(DIPROPRANGE))
					{
						return DIERR_INVALIDPARAM;
					}
					reinterpret_cast<LPDIPROPRANGE>(header)->lMin = axis.min;
					reinterpret_cast<LPDIPROPRANGE>(header)->lMax = axis.max;
					return DI_OK;
				}
				return dword_value(id == id_of(DIPROP_DEADZONE) ? axis.deadzone : 10000);
			}
			if (id == id_of(DIPROP_VIDPID))
			{
				return dword_value((static_cast<DWORD>(profile().pid) << 16) | profile().vid);
			}
			if (id == id_of(DIPROP_PRODUCTNAME) || id == id_of(DIPROP_INSTANCENAME))
			{
				if (header->dwSize < sizeof(DIPROPSTRING))
				{
					return DIERR_INVALIDPARAM;
				}
				std::swprintf(reinterpret_cast<LPDIPROPSTRING>(header)->wsz, MAX_PATH, L"%hs", profile().product_name ? profile().product_name : profile().description);
				return DI_OK;
			}
			if (id == id_of(DIPROP_JOYSTICKID)) return dword_value(static_cast<DWORD>(d.index));
			if (id == id_of(DIPROP_BUFFERSIZE)) return dword_value(d.buffer_size);
			if (id == id_of(DIPROP_AXISMODE)) return dword_value(DIPROPAXISMODE_ABS);
			if (id == id_of(DIPROP_GRANULARITY)) return dword_value(1);
			if (id == id_of(DIPROP_AUTOCENTER)) return dword_value(DIPROPAUTOCENTER_OFF);
			if (id == id_of(DIPROP_CALIBRATIONMODE)) return dword_value(DIPROPCALIBRATIONMODE_COOKED);
			if (id == id_of(DIPROP_FFGAIN)) return dword_value(10000);
			return DIERR_UNSUPPORTED;
		}

		HRESULT STDMETHODCALLTYPE set_property(void* self, REFGUID property, LPCDIPROPHEADER header)
		{
			if (!header)
			{
				return E_POINTER;
			}
			if (header->dwHeaderSize != sizeof(DIPROPHEADER))
			{
				return DIERR_INVALIDPARAM;
			}
			auto& d = owner(self);
			const auto id = property_id(property);
			std::lock_guard lock(d.mutex);
			if (id == id_of(DIPROP_RANGE) || id == id_of(DIPROP_DEADZONE) || id == id_of(DIPROP_SATURATION))
			{
				const auto axes = addressed_axes(d, *header, true);
				if (axes.empty())
				{
					return DIERR_OBJECTNOTFOUND;
				}
				if (id == id_of(DIPROP_RANGE))
				{
					const auto* range = reinterpret_cast<LPCDIPROPRANGE>(header);
					if (header->dwSize < sizeof(DIPROPRANGE) || range->lMin >= range->lMax)
					{
						return DIERR_INVALIDPARAM;
					}
					for (const int axis : axes)
					{
						d.axes[axis].min = range->lMin;
						d.axes[axis].max = range->lMax;
					}
					return DI_OK;
				}
				const auto* value = reinterpret_cast<LPCDIPROPDWORD>(header);
				if (header->dwSize < sizeof(DIPROPDWORD) || value->dwData > 10000)
				{
					return DIERR_INVALIDPARAM;
				}
				if (id == id_of(DIPROP_DEADZONE))
				{
					for (const int axis : axes)
					{
						d.axes[axis].deadzone = value->dwData;
					}
				}
				return DI_OK; // saturation: the pad's full range is its full travel anyway
			}
			if (id == id_of(DIPROP_BUFFERSIZE))
			{
				if (header->dwSize < sizeof(DIPROPDWORD))
				{
					return DIERR_INVALIDPARAM;
				}
				d.buffer_size = reinterpret_cast<LPCDIPROPDWORD>(header)->dwData;
				return DI_OK;
			}
			if (id == id_of(DIPROP_AXISMODE) || id == id_of(DIPROP_AUTOCENTER) || id == id_of(DIPROP_CALIBRATIONMODE) || id == id_of(DIPROP_FFGAIN))
			{
				return DI_OK;
			}
			return DIERR_UNSUPPORTED;
		}

		// Nothing to take from anyone: acquiring always works, and a read works acquired or not (the
		// game's per-frame read acquires only when Poll fails; a real pad's foreground/exclusive
		// acquire failing on the fix's own windows was what stopped real pads in the borderless mode).
		HRESULT STDMETHODCALLTYPE acquire(void* self)
		{
			auto& d = owner(self);
			std::lock_guard lock(d.mutex);
			const bool was = d.acquired;
			d.acquired = true;
			return was ? S_FALSE : DI_OK;
		}

		HRESULT STDMETHODCALLTYPE unacquire(void* self)
		{
			auto& d = owner(self);
			std::lock_guard lock(d.mutex);
			const bool was = d.acquired;
			d.acquired = false;
			return was ? DI_OK : DI_NOEFFECT;
		}

		HRESULT STDMETHODCALLTYPE get_device_state(void* self, const DWORD size, LPVOID data)
		{
			if (!data)
			{
				return E_POINTER;
			}
			auto& d = owner(self);
			xinput_pad::raw_state pad{};
			real_input(d.index, pad);
			pad_input::on_read(d.index, pad, d.di8);

			std::lock_guard lock(d.mutex);
			if (!d.has_format)
			{
				return DIERR_NOTINITIALIZED;
			}
			if (size != d.layout.size)
			{
				return DIERR_INVALIDPARAM;
			}
			DIJOYSTATE2 state{};
			profile().fill_state(*reinterpret_cast<DIJOYSTATE*>(&state), pad, d.axes);
			translate(d.layout, reinterpret_cast<const BYTE*>(&state), static_cast<BYTE*>(data));
			if (!d.read_logged.exchange(true))
			{
				logger::write_once("virtual-read:" + std::to_string(d.index) + (d.di8 ? ":8" : ":7"), "virtual pads: the game reads virtual pad %d through DirectInput %d",
				                   d.index + 1, d.di8 ? 8 : 7);
			}
			return DI_OK;
		}

		HRESULT STDMETHODCALLTYPE get_device_data(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD in_out, DWORD)
		{
			if (!in_out)
			{
				return E_POINTER;
			}
			logger::write_once("virtual-buffered", "virtual pads: the game asks for buffered data - virtual pads report none (state reads only)");
			*in_out = 0;
			return DI_OK;
		}

		HRESULT STDMETHODCALLTYPE set_data_format(void* self, LPCDIDATAFORMAT format)
		{
			if (!format)
			{
				return E_POINTER;
			}
			auto layout = map_format(*format, profile().objects);
			auto& d = owner(self);
			if (!layout.error.empty())
			{
				logger::write("virtual pads: virtual pad %d refuses a data format: %s", d.index + 1, layout.error.c_str());
				return DIERR_INVALIDPARAM;
			}
			logger::write_once("virtual-format:" + std::to_string(format->dwDataSize) + ":" + std::to_string(format->dwNumObjs),
			                   "virtual pads: a %lu-byte data format of %lu objects, %zu of them the pad's", format->dwDataSize, format->dwNumObjs, layout.fields.size());
			std::lock_guard lock(d.mutex);
			d.layout = std::move(layout);
			d.has_format = true;
			return DI_OK;
		}

		HRESULT STDMETHODCALLTYPE set_event_notification(void* self, HANDLE event)
		{
			auto& d = owner(self);
			std::lock_guard lock(d.mutex);
			d.event = event; // never set: the pads are read, not waited for
			return DI_OK;
		}

		HRESULT STDMETHODCALLTYPE set_cooperative_level(void*, HWND, DWORD)
		{
			return DI_OK;
		}

		template <bool Wide>
		HRESULT STDMETHODCALLTYPE get_object_info(void* self, object_instance_t<Wide>* out, const DWORD obj, const DWORD how)
		{
			if (!out)
			{
				return E_POINTER;
			}
			const DWORD size = out->dwSize;
			if (size != sizeof(object_instance_t<Wide>) && size != sizeof(object_instance_dx3_t<Wide>))
			{
				return DIERR_INVALIDPARAM;
			}
			auto& d = owner(self);
			int i = -1;
			DWORD offset = 0;
			{
				std::lock_guard lock(d.mutex);
				i = find_object(profile().objects, d.has_format ? &d.layout : nullptr, obj, how);
				if (i >= 0)
				{
					offset = offset_of(d, i);
				}
			}
			if (i < 0)
			{
				return DIERR_OBJECTNOTFOUND;
			}
			object_instance_t<Wide> full;
			pad_profile::describe(full, profile().objects[i], offset);
			full.dwSize = size;
			std::memcpy(out, &full, size);
			return DI_OK;
		}

		template <bool Wide>
		HRESULT STDMETHODCALLTYPE get_device_info(void* self, instance_t<Wide>* out)
		{
			if (!out)
			{
				return E_POINTER;
			}
			const DWORD size = out->dwSize;
			if (size != sizeof(instance_t<Wide>) && size != sizeof(instance_dx3_t<Wide>))
			{
				return DIERR_INVALIDPARAM;
			}
			instance_t<Wide> full{};
			full.dwSize = size;
			fill_instance<Wide>(full, owner(self).index, owner(self).di8);
			std::memcpy(out, &full, size);
			return DI_OK;
		}

		HRESULT STDMETHODCALLTYPE run_control_panel(void*, HWND, DWORD) { return DI_OK; }
		HRESULT STDMETHODCALLTYPE initialize(void*, HINSTANCE, DWORD, REFGUID) { return DI_OK; }

		// ---- IDirectInputDevice2: no force feedback ---------------------------------------------------------

		HRESULT STDMETHODCALLTYPE create_effect(void*, REFGUID, LPCDIEFFECT, void** effect, LPUNKNOWN)
		{
			if (effect)
			{
				*effect = nullptr;
			}
			return DIERR_UNSUPPORTED;
		}
		HRESULT STDMETHODCALLTYPE enum_effects(void*, void*, LPVOID, DWORD) { return DI_OK; }
		HRESULT STDMETHODCALLTYPE get_effect_info(void*, void*, REFGUID) { return DIERR_DEVICENOTREG; }
		HRESULT STDMETHODCALLTYPE get_force_feedback_state(void*, LPDWORD) { return DIERR_UNSUPPORTED; }
		HRESULT STDMETHODCALLTYPE send_force_feedback_command(void*, DWORD) { return DIERR_UNSUPPORTED; }
		HRESULT STDMETHODCALLTYPE enum_created_effect_objects(void*, void*, LPVOID, DWORD) { return DI_OK; }
		HRESULT STDMETHODCALLTYPE escape(void*, LPDIEFFESCAPE) { return DIERR_UNSUPPORTED; }
		HRESULT STDMETHODCALLTYPE poll(void*) { return DI_OK; }
		HRESULT STDMETHODCALLTYPE send_device_data(void*, DWORD, LPCDIDEVICEOBJECTDATA, LPDWORD, DWORD) { return DIERR_UNSUPPORTED; }

		// ---- IDirectInputDevice7 and 8: effect files, action maps ---------------------------------------------

		HRESULT STDMETHODCALLTYPE enum_effects_in_file(void*, const void*, void*, LPVOID, DWORD) { return DIERR_UNSUPPORTED; }
		HRESULT STDMETHODCALLTYPE write_effect_to_file(void*, const void*, DWORD, LPDIFILEEFFECT, DWORD) { return DIERR_UNSUPPORTED; }
		HRESULT STDMETHODCALLTYPE build_action_map(void*, void*, const void*, DWORD) { return DIERR_UNSUPPORTED; }
		HRESULT STDMETHODCALLTYPE set_action_map(void*, void*, const void*, DWORD) { return DIERR_UNSUPPORTED; }
		HRESULT STDMETHODCALLTYPE get_image_info(void*, void*) { return DIERR_UNSUPPORTED; }

		template <typename F>
		const void* slot(F function)
		{
			return reinterpret_cast<const void*>(function);
		}

		// IDirectInputDevice8's 32 slots, in order.
		template <bool Wide>
		const void* const* vtable()
		{
			static const void* const table[32] = {
				slot(&query_interface), slot(&add_ref), slot(&release),
				// IDirectInputDevice
				slot(&get_capabilities), slot(&enum_objects<Wide>), slot(&get_property), slot(&set_property), slot(&acquire), slot(&unacquire),
				slot(&get_device_state), slot(&get_device_data), slot(&set_data_format), slot(&set_event_notification), slot(&set_cooperative_level),
				slot(&get_object_info<Wide>), slot(&get_device_info<Wide>), slot(&run_control_panel), slot(&initialize),
				// IDirectInputDevice2
				slot(&create_effect), slot(&enum_effects), slot(&get_effect_info), slot(&get_force_feedback_state), slot(&send_force_feedback_command),
				slot(&enum_created_effect_objects), slot(&escape), slot(&poll), slot(&send_device_data),
				// IDirectInputDevice7
				slot(&enum_effects_in_file), slot(&write_effect_to_file),
				// IDirectInputDevice8
				slot(&build_action_map), slot(&set_action_map), slot(&get_image_info),
			};
			return table;
		}
	}

	void install()
	{
		const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
		wchar_t value[64]{};
		GetPrivateProfileStringW(L"Test", L"VirtualPads", L"", value, static_cast<DWORD>(std::size(value)), ini.c_str());
		std::string text;
		for (const wchar_t c : std::wstring(value))
		{
			text += c < 0x80 ? static_cast<char>(c) : '?';
		}
		const auto chosen = choose_count(text);
		if (!chosen.error.empty())
		{
			logger::write("virtual pads: [Test] %s", chosen.error.c_str());
		}
		pads = chosen.count;
		if (pads > 0)
		{
			logger::write("virtual pads: the game sees %d %s pad%s ([Test] VirtualPads) in place of any real controller - virtual pad N follows XInput pad N "
			              "while the game has the focus, and the test pipe's pad commands",
			              pads, profile().description, pads == 1 ? "" : "s");
		}
	}

	int count()
	{
		return pads;
	}

	bool is_virtual(REFGUID instance)
	{
		return pad_of(instance, pads).has_value();
	}

	bool enumerate(const bool di8, const bool wide, void* callback, void* user)
	{
		for (int i = 0; i < pads; ++i)
		{
			BOOL more = DIENUM_CONTINUE;
			if (wide)
			{
				DIDEVICEINSTANCEW instance{};
				instance.dwSize = sizeof(instance);
				fill_instance<true>(instance, i, di8);
				more = reinterpret_cast<LPDIENUMDEVICESCALLBACKW>(callback)(&instance, user);
			}
			else
			{
				DIDEVICEINSTANCEA instance{};
				instance.dwSize = sizeof(instance);
				fill_instance<false>(instance, i, di8);
				more = reinterpret_cast<LPDIENUMDEVICESCALLBACKA>(callback)(&instance, user);
			}
			logger::write_once(std::string("virtual-enum:") + (di8 ? "8" : "7"), "virtual pads: DirectInput %d lists the game %d virtual pad%s", di8 ? 8 : 7, pads,
			                   pads == 1 ? "" : "s");
			if (more == DIENUM_STOP)
			{
				return false;
			}
		}
		return true;
	}

	HRESULT create(REFGUID instance, const IID* iid, const bool di8, const bool wide, void** out)
	{
		if (!out)
		{
			return E_POINTER;
		}
		*out = nullptr;
		const auto index = pad_of(instance, pads);
		if (!index)
		{
			return DIERR_DEVICENOTREG;
		}

		auto* d = new device;
		d->a = {vtable<false>(), d};
		d->w = {vtable<true>(), d};
		d->index = *index;
		d->di8 = di8;
		if (di8)
		{
			pad_input::game_device(d->index, true);
		}

		HRESULT result = S_OK;
		if (iid)
		{
			result = query_interface(&d->a, *iid, out); // takes a reference of its own
			release(&d->a);
		}
		else
		{
			*out = wide ? &d->w : &d->a;
		}
		logger::write("virtual pads: DirectInput %d creates virtual pad %d%s", di8 ? 8 : 7, *index + 1, SUCCEEDED(result) ? "" : " - for an interface it doesn't have");
		return SUCCEEDED(result) ? DI_OK : result;
	}
}
