#pragma once

// [Test] VirtualPads: pads the game sees with nothing plugged in, for tests of controller play
// and local co-op driven through the test pipe (pad_input_rules.hpp). The decisions, kept apart
// from the COM objects (virtual_pad.cpp) so xml2_test can check them: the ini value, which
// DirectInput enumerations list the pads and which real devices they replace, the pads' GUIDs
// and device types, and how a pad's state reaches whatever data format the caller set.
//
// Both of the game's DirectInput paths list them: the engine's DirectInput 7 (libIGDisplay.dll,
// EnumDevices(DIDEVTYPE_JOYSTICK)) and XMen2.exe's own DirectInput 8 (0x628e20:
// EnumDevices(DI8DEVCLASS_GAMECTRL, ATTACHEDONLY), then per pad 0x628b40: CreateDevice,
// SetDataFormat(c_dfDIJoystick2), SetCooperativeLevel, EnumObjects(DIDFT_AXIS) with a
// -1000..1000 range by id each; every frame 0x6285c0: Poll, Acquire when Poll fails,
// GetDeviceState(0x110)). The game keeps up to 10 pads in slots by instance GUID, pins
// Controls\Gamepads\Gamepad1-4's saved GUIDs to slots 0-3 at the settings load (0x61b030) and
// then fills the free slots in enumeration order (0x628e20(1)); a thread re-enumerates every
// few seconds for pads plugged in or out. A binding's device 3+n is slot n, so virtual pad N,
// always listed and always N-th, is player N's pad.

#include "limits_rules.hpp" // value_text
#include "pad_profile.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace virtual_pad_rules
{
	constexpr int max_pads = 4;

	// dinput.h has these only for DIRECTINPUT_VERSION <= 0x0700 (DIDFT_OPTIONAL not at all any more).
	constexpr DWORD di7_type_joystick = 4;         // DIDEVTYPE_JOYSTICK
	constexpr DWORD di7_subtype_gamepad = 4;       // DIDEVTYPEJOYSTICK_GAMEPAD
	constexpr DWORD optional_object = 0x80000000;  // DIDFT_OPTIONAL: a data format object the device may lack

	// ---- [Test] VirtualPads -----------------------------------------------------------------------------

	struct pads_choice
	{
		int count = 0;
		std::string error; // why the ini's value was refused or cut down
	};

	// `value`: [Test] VirtualPads as the ini has it, "" when absent. 0 to max_pads; more is cut to max_pads.
	inline pads_choice choose_count(const std::string_view value)
	{
		pads_choice chosen;
		const auto text = limits_rules::value_text(value);
		if (text.empty())
		{
			return chosen;
		}
		const auto parsed = limits_rules::parse_count(text);
		if (!parsed)
		{
			chosen.error = "VirtualPads=" + std::string(text) + " isn't a number of pads (0 to " + std::to_string(max_pads) + ") - no virtual pads";
			return chosen;
		}
		chosen.count = *parsed;
		if (chosen.count > max_pads)
		{
			chosen.error = "VirtualPads=" + std::string(text) + " is more than the game's " + std::to_string(max_pads) + " players - " + std::to_string(max_pads) + " virtual pads";
			chosen.count = max_pads;
		}
		return chosen;
	}

	// ---- Identity -----------------------------------------------------------------------------------------

	// {56504144-5832-4658-0000-00000000000N}: "VPAD", "X2", "FX", then the pad's number.
	inline GUID instance_guid(const int index)
	{
		return {0x56504144, 0x5832, 0x4658, {0, 0, 0, 0, 0, 0, 0, static_cast<unsigned char>(index + 1)}};
	}

	// The pad (0-based) a GUID is the instance of, if it is a virtual pad's.
	inline std::optional<int> pad_of(const GUID& guid, const int count)
	{
		for (int i = 0; i < count; ++i)
		{
			if (IsEqualGUID(guid, instance_guid(i)))
			{
				return i;
			}
		}
		return std::nullopt;
	}

	// A HID device's product GUID as DirectInput builds it: {PIDVID-0000-0000-0000-504944564944}.
	inline GUID product_guid(const std::uint16_t vid, const std::uint16_t pid)
	{
		return {(static_cast<DWORD>(pid) << 16) | vid, 0, 0, {0, 0, 'P', 'I', 'D', 'V', 'I', 'D'}};
	}

	// dwDevType as a HID gamepad reports it to a DirectInput 8 or an older one.
	inline DWORD device_type(const bool di8)
	{
		return di8 ? (DI8DEVTYPE_GAMEPAD | (DI8DEVTYPEGAMEPAD_STANDARD << 8) | DIDEVTYPE_HID)
		           : (di7_type_joystick | (di7_subtype_gamepad << 8) | DIDEVTYPE_HID);
	}

	// Whether a device of `dev_type` is a game controller - one the virtual pads replace.
	inline bool is_game_controller(const bool di8, const DWORD dev_type)
	{
		const DWORD type = GET_DIDEVICE_TYPE(dev_type);
		return di8 ? (type >= DI8DEVTYPE_JOYSTICK && type <= DI8DEVTYPE_1STPERSON) : type == di7_type_joystick;
	}

	// Whether EnumDevices(`type`, `flags`) lists the virtual pads: every device, the game controller
	// class, or the gamepad type (and its standard subtype); none when only force feedback devices
	// are wanted.
	inline bool lists_pads(const bool di8, const DWORD type, const DWORD flags)
	{
		if (flags & DIEDFL_FORCEFEEDBACK)
		{
			return false;
		}
		const DWORD ours = device_type(di8);
		const DWORD main_type = GET_DIDEVICE_TYPE(type);
		const DWORD sub_type = GET_DIDEVICE_SUBTYPE(type);
		if (type == 0)
		{
			return true;
		}
		if (di8 && type == DI8DEVCLASS_GAMECTRL)
		{
			return true;
		}
		return main_type == GET_DIDEVICE_TYPE(ours) && (sub_type == 0 || sub_type == GET_DIDEVICE_SUBTYPE(ours));
	}

	// ---- Data formats -----------------------------------------------------------------------------------

	// How the pad's objects land in the caller's data format, as DirectInput maps them: each of the
	// format's objects, in order, takes the first object not taken yet whose kind (axis, button,
	// POV), type GUID (none: any) and instance (DIDFT_ANYINSTANCE: any) match; an optional one
	// nothing matches stays empty (0, a POV centred), a required one fails the format.
	struct field
	{
		DWORD from;   // the object's offset in the DIJOYSTATE2 the profile fills (pad_profile::object::offset)
		DWORD to;     // in the caller's data
		DWORD size;   // 4 (axis, POV) or 1 (button)
		int object;   // index in the profile's objects
	};

	struct data_layout
	{
		DWORD size = 0;
		std::vector<field> fields;
		std::vector<DWORD> empty_povs; // offsets of POVs nothing maps to: reported centred (-1)
		std::string error;             // set: the format was refused
	};

	inline DWORD object_kind(const DWORD type) { return DIDFT_GETTYPE(type) & (DIDFT_AXIS | DIDFT_BUTTON | DIDFT_POV); }

	inline data_layout map_format(const DIDATAFORMAT& format, const std::span<const pad_profile::object> objects)
	{
		data_layout layout;
		if (format.dwSize != sizeof(DIDATAFORMAT) || format.dwObjSize != sizeof(DIOBJECTDATAFORMAT) || (format.dwNumObjs && !format.rgodf) ||
		    format.dwDataSize % 4 != 0)
		{
			layout.error = "not a DIDATAFORMAT (sizes, or a data size that isn't a multiple of 4)";
			return layout;
		}
		layout.size = format.dwDataSize;
		std::vector<bool> taken(objects.size());
		for (DWORD n = 0; n < format.dwNumObjs; ++n)
		{
			const auto& want = format.rgodf[n];
			const DWORD kind = object_kind(want.dwType);
			const DWORD instance = DIDFT_GETINSTANCE(want.dwType);
			const bool any_instance = (want.dwType & DIDFT_ANYINSTANCE) == DIDFT_ANYINSTANCE;
			const DWORD size = (kind & DIDFT_BUTTON) ? 1 : 4;
			if (want.dwOfs + size > format.dwDataSize)
			{
				layout.error = "object " + std::to_string(n) + " lies outside the data";
				return layout;
			}
			int match = -1;
			for (std::size_t i = 0; i < objects.size() && match < 0; ++i)
			{
				const auto& object = objects[i];
				if (taken[i] || !(kind & object_kind(object.id)))
				{
					continue;
				}
				if (want.pguid && !IsEqualGUID(*want.pguid, *object.type))
				{
					continue;
				}
				if (!any_instance && DIDFT_GETINSTANCE(object.id) != instance)
				{
					continue;
				}
				match = static_cast<int>(i);
			}
			if (match >= 0)
			{
				taken[match] = true;
				const auto& object = objects[match];
				layout.fields.push_back({object.offset, want.dwOfs, (object_kind(object.id) & DIDFT_BUTTON) ? 1u : 4u, match});
			}
			else if (!(want.dwType & optional_object))
			{
				layout.error = "object " + std::to_string(n) + " is required and the pad has nothing like it";
				return layout;
			}
			else if (kind == DIDFT_POV)
			{
				layout.empty_povs.push_back(want.dwOfs);
			}
		}
		return layout;
	}

	// `state`: a DIJOYSTATE2 as the profile fills it; `out`: layout.size bytes.
	inline void translate(const data_layout& layout, const BYTE* state, BYTE* out)
	{
		std::memset(out, 0, layout.size);
		for (const DWORD pov : layout.empty_povs)
		{
			std::memset(out + pov, 0xFF, sizeof(DWORD));
		}
		for (const auto& f : layout.fields)
		{
			std::memcpy(out + f.to, state + f.from, f.size);
		}
	}

	// The object a DIPROPHEADER / GetObjectInfo (dwObj, dwHow) addresses, or -1. By offset: in the
	// caller's data format when one is set, else the standard joystick formats' offsets.
	inline int find_object(const std::span<const pad_profile::object> objects, const data_layout* layout, const DWORD obj, const DWORD how)
	{
		switch (how)
		{
		case DIPH_BYOFFSET:
			if (layout)
			{
				for (const auto& f : layout->fields)
				{
					if (f.to == obj)
					{
						return f.object;
					}
				}
				return -1;
			}
			for (std::size_t i = 0; i < objects.size(); ++i)
			{
				if (objects[i].offset == obj)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		case DIPH_BYID:
			for (std::size_t i = 0; i < objects.size(); ++i)
			{
				if ((object_kind(obj) & object_kind(objects[i].id)) && DIDFT_GETINSTANCE(obj) == DIDFT_GETINSTANCE(objects[i].id))
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		case DIPH_BYUSAGE:
			for (std::size_t i = 0; i < objects.size(); ++i)
			{
				if (HIWORD(obj) == objects[i].usage_page && LOWORD(obj) == objects[i].usage)
				{
					return static_cast<int>(i);
				}
			}
			return -1;
		default:
			return -1;
		}
	}

	// The DIJOYSTATE axis (0-5: X, Y, Z, Rx, Ry, Rz) an object is, or -1 for a button or POV.
	inline int axis_of(const pad_profile::object& object)
	{
		if (!(object_kind(object.id) & DIDFT_AXIS) || object.offset > DIJOFS_RZ)
		{
			return -1;
		}
		return static_cast<int>(object.offset / sizeof(LONG));
	}

	// The axis (0-5) a range or deadzone header addresses (DIPH_BYOFFSET or DIPH_BYID), or -1. Like a
	// real pad presented as a Dual Action (gamepad_fix.cpp), the six DIJOYSTATE axes answer by their
	// standard offset or instance whether the pad has them or not: the engine sets a range on all six
	// (libIGDisplay.dll, by offset) and a refusal for Rx and Ry is nothing it has ever seen.
	inline int addressed_axis(const std::span<const pad_profile::object> objects, const data_layout* layout, const DWORD obj, const DWORD how)
	{
		if (const int i = find_object(objects, layout, obj, how); i >= 0)
		{
			return axis_of(objects[i]);
		}
		if (how == DIPH_BYOFFSET && obj <= DIJOFS_RZ && obj % sizeof(LONG) == 0)
		{
			return static_cast<int>(obj / sizeof(LONG));
		}
		if (how == DIPH_BYID && (DIDFT_GETTYPE(obj) & DIDFT_AXIS) && DIDFT_GETINSTANCE(obj) <= 5)
		{
			return static_cast<int>(DIDFT_GETINSTANCE(obj));
		}
		return -1;
	}
}
