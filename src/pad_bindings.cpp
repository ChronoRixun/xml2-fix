#include "pad_bindings.hpp"
#include "log.hpp"

#include <Windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace xml2_pad_bindings
{
	namespace
	{
		// A binding is (device << 16) | control. Device 1 is the keyboard, 2 the mouse and 3-12
		// the gamepads in the order DirectInput lists them. Gamepad controls, read from the game's
		// c_dfDIJoystick2 state:
		//   1-16   axis n (X, Y, Z, Rx, Ry, Rz, slider 0, slider 1): 2n+1 above centre, 2n+2 below
		//   17-20  hat right, left, down, up
		//   21+    button n-21
		constexpr DWORD unbound = 0xFFFF;
		constexpr DWORD first_gamepad = 3;
		constexpr DWORD keyboard = 1;

		constexpr std::uint16_t axis_above(const int axis) { return static_cast<std::uint16_t>(2 * axis + 1); }
		constexpr std::uint16_t axis_below(const int axis) { return static_cast<std::uint16_t>(2 * axis + 2); }
		constexpr std::uint16_t button(const int index) { return static_cast<std::uint16_t>(21 + index); }
		constexpr std::uint16_t hat_right = 17, hat_left = 18, hat_down = 19, hat_up = 20;

		// Dual Action axes and buttons (pad_profile::logitech_dual_action).
		constexpr int x = 0, y = 1, z = 2, rz = 5;
		constexpr int pad_x = 0, pad_a = 1, pad_b = 2, pad_y = 3, pad_lb = 4, pad_rb = 5, pad_lt = 6, pad_rt = 7;
		constexpr int pad_back = 8, pad_start = 9, pad_rs = 11;

		struct pad_binding
		{
			const char* action; // as the game names it (registry values are <action><slot>)
			std::uint16_t control;
		};

		constexpr std::array<pad_binding, 23> layout{{
			{"Forward", axis_below(y)},
			{"Backward", axis_above(y)},
			{"MoveLeft", axis_below(x)},
			{"MoveRight", axis_above(x)},
			{"LowAttack", button(pad_x)},
			{"HighAttack", button(pad_y)},
			{"Jump", button(pad_a)},
			{"Guard", button(pad_b)},
			{"Power", button(pad_rb)},
			{"Solo", button(pad_lb)},
			{"TargetLock", button(pad_lt)},
			{"Ally", button(pad_rt)},
			{"MapToggle", button(pad_back)},
			{"Pause", button(pad_start)},
			{"Stats", button(pad_rs)},
			{"NextHero", hat_up},
			{"PreviousHero", hat_down},
			{"IncreaseHeroAggr", hat_right},
			{"DecreaseHeroAggr", hat_left},
			{"CameraUp", axis_below(rz)},
			{"CameraDown", axis_above(rz)},
			{"CameraLeft", axis_below(z)},
			{"CameraRight", axis_above(z)},
		}};

		constexpr int players = 4;
		constexpr int slots = 2;

		// Where a player's pad binding goes: player 1 keeps the keyboard in slot 1.
		constexpr int pad_slot(const int player) { return player == 0 ? 1 : 0; }
		constexpr DWORD pad_binding_for(const int player, const std::uint16_t control) { return ((first_gamepad + player) << 16) | control; }

		// XMen2.exe's built-in action table (the 2005 retail build, fixed base 0x400000): 42 entries of 0x94 bytes,
		// a 0x30-byte name, then per player 5 DWORD bindings (2 used), ..., per-slot lock flags at 0x80.
		constexpr std::uintptr_t default_table_rva = 0x6EE1F0 - 0x400000;
		constexpr size_t action_count = 42;
		constexpr size_t action_size = 0x94;
		constexpr size_t bindings_offset = 0x30;
		constexpr size_t locks_offset = 0x80;

		struct action_entry
		{
			std::byte* base;
			const char* name() const { return reinterpret_cast<const char*>(base); }
			DWORD& binding(const int player, const int slot) const { return reinterpret_cast<DWORD*>(base + bindings_offset)[player * 5 + slot]; }
			bool locked(const int player, const int slot) const { return base[locks_offset + player * 5 + slot] != std::byte{0}; }
		};

		// Stock defaults, by action and player/slot, before patching.
		std::array<std::array<DWORD, players * slots>, action_count> stock{};
		std::array<const char*, action_count> stock_names{};

		bool is_default_table(const std::byte* table)
		{
			__try
			{
				// NUL-padded names, and W as the first action's keyboard default.
				return std::memcmp(table, "Forward\0", 8) == 0 &&
				       std::memcmp(table + action_size, "Backward\0", 9) == 0 &&
				       reinterpret_cast<const DWORD*>(table + bindings_offset)[0] == ((keyboard << 16) | 0x11);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		const pad_binding* layout_for(const char* action)
		{
			for (const auto& binding : layout)
			{
				if (std::strcmp(binding.action, action) == 0)
				{
					return &binding;
				}
			}
			return nullptr;
		}

		bool patch_default_table()
		{
			auto* table = reinterpret_cast<std::byte*>(GetModuleHandleW(nullptr)) + default_table_rva;
			if (!is_default_table(table))
			{
				logger::write("bindings: not a known XMen2.exe build - pad bindings not added");
				return false;
			}

			DWORD old_protect = 0;
			VirtualProtect(table, action_count * action_size, PAGE_READWRITE, &old_protect);
			int patched = 0;
			for (size_t i = 0; i < action_count; ++i)
			{
				const action_entry entry{table + i * action_size};
				stock_names[i] = entry.name();
				for (int player = 0; player < players; ++player)
				{
					for (int slot = 0; slot < slots; ++slot)
					{
						stock[i][player * slots + slot] = entry.binding(player, slot);
					}
				}

				const auto* binding = layout_for(entry.name());
				if (!binding)
				{
					continue;
				}
				for (int player = 0; player < players; ++player)
				{
					if (!entry.locked(player, pad_slot(player)))
					{
						entry.binding(player, pad_slot(player)) = pad_binding_for(player, binding->control);
						++patched;
					}
				}
			}
			VirtualProtect(table, action_count * action_size, old_protect, &old_protect);
			logger::write("bindings: added %d gamepad bindings to the game's defaults", patched);
			return true;
		}

		bool is_gamepad_binding(const DWORD value)
		{
			const DWORD device = value >> 16;
			return value != unbound && value != 0xFFFFFFFF && device >= first_gamepad && device < first_gamepad + 10;
		}

		// Adds the layout to settings saved before the fix was installed, once.
		void migrate_saved_settings()
		{
			const std::wstring root = L"Software\\Activision\\X-Men Legends 2\\Controls";
			HKEY gamepads = nullptr;
			if (RegOpenKeyExW(HKEY_CURRENT_USER, (root + L"\\Gamepads").c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &gamepads) != ERROR_SUCCESS)
			{
				return; // first run: the game saves its (patched) defaults itself
			}

			DWORD done = 0, size = sizeof(done);
			if (RegQueryValueExW(gamepads, L"Xml2FixPadBindings", nullptr, nullptr, reinterpret_cast<BYTE*>(&done), &size) == ERROR_SUCCESS && done)
			{
				RegCloseKey(gamepads);
				return;
			}

			for (int player = 0; player < players; ++player)
			{
				HKEY key = nullptr;
				const auto path = root + L"\\Player" + std::to_wstring(player + 1);
				if (RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key) != ERROR_SUCCESS)
				{
					continue;
				}

				// A player who already has any gamepad binding set up keeps their setup as is.
				bool has_pad = false;
				for (size_t i = 0; i < action_count && !has_pad && stock_names[i]; ++i)
				{
					for (int slot = 0; slot < slots && !has_pad; ++slot)
					{
						const auto name = std::string(stock_names[i]) + std::to_string(slot + 1);
						DWORD value = 0, value_size = sizeof(value);
						has_pad = RegQueryValueExA(key, name.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &value_size) == ERROR_SUCCESS &&
						          is_gamepad_binding(value);
					}
				}

				int written = 0;
				for (size_t i = 0; i < action_count && !has_pad && stock_names[i]; ++i)
				{
					const auto* binding = layout_for(stock_names[i]);
					if (!binding)
					{
						continue;
					}

					const int slot = pad_slot(player);
					const auto name = std::string(stock_names[i]) + std::to_string(slot + 1);
					DWORD value = unbound, value_size = sizeof(value);
					const bool saved = RegQueryValueExA(key, name.c_str(), nullptr, nullptr, reinterpret_cast<BYTE*>(&value), &value_size) == ERROR_SUCCESS;
					const bool free = !saved || value == unbound || value == 0xFFFFFFFF || value == stock[i][player * slots + slot];
					if (free)
					{
						const DWORD pad = pad_binding_for(player, binding->control);
						RegSetValueExA(key, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&pad), sizeof(pad));
						++written;
					}
				}
				RegCloseKey(key);
				logger::write("bindings: player %d %s", player + 1,
				              has_pad ? "already has gamepad bindings - left as is" : (std::to_string(written) + " gamepad bindings added to saved settings").c_str());
			}

			done = 1;
			RegSetValueExW(gamepads, L"Xml2FixPadBindings", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&done), sizeof(done));
			RegCloseKey(gamepads);
		}
	}

	void install()
	{
		if (patch_default_table())
		{
			migrate_saved_settings();
		}
	}
}
