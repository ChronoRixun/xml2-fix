#pragma once

// The parts of Direct3D 8 the display fix touches. The Windows SDK no longer ships d3d8.h, so
// they are declared here: the public D3D8 structure layouts, constants and vtable slots.

#include <Windows.h>
#include <cstddef>

namespace d3d8
{
	constexpr UINT sdk_version = 220;

	// D3DFORMAT values the engine and the desktop use.
	constexpr DWORD format_a8r8g8b8 = 21;
	constexpr DWORD format_x8r8g8b8 = 22;
	constexpr DWORD format_r5g6b5 = 23;
	constexpr DWORD format_x1r5g5b5 = 24;
	constexpr DWORD format_a1r5g5b5 = 25;
	constexpr DWORD format_d24s8 = 75;

	// D3DSWAPEFFECT
	constexpr DWORD swap_discard = 1;
	constexpr DWORD swap_flip = 2;
	constexpr DWORD swap_copy = 3;
	constexpr DWORD swap_copy_vsync = 4;

	constexpr DWORD multisample_none = 0;
	constexpr UINT present_interval_default = 0;            // the only value a windowed device accepts
	constexpr UINT present_interval_one = 1;                // fullscreen: wait for one vertical blank (vsync on)
	constexpr UINT present_interval_immediate = 0x80000000; // fullscreen: never wait (vsync off)
	constexpr DWORD device_type_hal = 1;
	constexpr DWORD back_buffer_type_mono = 0; // D3DBACKBUFFER_TYPE_MONO
	constexpr DWORD lock_read_only = 0x10;     // D3DLOCK_READONLY
	constexpr DWORD clear_target = 1;          // D3DCLEAR_TARGET

	constexpr HRESULT err_device_lost = static_cast<HRESULT>(0x88760868);
	constexpr HRESULT err_device_not_reset = static_cast<HRESULT>(0x88760869);
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

	// D3DSURFACE_DESC (32 bytes) and D3DLOCKED_RECT.
	struct surface_desc
	{
		DWORD format;
		DWORD type;
		DWORD usage;
		DWORD pool;
		UINT size;
		DWORD multi_sample_type;
		UINT width;
		UINT height;
	};
	static_assert(sizeof(surface_desc) == 32);
	static_assert(offsetof(surface_desc,multi_sample_type)==0x14 && offsetof(surface_desc,width)==0x18 && offsetof(surface_desc,height)==0x1c);

	// D3DVIEWPORT8 and D3DRECT.
	struct viewport
	{
		DWORD x, y, width, height;
		float min_z, max_z;
	};
	static_assert(sizeof(viewport) == 24);
	struct d3d_rect
	{
		LONG x1, y1, x2, y2;
	};

	struct locked_rect
	{
		INT pitch;
		void* bits;
	};

	// D3DCAPS8: 53 DWORD-sized fields (floats occupy one field), x86 public ABI.
	struct caps8
	{
		DWORD device_type, adapter_ordinal, caps, caps2, caps3, presentation_intervals;
		DWORD cursor_caps, dev_caps, primitive_misc_caps, raster_caps;
		DWORD z_cmp_caps, src_blend_caps, dest_blend_caps, alpha_cmp_caps, shade_caps, texture_caps;
		DWORD texture_filter_caps, cube_texture_filter_caps, volume_texture_filter_caps;
		DWORD texture_address_caps, volume_texture_address_caps, line_caps;
		DWORD max_texture_width, max_texture_height, max_volume_extent, max_texture_repeat, max_texture_aspect_ratio, max_anisotropy;
		float max_vertex_w, guard_band_left, guard_band_top, guard_band_right, guard_band_bottom, extents_adjust;
		DWORD stencil_caps, fvf_caps, texture_op_caps, max_texture_blend_stages, max_simultaneous_textures;
		DWORD vertex_processing_caps, max_active_lights, max_user_clip_planes, max_vertex_blend_matrices, max_vertex_blend_matrix_index;
		float max_point_size;
		DWORD max_primitive_count, max_vertex_index, max_streams, max_stream_stride, vertex_shader_version, max_vertex_shader_const, pixel_shader_version;
		float max_pixel_shader_value;
	};
	static_assert(sizeof(caps8) == 212);
	static_assert(offsetof(caps8, raster_caps) == 0x24 && offsetof(caps8, texture_filter_caps) == 0x40);
	static_assert(offsetof(caps8, max_anisotropy) == 0x6c && offsetof(caps8, max_texture_blend_stages) == 0x94);
	static_assert(offsetof(caps8, max_simultaneous_textures) == 0x98);

	// IDirect3D8 vtable slots.
	namespace d3d_slot
	{
		constexpr int get_adapter_mode_count = 6;
		constexpr int enum_adapter_modes = 7;
		constexpr int get_adapter_display_mode = 8;
		constexpr int check_device_type = 9;
		constexpr int check_device_multi_sample_type = 11;
		constexpr int check_depth_stencil_match = 12;
		constexpr int get_device_caps = 13;
		constexpr int create_device = 15;
	}

	// IDirect3DDevice8 vtable slots.
	namespace device_slot
	{
		constexpr int test_cooperative_level = 3;
		constexpr int reset = 14;
		constexpr int present = 15;
		constexpr int get_back_buffer = 16;
		constexpr int create_image_surface = 27;
		constexpr int copy_rects = 28;
		constexpr int get_render_target = 32;
		constexpr int clear = 36;
		constexpr int set_viewport = 40;
		constexpr int get_viewport = 41;
	}

	// IDirect3DSurface8 vtable slots.
	namespace surface_slot
	{
		constexpr int get_desc = 8;
		constexpr int lock_rect = 9;
		constexpr int unlock_rect = 10;
	}

	using direct3d_create8_t = void*(WINAPI*)(UINT);
	using get_adapter_mode_count_t = UINT(STDMETHODCALLTYPE*)(void*, UINT);
	using enum_adapter_modes_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, display_mode*);
	using get_adapter_display_mode_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, display_mode*);
	using check_device_type_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, DWORD, DWORD, BOOL);
	using check_device_multi_sample_type_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, DWORD, BOOL, DWORD);
	using get_depth_stencil_surface_t = HRESULT(STDMETHODCALLTYPE*)(void*, void**);
	using check_depth_stencil_match_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, DWORD, DWORD, DWORD);
	using get_device_caps_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, caps8*);
	using create_device_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, HWND, DWORD, present_parameters*, void**);
	using test_cooperative_level_t = HRESULT(STDMETHODCALLTYPE*)(void*);
	using reset_t = HRESULT(STDMETHODCALLTYPE*)(void*, present_parameters*);
	using present_t = HRESULT(STDMETHODCALLTYPE*)(void*, const RECT*, const RECT*, HWND, const void*);
	using get_back_buffer_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, DWORD, void**);
	using create_image_surface_t = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, UINT, DWORD, void**);
	using copy_rects_t = HRESULT(STDMETHODCALLTYPE*)(void*, void*, const RECT*, UINT, void*, const POINT*);
	using clear_t = HRESULT(STDMETHODCALLTYPE*)(void*, DWORD, const void*, DWORD, DWORD, float, DWORD);
	using get_render_target_t = HRESULT(STDMETHODCALLTYPE*)(void*, void**);
	using viewport_t = HRESULT(STDMETHODCALLTYPE*)(void*, viewport*);
	using get_desc_t = HRESULT(STDMETHODCALLTYPE*)(void*, surface_desc*);
	using lock_rect_t = HRESULT(STDMETHODCALLTYPE*)(void*, locked_rect*, const RECT*, DWORD);
	using unlock_rect_t = HRESULT(STDMETHODCALLTYPE*)(void*);

	template <typename T>
	T method(void* object, const int slot)
	{
		return reinterpret_cast<T>((*static_cast<void***>(object))[slot]);
	}
}
