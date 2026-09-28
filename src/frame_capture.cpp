#include "frame_capture.hpp"
#include "d3d8_min.hpp"
#include "image_file.hpp"

#include <Unknwn.h> // IUnknown, which WIN32_LEAN_AND_MEAN leaves out

#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <fstream>

namespace frame_capture
{
	namespace
	{
		std::string failed(const char* what, const HRESULT result)
		{
			char text[96];
			std::snprintf(text, sizeof(text), "%s failed (%08lX)", what, result);
			return text;
		}

		struct release_on_exit
		{
			IUnknown* object;
			~release_on_exit()
			{
				if (object)
				{
					object->Release();
				}
			}
		};

		bool convertible(const DWORD format)
		{
			return format == d3d8::format_a8r8g8b8 || format == d3d8::format_x8r8g8b8 || format == d3d8::format_r5g6b5 ||
			       format == d3d8::format_x1r5g5b5 || format == d3d8::format_a1r5g5b5;
		}

		// One row of the locked surface to BGRX.
		void convert_row(const DWORD format, const std::uint8_t* in, std::uint8_t* out, const UINT width)
		{
			if (format == d3d8::format_a8r8g8b8 || format == d3d8::format_x8r8g8b8)
			{
				std::copy_n(in, static_cast<size_t>(width) * 4, out);
				return;
			}
			for (UINT x = 0; x < width; ++x)
			{
				const std::uint16_t pixel = static_cast<std::uint16_t>(in[x * 2] | (in[x * 2 + 1] << 8));
				std::uint8_t r, g, b;
				if (format == d3d8::format_r5g6b5)
				{
					r = static_cast<std::uint8_t>(((pixel >> 11) & 0x1F) * 255 / 31);
					g = static_cast<std::uint8_t>(((pixel >> 5) & 0x3F) * 255 / 63);
					b = static_cast<std::uint8_t>((pixel & 0x1F) * 255 / 31);
				}
				else
				{
					r = static_cast<std::uint8_t>(((pixel >> 10) & 0x1F) * 255 / 31);
					g = static_cast<std::uint8_t>(((pixel >> 5) & 0x1F) * 255 / 31);
					b = static_cast<std::uint8_t>((pixel & 0x1F) * 255 / 31);
				}
				out[x * 4 + 0] = b;
				out[x * 4 + 1] = g;
				out[x * 4 + 2] = r;
				out[x * 4 + 3] = 0xFF;
			}
		}
	}

	std::string read_back_buffer(void* device, frame& out)
	{
		if (!device)
		{
			return "no Direct3D device";
		}

		void* back = nullptr;
		HRESULT result = d3d8::method<d3d8::get_back_buffer_t>(device, d3d8::device_slot::get_back_buffer)(device, 0, d3d8::back_buffer_type_mono, &back);
		if (FAILED(result) || !back)
		{
			return failed("GetBackBuffer", result);
		}
		release_on_exit release_back{static_cast<IUnknown*>(back)};

		d3d8::surface_desc desc{};
		result = d3d8::method<d3d8::get_desc_t>(back, d3d8::surface_slot::get_desc)(back, &desc);
		if (FAILED(result))
		{
			return failed("GetDesc", result);
		}
		if (desc.multi_sample_type != d3d8::multisample_none)
		{
			return "the back buffer is multisampled (FSAA), which Direct3D 8 can't copy - the fix turns multisampling off when the pipe is on, so its Direct3D hook isn't in place";
		}
		if (!convertible(desc.format))
		{
			return "back buffer format " + std::to_string(desc.format) + " isn't one the capture converts";
		}

		// A system-memory copy of the same format is the one read-back Direct3D 8 offers.
		void* copy = nullptr;
		result = d3d8::method<d3d8::create_image_surface_t>(device, d3d8::device_slot::create_image_surface)(device, desc.width, desc.height, desc.format, &copy);
		if (FAILED(result) || !copy)
		{
			return failed("CreateImageSurface", result);
		}
		release_on_exit release_copy{static_cast<IUnknown*>(copy)};

		result = d3d8::method<d3d8::copy_rects_t>(device, d3d8::device_slot::copy_rects)(device, back, nullptr, 0, copy, nullptr);
		if (FAILED(result))
		{
			return failed("CopyRects", result);
		}

		d3d8::locked_rect lock{};
		result = d3d8::method<d3d8::lock_rect_t>(copy, d3d8::surface_slot::lock_rect)(copy, &lock, nullptr, d3d8::lock_read_only);
		if (FAILED(result) || !lock.bits)
		{
			return failed("LockRect", result);
		}

		out.width = desc.width;
		out.height = desc.height;
		out.bgrx.resize(static_cast<size_t>(desc.width) * desc.height * 4);
		for (UINT y = 0; y < desc.height; ++y)
		{
			convert_row(desc.format, static_cast<const std::uint8_t*>(lock.bits) + static_cast<std::ptrdiff_t>(y) * lock.pitch,
			            out.bgrx.data() + static_cast<size_t>(y) * desc.width * 4, desc.width);
		}
		d3d8::method<d3d8::unlock_rect_t>(copy, d3d8::surface_slot::unlock_rect)(copy);
		return {};
	}

	std::string save(const frame& picture, const std::filesystem::path& path)
	{
		if (!picture.width || !picture.height || picture.bgrx.size() < static_cast<size_t>(picture.width) * picture.height * 4)
		{
			return "nothing captured";
		}

		auto extension = path.extension().wstring();
		std::ranges::transform(extension, extension.begin(), [](const wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
		const auto bytes = extension == L".bmp" ? image_file::encode_bmp(picture.width, picture.height, picture.bgrx)
		                                        : image_file::encode_png(picture.width, picture.height, picture.bgrx);

		std::error_code ignored;
		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), ignored);
		}
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return "can't write " + path.string();
		}
		file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		return file ? std::string() : "writing " + path.string() + " failed";
	}
}
