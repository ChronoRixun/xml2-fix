#include "hold_overlay.hpp"
#include "d3d8_min.hpp"

#include <Unknwn.h> // IUnknown, which WIN32_LEAN_AND_MEAN leaves out

#include <algorithm>
#include <cstring>

namespace hold_overlay
{
	namespace
	{
		// Colours: the panel (dark blue, ~80% opaque), its border (blue), the text (white).
		constexpr std::uint32_t panel_rgb = 0x101830, border_rgb = 0x5a8cd8, text_rgb = 0xffffff;
		constexpr unsigned panel_alpha = 205;

		struct releaser
		{
			void* object;
			~releaser() { if (object) static_cast<IUnknown*>(object)->Release(); }
		};

		// The cached panel (one text at one size) and the system-memory surface the blend path uses.
		panel cached;
		std::string cached_text;
		unsigned cached_bb_w = 0, cached_bb_h = 0;
		void* scratch = nullptr; // IDirect3DSurface8 in system memory
		void* scratch_device = nullptr;
		DWORD scratch_format = 0;
		unsigned scratch_w = 0, scratch_h = 0;

		bool blendable(DWORD format)
		{
			return format == d3d8::format_x8r8g8b8 || format == d3d8::format_a8r8g8b8 || format == d3d8::format_r5g6b5 ||
			       format == d3d8::format_x1r5g5b5 || format == d3d8::format_a1r5g5b5;
		}
		const panel& panel_for(const char* text, unsigned w, unsigned h)
		{
			if (cached_text != text || cached_bb_w != w || cached_bb_h != h)
			{
				cached = render(text, w, h);
				cached_text = text;
				cached_bb_w = w;
				cached_bb_h = h;
			}
			return cached;
		}
		std::uint32_t clear_colour(std::uint32_t rgb) { return 0xff000000u | rgb; }

		outcome draw_blend(void* device, void* back, const d3d8::surface_desc& desc, const panel& p, const RECT& r)
		{
			outcome o;
			o.where = r;
			const unsigned w = unsigned(r.right - r.left), h = unsigned(r.bottom - r.top);
			if (!scratch || scratch_device != device || scratch_format != desc.format || scratch_w != w || scratch_h != h)
			{
				release();
				const HRESULT made = d3d8::method<d3d8::create_image_surface_t>(device, d3d8::device_slot::create_image_surface)(device, w, h, desc.format, &scratch);
				if (FAILED(made) || !scratch) { scratch = nullptr; o.result = made; o.step = "CreateImageSurface"; return o; }
				scratch_device = device; scratch_format = desc.format; scratch_w = w; scratch_h = h;
			}
			const auto copy = d3d8::method<d3d8::copy_rects_t>(device, d3d8::device_slot::copy_rects);
			const POINT origin{0, 0};
			HRESULT hr = copy(device, back, &r, 1, scratch, &origin);
			if (FAILED(hr)) { o.result = hr; o.step = "CopyRects (back buffer to system memory)"; return o; }
			d3d8::locked_rect lock{};
			hr = d3d8::method<d3d8::lock_rect_t>(scratch, d3d8::surface_slot::lock_rect)(scratch, &lock, nullptr, 0);
			if (FAILED(hr) || !lock.bits) { o.result = hr; o.step = "LockRect"; return o; }
			for (unsigned y = 0; y < h; ++y)
			{
				auto* row = static_cast<std::uint8_t*>(lock.bits) + std::ptrdiff_t(y) * lock.pitch;
				const std::uint32_t* src = p.argb.data() + std::size_t(y) * p.width;
				if (desc.format == d3d8::format_x8r8g8b8 || desc.format == d3d8::format_a8r8g8b8)
				{
					auto* px = reinterpret_cast<std::uint32_t*>(row);
					for (unsigned x = 0; x < w; ++x) px[x] = blend(px[x], src[x]);
				}
				else
				{
					auto* px = reinterpret_cast<std::uint16_t*>(row);
					for (unsigned x = 0; x < w; ++x) px[x] = to16(blend(from16(px[x], desc.format), src[x]), desc.format);
				}
			}
			d3d8::method<d3d8::unlock_rect_t>(scratch, d3d8::surface_slot::unlock_rect)(scratch);
			const RECT all{0, 0, LONG(w), LONG(h)};
			const POINT at{r.left, r.top};
			hr = copy(device, scratch, &all, 1, back, &at);
			if (FAILED(hr)) { o.result = hr; o.step = "CopyRects (system memory to back buffer)"; return o; }
			o.drawn = path::blend;
			return o;
		}

		outcome draw_clear(void* device, void* back, const d3d8::surface_desc& desc, const panel& p, const RECT& r)
		{
			outcome o;
			o.where = r;
			void* target = nullptr;
			HRESULT hr = d3d8::method<d3d8::get_render_target_t>(device, d3d8::device_slot::get_render_target)(device, &target);
			if (FAILED(hr) || !target) { o.result = hr; o.step = "GetRenderTarget"; return o; }
			releaser release_target{target};
			if (target != back) { o.result = d3d8::err_invalid_call; o.step = "the render target is not the back buffer"; return o; }
			d3d8::viewport saved{};
			hr = d3d8::method<d3d8::viewport_t>(device, d3d8::device_slot::get_viewport)(device, &saved);
			if (FAILED(hr)) { o.result = hr; o.step = "GetViewport"; return o; }
			d3d8::viewport full{0, 0, desc.width, desc.height, 0.0f, 1.0f};
			hr = d3d8::method<d3d8::viewport_t>(device, d3d8::device_slot::set_viewport)(device, &full);
			if (FAILED(hr)) { o.result = hr; o.step = "SetViewport"; return o; }
			const auto clear = d3d8::method<d3d8::clear_t>(device, d3d8::device_slot::clear);
			const LONG b = LONG(p.border);
			const d3d8::d3d_rect outer{r.left, r.top, r.right, r.bottom};
			const d3d8::d3d_rect inner{r.left + b, r.top + b, r.right - b, r.bottom - b};
			hr = clear(device, 1, &outer, d3d8::clear_target, clear_colour(border_rgb), 1.0f, 0);
			if (SUCCEEDED(hr)) hr = clear(device, 1, &inner, d3d8::clear_target, clear_colour(panel_rgb), 1.0f, 0);
			// The text as horizontal runs of covered pixels, cleared in batches.
			std::vector<d3d8::d3d_rect> runs;
			const unsigned w = unsigned(r.right - r.left), h = unsigned(r.bottom - r.top);
			for (unsigned y = 0; y < h && SUCCEEDED(hr); ++y)
			{
				const std::uint8_t* m = p.text_mask.data() + std::size_t(y) * p.width;
				for (unsigned x = 0; x < w;)
				{
					if (!m[x]) { ++x; continue; }
					unsigned e = x;
					while (e < w && m[e]) ++e;
					runs.push_back({r.left + LONG(x), r.top + LONG(y), r.left + LONG(e), r.top + LONG(y) + 1});
					x = e;
				}
			}
			for (std::size_t i = 0; i < runs.size() && SUCCEEDED(hr); i += 256)
				hr = clear(device, DWORD(std::min<std::size_t>(256, runs.size() - i)), runs.data() + i, d3d8::clear_target, clear_colour(text_rgb), 1.0f, 0);
			d3d8::method<d3d8::viewport_t>(device, d3d8::device_slot::set_viewport)(device, &saved);
			if (FAILED(hr)) { o.result = hr; o.step = "Clear"; return o; }
			o.drawn = path::clear;
			return o;
		}
	}

	std::uint32_t blend(std::uint32_t dst, std::uint32_t src)
	{
		const unsigned a = src >> 24, ia = 255 - a;
		auto channel = [&](int shift) {
			const unsigned s = (src >> shift) & 0xff, d = (dst >> shift) & 0xff;
			return ((s * a + d * ia + 127) / 255) << shift;
		};
		return 0xff000000u | channel(16) | channel(8) | channel(0);
	}
	std::uint32_t from16(std::uint16_t v, DWORD format)
	{
		unsigned r, g, b;
		if (format == d3d8::format_r5g6b5) { r = (v >> 11) & 0x1f; g = (v >> 5) & 0x3f; b = v & 0x1f; g = (g * 255 + 31) / 63; }
		else { r = (v >> 10) & 0x1f; g = (v >> 5) & 0x1f; b = v & 0x1f; g = (g * 255 + 15) / 31; }
		r = (r * 255 + 15) / 31; b = (b * 255 + 15) / 31;
		return 0xff000000u | (r << 16) | (g << 8) | b;
	}
	std::uint16_t to16(std::uint32_t c, DWORD format)
	{
		const unsigned r = (c >> 16) & 0xff, g = (c >> 8) & 0xff, b = c & 0xff;
		if (format == d3d8::format_r5g6b5) return std::uint16_t(((r * 31 + 127) / 255) << 11 | ((g * 63 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
		const std::uint16_t top = format == d3d8::format_a1r5g5b5 ? 0x8000 : 0;
		return std::uint16_t(top | ((r * 31 + 127) / 255) << 10 | ((g * 31 + 127) / 255) << 5 | ((b * 31 + 127) / 255));
	}

	panel render(const char* text, unsigned bb_w, unsigned bb_h)
	{
		panel p;
		if (!text || !*text || bb_w < 32 || bb_h < 32) return p;
		const int font_px = std::max(14, int(bb_h / 24));       // 720 lines: 30 px
		const int pad_x = font_px, pad_y = font_px / 2;
		p.border = std::max(1u, unsigned(font_px) / 14);        // 2 px at 720
		HDC dc = CreateCompatibleDC(nullptr);
		if (!dc) return p;
		HFONT font = CreateFontW(-font_px, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
		                         ANTIALIASED_QUALITY, VARIABLE_PITCH | FF_SWISS, L"Arial");
		HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
		SIZE extent{};
		const int length = int(std::strlen(text));
		GetTextExtentPoint32A(dc, text, length, &extent);
		const unsigned w = std::min<unsigned>(bb_w, unsigned(extent.cx + 2 * pad_x)), h = std::min<unsigned>(bb_h, unsigned(std::max<LONG>(extent.cy, font_px) + 2 * pad_y));
		BITMAPINFO bi{};
		bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
		bi.bmiHeader.biWidth = LONG(w);
		bi.bmiHeader.biHeight = -LONG(h); // top-down
		bi.bmiHeader.biPlanes = 1;
		bi.bmiHeader.biBitCount = 32;
		bi.bmiHeader.biCompression = BI_RGB;
		void* bits = nullptr;
		HBITMAP dib = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (dib && bits && w && h)
		{
			HGDIOBJ old_bitmap = SelectObject(dc, dib);
			std::memset(bits, 0, std::size_t(w) * h * 4);
			SetBkMode(dc, TRANSPARENT);
			SetTextColor(dc, RGB(255, 255, 255));
			RECT box{0, 0, LONG(w), LONG(h)};
			DrawTextA(dc, text, length, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
			GdiFlush();
			p.width = w; p.height = h;
			p.argb.resize(std::size_t(w) * h);
			p.text_mask.resize(std::size_t(w) * h);
			const auto* in = static_cast<const std::uint32_t*>(bits);
			for (unsigned y = 0; y < h; ++y)
				for (unsigned x = 0; x < w; ++x)
				{
					const std::size_t i = std::size_t(y) * w + x;
					const bool edge = x < p.border || y < p.border || x >= w - p.border || y >= h - p.border;
					const std::uint32_t base = edge ? (0xff000000u | border_rgb) : ((std::uint32_t(panel_alpha) << 24) | panel_rgb);
					// Grey antialiasing: every channel carries the coverage; take the brightest.
					const std::uint32_t g = in[i];
					const unsigned coverage = std::max({(g >> 16) & 0xffu, (g >> 8) & 0xffu, g & 0xffu});
					std::uint32_t out = base;
					if (!edge && coverage)
					{
						// text over the panel: white with the coverage, composited onto the panel colour
						const unsigned ba = base >> 24;
						const unsigned a = coverage + ba * (255 - coverage) / 255;
						auto mix = [&](int shift) {
							const unsigned t = (text_rgb >> shift) & 0xff, b = (base >> shift) & 0xff;
							const unsigned num = t * coverage + b * ba * (255 - coverage) / 255;
							return (a ? (num + a / 2) / a : 0) << shift;
						};
						out = (a << 24) | mix(16) | mix(8) | mix(0);
					}
					p.argb[i] = out;
					p.text_mask[i] = !edge && coverage >= 128;
				}
			SelectObject(dc, old_bitmap);
		}
		if (dib) DeleteObject(dib);
		if (old_font) SelectObject(dc, old_font);
		if (font) DeleteObject(font);
		DeleteDC(dc);
		return p;
	}

	RECT place(const panel& p, unsigned bb_w, unsigned bb_h)
	{
		const LONG w = LONG(std::min(p.width, bb_w)), h = LONG(std::min(p.height, bb_h));
		const LONG left = (LONG(bb_w) - w) / 2;
		LONG top = LONG(bb_h) * 3 / 10 - h / 2;
		top = std::clamp<LONG>(top, 0, LONG(bb_h) - h);
		return RECT{left, top, left + w, top + h};
	}

	outcome draw(void* device, const char* text, bool force_clear)
	{
		outcome o;
		if (!device || !text || !*text) return o;
		void* back = nullptr;
		HRESULT hr = d3d8::method<d3d8::get_back_buffer_t>(device, d3d8::device_slot::get_back_buffer)(device, 0, d3d8::back_buffer_type_mono, &back);
		if (FAILED(hr) || !back) { o.result = hr; o.step = "GetBackBuffer"; return o; }
		releaser release_back{back};
		d3d8::surface_desc desc{};
		hr = d3d8::method<d3d8::get_desc_t>(back, d3d8::surface_slot::get_desc)(back, &desc);
		if (FAILED(hr)) { o.result = hr; o.step = "GetDesc"; return o; }
		const panel& p = panel_for(text, desc.width, desc.height);
		if (!p.width || !p.height) { o.result = E_FAIL; o.step = "the text panel (GDI)"; return o; }
		const RECT r = place(p, desc.width, desc.height);
		if (!force_clear && desc.multi_sample_type == d3d8::multisample_none && blendable(desc.format))
		{
			o = draw_blend(device, back, desc, p, r);
			if (o.drawn != path::none) return o;
		}
		const outcome first = o;
		o = draw_clear(device, back, desc, p, r);
		if (o.drawn == path::none && first.step) return first; // report the blend failure, the more useful one
		return o;
	}

	void release()
	{
		if (scratch) static_cast<IUnknown*>(scratch)->Release();
		scratch = nullptr;
		scratch_device = nullptr;
		scratch_w = scratch_h = 0;
		scratch_format = 0;
	}
}
