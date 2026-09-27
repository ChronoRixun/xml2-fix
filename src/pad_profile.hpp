#pragma once

// The controller a fixed game should see in place of an XInput pad: its USB ids and name,
// optionally its DirectInput objects, and how to build its DIJOYSTATE from XInput.

#include "xinput_pad.hpp"

#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>

#include <array>
#include <cstdint>
#include <span>

namespace pad_profile
{
	// DIJOYSTATE axes in order: X, Y, Z, Rx, Ry, Rz. With the standard joystick data
	// formats an axis' index is its offset / 4 and the instance number in its object id.
	constexpr int axis_count = 6;

	struct axis_range
	{
		LONG min = 0;
		LONG max = 65535;
		DWORD deadzone = 0; // 0..10000, as DIPROP_DEADZONE
	};

	using axis_ranges = std::array<axis_range, axis_count>;

	// A DirectInput object (axis, button or POV) as EnumObjects reports it.
	struct object
	{
		const GUID* type;
		DWORD offset; // in c_dfDIJoystick / c_dfDIJoystick2
		DWORD id;     // DIDFT_* type | DIDFT_MAKEINSTANCE(n)
		DWORD flags;  // DIDOI_*
		const char* name;
		WORD usage_page;
		WORD usage;
	};

	struct profile
	{
		const char* description; // for the log
		std::uint16_t vid;
		std::uint16_t pid;
		// Name reported for the device; nullptr keeps the real one.
		const char* product_name;
		// Microsoft pads the game already handles correctly; left untouched.
		std::span<const std::uint16_t> native_microsoft_pids;
		// When set, capabilities and EnumObjects report these instead of the real device's objects.
		std::span<const object> objects;
		void (*fill_state)(DIJOYSTATE& state, const xinput_pad::raw_state& pad, const axis_ranges& ranges);
	};

	// Logitech Dual Action (046D:C216): a pad X-Men Legends II knows by name, with separate
	// digital triggers and the right stick on Z / Rz.
	extern const profile logitech_dual_action;

	// value in [-1, 1] -> the axis range, applying the axis deadzone like DirectInput does.
	LONG to_axis(float value, const axis_range& axis);
}
