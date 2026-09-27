#include "pad_profile.hpp"

#include <Xinput.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pad_profile
{
	namespace
	{
		float stick(const std::int16_t raw)
		{
			return std::max(-1.0f, raw / 32767.0f);
		}

		DWORD dpad_pov(const std::uint16_t buttons)
		{
			const bool up = buttons & XINPUT_GAMEPAD_DPAD_UP, down = buttons & XINPUT_GAMEPAD_DPAD_DOWN;
			const bool left = buttons & XINPUT_GAMEPAD_DPAD_LEFT, right = buttons & XINPUT_GAMEPAD_DPAD_RIGHT;
			if (up && right) return 4500;
			if (right && down) return 13500;
			if (down && left) return 22500;
			if (left && up) return 31500;
			if (up) return 0;
			if (right) return 9000;
			if (down) return 18000;
			if (left) return 27000;
			return static_cast<DWORD>(-1);
		}

		void set_pov(DIJOYSTATE& state, const std::uint16_t buttons)
		{
			state.rgdwPOV[0] = dpad_pov(buttons);
			state.rgdwPOV[1] = state.rgdwPOV[2] = state.rgdwPOV[3] = static_cast<DWORD>(-1);
		}

		// Exactly as a Logitech Dual Action reports: buttons 1-4 are the face buttons clockwise
		// from the left (X, A, B, Y on an Xbox pad), 5-8 are LB, RB, LT, RT, then Back, Start,
		// LS, RS; the right stick is Z / Rz.
		void fill_dual_action(DIJOYSTATE& state, const xinput_pad::raw_state& pad, const axis_ranges& axes)
		{
			state.lX = to_axis(stick(pad.left_x), axes[0]);
			state.lY = to_axis(-stick(pad.left_y), axes[1]);
			state.lZ = to_axis(stick(pad.right_x), axes[2]);
			state.lRx = state.lRy = 0; // not present on the device
			state.lRz = to_axis(-stick(pad.right_y), axes[5]);
			state.rglSlider[0] = state.rglSlider[1] = 0;
			set_pov(state, pad.buttons);

			const bool pressed[12]{
				(pad.buttons & XINPUT_GAMEPAD_X) != 0,
				(pad.buttons & XINPUT_GAMEPAD_A) != 0,
				(pad.buttons & XINPUT_GAMEPAD_B) != 0,
				(pad.buttons & XINPUT_GAMEPAD_Y) != 0,
				(pad.buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0,
				(pad.buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0,
				pad.left_trigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD,
				pad.right_trigger > XINPUT_GAMEPAD_TRIGGER_THRESHOLD,
				(pad.buttons & XINPUT_GAMEPAD_BACK) != 0,
				(pad.buttons & XINPUT_GAMEPAD_START) != 0,
				(pad.buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0,
				(pad.buttons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0,
			};
			std::memset(state.rgbButtons, 0, sizeof(state.rgbButtons));
			for (size_t i = 0; i < std::size(pressed); ++i)
			{
				state.rgbButtons[i] = pressed[i] ? 0x80 : 0;
			}
		}

		constexpr DWORD axis_id(const int instance) { return DIDFT_ABSAXIS | DIDFT_MAKEINSTANCE(instance); }
		constexpr DWORD button_id(const int instance) { return DIDFT_PSHBUTTON | DIDFT_MAKEINSTANCE(instance); }

		const std::array<object, 17> dual_action_objects{{
			{&GUID_XAxis, DIJOFS_X, axis_id(0), DIDOI_ASPECTPOSITION, "X Axis", 0x01, 0x30},
			{&GUID_YAxis, DIJOFS_Y, axis_id(1), DIDOI_ASPECTPOSITION, "Y Axis", 0x01, 0x31},
			{&GUID_ZAxis, DIJOFS_Z, axis_id(2), DIDOI_ASPECTPOSITION, "Z Axis", 0x01, 0x32},
			{&GUID_RzAxis, DIJOFS_RZ, axis_id(5), DIDOI_ASPECTPOSITION, "Z Rotation", 0x01, 0x35},
			{&GUID_POV, DIJOFS_POV(0), DIDFT_POV | DIDFT_MAKEINSTANCE(0), 0, "Hat Switch", 0x01, 0x39},
			{&GUID_Button, DIJOFS_BUTTON(0), button_id(0), 0, "Button 1", 0x09, 1},
			{&GUID_Button, DIJOFS_BUTTON(1), button_id(1), 0, "Button 2", 0x09, 2},
			{&GUID_Button, DIJOFS_BUTTON(2), button_id(2), 0, "Button 3", 0x09, 3},
			{&GUID_Button, DIJOFS_BUTTON(3), button_id(3), 0, "Button 4", 0x09, 4},
			{&GUID_Button, DIJOFS_BUTTON(4), button_id(4), 0, "Button 5", 0x09, 5},
			{&GUID_Button, DIJOFS_BUTTON(5), button_id(5), 0, "Button 6", 0x09, 6},
			{&GUID_Button, DIJOFS_BUTTON(6), button_id(6), 0, "Button 7", 0x09, 7},
			{&GUID_Button, DIJOFS_BUTTON(7), button_id(7), 0, "Button 8", 0x09, 8},
			{&GUID_Button, DIJOFS_BUTTON(8), button_id(8), 0, "Button 9", 0x09, 9},
			{&GUID_Button, DIJOFS_BUTTON(9), button_id(9), 0, "Button 10", 0x09, 10},
			{&GUID_Button, DIJOFS_BUTTON(10), button_id(10), 0, "Button 11", 0x09, 11},
			{&GUID_Button, DIJOFS_BUTTON(11), button_id(11), 0, "Button 12", 0x09, 12},
		}};
	}

	const profile logitech_dual_action{
		.description = "Logitech Dual Action",
		.vid = 0x046D,
		.pid = 0xC216,
		.product_name = "Logitech Dual Action",
		.native_microsoft_pids = {},
		.objects = dual_action_objects,
		.fill_state = &fill_dual_action,
	};

	LONG to_axis(float value, const axis_range& axis)
	{
		const float deadzone = axis.deadzone / 10000.0f;
		const float magnitude = std::fabs(value);
		value = magnitude <= deadzone ? 0.0f : std::copysign((magnitude - deadzone) / (1.0f - deadzone), value);
		value = std::clamp(value, -1.0f, 1.0f);

		const double center = (static_cast<double>(axis.min) + axis.max) / 2.0;
		const double half = (static_cast<double>(axis.max) - axis.min) / 2.0;
		return static_cast<LONG>(std::lround(center + value * half));
	}
}
