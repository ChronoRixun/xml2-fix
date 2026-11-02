#pragma once
// p060fix/AQ2: a CPU-only stand-in for the IDirect3DDevice8 / IDirect3DSurface8 calls hold_overlay.cpp makes (the
// native fixture has no real device): GetBackBuffer, CreateImageSurface, CopyRects, GetRenderTarget, Clear,
// Get/SetViewport; surfaces: Release, GetDesc, LockRect, UnlockRect. Pixels are real memory, so a test can read
// what the overlay drew. Not thread-safe; one device at a time.
#include "../src/d3d8_min.hpp"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fake_d3d8
{
	struct surface
	{
		void** vt;
		LONG refs = 1;
		d3d8::surface_desc desc{};
		std::vector<std::uint8_t> bits;
		unsigned bpp() const { return desc.format == d3d8::format_x8r8g8b8 || desc.format == d3d8::format_a8r8g8b8 ? 4u : 2u; }
		unsigned pitch() const { return desc.width * bpp(); }
		std::uint32_t pixel(unsigned x, unsigned y) const
		{
			const auto* p = bits.data() + std::size_t(y) * pitch() + std::size_t(x) * bpp();
			if (bpp() == 4) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }
			std::uint16_t v; std::memcpy(&v, p, 2); return v;
		}
		void fill(std::uint32_t v)
		{
			for (unsigned y = 0; y < desc.height; ++y)
				for (unsigned x = 0; x < desc.width; ++x)
					std::memcpy(bits.data() + std::size_t(y) * pitch() + std::size_t(x) * bpp(), &v, bpp());
		}
	};
	struct device
	{
		void** vt;
		surface* back = nullptr;
		d3d8::viewport view{0, 0, 0, 0, 0.0f, 1.0f};
		unsigned copies = 0, clears = 0, image_surfaces = 0, live_surfaces = 0;
		HRESULT fail_copy = S_OK; // a refused CopyRects (the clear path then)
		bool render_target_is_back = true;
		surface other_target{};
	};
	inline device* current = nullptr;

	inline ULONG STDMETHODCALLTYPE surface_add_ref(surface* s) { return ULONG(++s->refs); }
	inline ULONG STDMETHODCALLTYPE surface_release(surface* s)
	{
		const LONG left = --s->refs;
		if (left == 0 && current && s != current->back && s != &current->other_target) { --current->live_surfaces; delete s; }
		return ULONG(left < 0 ? 0 : left);
	}
	inline HRESULT STDMETHODCALLTYPE surface_get_desc(surface* s, d3d8::surface_desc* d) { *d = s->desc; return S_OK; }
	inline HRESULT STDMETHODCALLTYPE surface_lock(surface* s, d3d8::locked_rect* l, const RECT*, DWORD) { l->bits = s->bits.data(); l->pitch = INT(s->pitch()); return S_OK; }
	inline HRESULT STDMETHODCALLTYPE surface_unlock(surface*) { return S_OK; }
	inline void* surface_vt[16] = {
		nullptr, reinterpret_cast<void*>(&surface_add_ref), reinterpret_cast<void*>(&surface_release), nullptr, nullptr, nullptr, nullptr, nullptr,
		reinterpret_cast<void*>(&surface_get_desc), reinterpret_cast<void*>(&surface_lock), reinterpret_cast<void*>(&surface_unlock), nullptr, nullptr, nullptr, nullptr, nullptr};

	inline surface* make(unsigned w, unsigned h, DWORD format, DWORD multisample = d3d8::multisample_none)
	{
		auto* s = new surface{surface_vt};
		s->desc.format = format; s->desc.width = w; s->desc.height = h; s->desc.multi_sample_type = multisample;
		s->bits.assign(std::size_t(w) * h * s->bpp(), 0);
		return s;
	}

	inline HRESULT STDMETHODCALLTYPE get_back_buffer(device* d, UINT, DWORD, void** out) { ++d->back->refs; *out = d->back; return S_OK; }
	inline HRESULT STDMETHODCALLTYPE create_image_surface(device* d, UINT w, UINT h, DWORD format, void** out)
	{
		*out = make(w, h, format); ++d->image_surfaces; ++d->live_surfaces; return S_OK;
	}
	inline HRESULT STDMETHODCALLTYPE copy_rects(device* d, surface* from, const RECT* rects, UINT count, surface* to, const POINT* points)
	{
		++d->copies;
		if (FAILED(d->fail_copy)) return d->fail_copy;
		if (from->desc.format != to->desc.format || from->desc.multi_sample_type || to->desc.multi_sample_type) return d3d8::err_invalid_call;
		const RECT all{0, 0, LONG(from->desc.width), LONG(from->desc.height)};
		const RECT r = count ? rects[0] : all;
		const POINT p = points ? points[0] : POINT{r.left, r.top};
		for (LONG y = r.top; y < r.bottom; ++y)
			std::memcpy(to->bits.data() + std::size_t(p.y + (y - r.top)) * to->pitch() + std::size_t(p.x) * to->bpp(),
			            from->bits.data() + std::size_t(y) * from->pitch() + std::size_t(r.left) * from->bpp(), std::size_t(r.right - r.left) * from->bpp());
		return S_OK;
	}
	inline HRESULT STDMETHODCALLTYPE get_render_target(device* d, void** out)
	{
		surface* t = d->render_target_is_back ? d->back : &d->other_target;
		++t->refs; *out = t; return S_OK;
	}
	inline HRESULT STDMETHODCALLTYPE clear(device* d, DWORD count, const d3d8::d3d_rect* rects, DWORD, DWORD colour, float, DWORD)
	{
		++d->clears;
		surface* t = d->back;
		for (DWORD i = 0; i < count; ++i)
		{
			const auto& r = rects[i];
			for (LONG y = std::max<LONG>(r.y1, LONG(d->view.y)); y < std::min<LONG>(r.y2, LONG(d->view.y + d->view.height)); ++y)
				for (LONG x = std::max<LONG>(r.x1, LONG(d->view.x)); x < std::min<LONG>(r.x2, LONG(d->view.x + d->view.width)); ++x)
				{
					auto* px = t->bits.data() + std::size_t(y) * t->pitch() + std::size_t(x) * t->bpp();
					if (t->bpp() == 4) std::memcpy(px, &colour, 4);
					else { const auto v = std::uint16_t(((colour >> 19) & 0x1f) << 11 | ((colour >> 10) & 0x3f) << 5 | ((colour >> 3) & 0x1f)); std::memcpy(px, &v, 2); }
				}
		}
		return S_OK;
	}
	inline HRESULT STDMETHODCALLTYPE set_viewport(device* d, d3d8::viewport* v) { d->view = *v; return S_OK; }
	inline HRESULT STDMETHODCALLTYPE get_viewport(device* d, d3d8::viewport* v) { *v = d->view; return S_OK; }

	inline void* device_vt[64]{};
	inline device* make_device(unsigned w, unsigned h, DWORD format, DWORD multisample = d3d8::multisample_none)
	{
		device_vt[d3d8::device_slot::get_back_buffer] = reinterpret_cast<void*>(&get_back_buffer);
		device_vt[d3d8::device_slot::create_image_surface] = reinterpret_cast<void*>(&create_image_surface);
		device_vt[d3d8::device_slot::copy_rects] = reinterpret_cast<void*>(&copy_rects);
		device_vt[d3d8::device_slot::get_render_target] = reinterpret_cast<void*>(&get_render_target);
		device_vt[d3d8::device_slot::clear] = reinterpret_cast<void*>(&clear);
		device_vt[d3d8::device_slot::set_viewport] = reinterpret_cast<void*>(&set_viewport);
		device_vt[d3d8::device_slot::get_viewport] = reinterpret_cast<void*>(&get_viewport);
		auto* d = new device{device_vt};
		d->back = make(w, h, format, multisample);
		d->other_target.vt = surface_vt;
		d->view = {0, 0, w / 2, h / 2, 0.0f, 1.0f}; // a sub-viewport, as an engine may leave it: the clear path must widen and restore it
		current = d;
		return d;
	}
}
