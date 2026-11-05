#pragma once
// p060fix/AQ2: the join hold's on-screen message, drawn by the fix itself into the engine's finished back buffer just
// before Present (display.cpp hooked_present, ahead of the test pipe's screenshot hook). The game's own timed network
// message (0x60a600) never showed during a hold (live-060zi ZI-1b / ZI-1bp: 4 + 12 captures on host and lobby guest,
// frozen scene, HUD, no text): Present keeps running while the world section is held, so a panel drawn there cannot
// depend on any game tick.
//
// Text only: a translucent dark panel with a thin border and white antialiased text (GDI, Arial bold, sized from the
// back buffer height), centred horizontally below the zone-title band. No game art, no logo, no engine font.
//
// Two ways onto the back buffer, both without touching any render state the engine set:
//  * blend (the normal path): CopyRects the panel's rectangle from the back buffer into a system-memory surface of
//    the same format, blend the panel over it on the CPU, CopyRects it back (X8R8G8B8 / A8R8G8B8 / R5G6B5 /
//    X1R5G5B5 / A1R5G5B5);
//  * clear (a multisampled back buffer, another format, or a refused CopyRects): IDirect3DDevice8::Clear over the
//    panel, its border and the text's pixel runs (opaque, the full-back-buffer viewport set and restored), only
//    when the current render target IS the back buffer.
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>

namespace hold_overlay
{
	// The panel for one text at one back-buffer size: straight-alpha ARGB, row-major, w * h.
	struct panel
	{
		unsigned width = 0, height = 0;
		std::vector<std::uint32_t> argb;
		std::vector<std::uint8_t> text_mask; // 1 where a text pixel is >= 50% covered (the clear path)
		unsigned border = 0;
	};
	// Renders `text` for a back buffer of bb_width x bb_height (GDI into a DIB). Empty panel on failure.
	// wrap: network notices may use multiple lines; admission holds retain their existing single line.
	panel render(const char* text, unsigned bb_width, unsigned bb_height, bool wrap = false);
	// Where the panel goes on that back buffer (clipped to it).
	RECT place(const panel& p, unsigned bb_width, unsigned bb_height);
	// One straight-alpha ARGB source pixel over one destination X8R8G8B8 pixel (the result has alpha 0xff).
	std::uint32_t blend(std::uint32_t dst_xrgb, std::uint32_t src_argb);
	// The 16-bit formats as X8R8G8B8 and back (D3DFMT_R5G6B5 = 23, X1R5G5B5 = 24, A1R5G5B5 = 25).
	std::uint32_t from16(std::uint16_t pixel, DWORD format);
	std::uint16_t to16(std::uint32_t xrgb, DWORD format);

	enum class path : std::uint8_t { none, blend, clear };
	struct outcome
	{
		path drawn = path::none; // none: nothing drawn (no text, or a failure)
		HRESULT result = S_OK;   // the failing call's result when drawn == none and step != nullptr
		const char* step = nullptr;
		RECT where{};
	};
	// Draws `text` (nothing when null/empty) onto the device's back buffer. Game/render thread only. force_clear: the
	// clear path even where the blend path would work (tests).
	outcome draw(void* device, const char* text, bool force_clear = false, bool wrap = false);
	// Frees the cached system-memory surface (device reset / release).
	void release();
}
