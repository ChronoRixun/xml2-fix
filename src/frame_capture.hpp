#pragma once

// A picture of the frame the game just drew, taken inside the process: the test pipe's
// "screenshot" command ([Test] InputPipe, see test_input.hpp). Grabbing the screen from outside
// needs the game window visible and in front, which is exactly what the tests avoid; the
// Direct3D 8 back buffer has the finished frame whether the window is covered or not. Direct3D 8
// can't copy a multisampled back buffer, so the display fix turns multisampling off when the
// pipe is on.

#include <Windows.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace frame_capture
{
	struct frame
	{
		UINT width = 0;
		UINT height = 0;
		std::vector<std::uint8_t> bgrx; // 4 bytes per pixel, rows top-down, no padding
	};

	// On the thread that owns the device (the game's render thread, just before Present): copies
	// the back buffer into `out`. Returns an empty string, or what went wrong.
	std::string read_back_buffer(void* device, frame& out);

	// Writes the frame as a .bmp, or as a .png for any other extension, creating the folder.
	// Returns an empty string, or what went wrong.
	std::string save(const frame& picture, const std::filesystem::path& path);
}
