#pragma once

// The parts of Direct3D 8 the display fix touches. The Windows SDK no longer ships d3d8.h, so
// they are declared here: the public D3D8 structure layouts, constants and vtable slots.

#include <Windows.h>

namespace d3d8
{
	constexpr UINT sdk_version = 220;

	// D3DFORMAT values the engine and the desktop use.
	constexpr DWORD format_a8r8g8b8 = 21;
	constexpr DWORD format_x8r8g8b8 = 22;
	constexpr DWORD format_d24s8 = 75;

	// D3DSWAPEFFECT
	constexpr DWORD swap_discard = 1;
	constexpr DWORD swap_flip = 2;
	constexpr DWORD swap_copy = 3;
	constexpr DWORD swap_copy_vsync = 4;

	constexpr DWORD multisample_none = 0;
	constexpr UINT present_interval_default = 0;
	constexpr DWORD device_type_hal = 1;

	constexpr HRESULT err_device_lost = static_cast<HRESULT>(0x88760868);
	constexpr HRESULT err_invalid_call = static_cast<HRESULT>(0x8876086C);

	struct display_mode
	{
		UINT width;
		UINT height;
		UINT refresh_rate;
		DWORD format;
	};

	// D3DPRESENT_PARAMETERS as Direct3D 8 lays it out (52 bytes).
	struct present_parameters
	{
		UINT back_buffer_width;
		UINT back_buffer_height;
		DWORD back_buffer_format;
		UINT back_buffer_count;
		DWORD multi_sample_type;
		DWORD swap_effect;
		HWND device_window;
		BOOL windowed;
		BOOL enable_auto_depth_stencil;
		DWORD auto_depth_stencil_format;
		DWORD flags;
		UINT fullscreen_refresh_rate;
		UINT fullscreen_presentation_interval;
	};
	static_assert(sizeof(present_parameters) == 52);

	// IDirect3D8 vtable slots.
	namespace d3d_slot
	{
		constexpr int get_adapter_mode_count = 6;
		constexpr int enum_adapter_modes = 7;
		constexpr int get_adapter_display_mode = 8;
		constexpr int check_device_type = 9;
		constexpr int check_device_multi_sample_type = 11;
		constexpr int create_device = 15;
	}

	// IDirect3DDevice8 vtable slots.
	namespace device_slot
	{
		constexpr int test_cooperative_level = 3;
		constexpr int reset = 14;
		constexpr int present = 15;
	}

	using direct3d_create8_t = void*(WINAPI*)(UINT);
	using get_adapter_mode_count_t = UINT(STDMETHODCALLTYPE*)(void*, UINT);
	using enum_adapter_modes_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, display_mode*);
	using get_adapter_display_mode_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, display_mode*);
	using check_device_type_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, DWORD, DWORD, BOOL);
	using check_device_multi_sample_type_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, DWORD, BOOL, DWORD);
	using create_device_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, HWND, DWORD, present_parameters*, void**);
	using test_cooperative_level_t = HRESULT(STDMETHODCALLTYPE*)(void*);
	using reset_t = HRESULT(STDMETHODCALLTYPE*)(void*, present_parameters*);
	using present_t = HRESULT(STDMETHODCALLTYPE*)(void*, const RECT*, const RECT*, HWND, const void*);

	template <typename T>
	T method(void* object, const int slot)
	{
		return reinterpret_cast<T>((*static_cast<void***>(object))[slot]);
	}
}
