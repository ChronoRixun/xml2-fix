#pragma once

// A way to press keys and take screenshots without giving the game the focus, for automated
// tests (the XML1 port's tour runner drives the game for an hour at a time). Off unless
// xml2-fix.ini says:
//
//   [Test]
//   InputPipe = 1
//   PipeName = xml2-fix-input   ; the default; another name per game folder to drive two games at once
//
// XMen2.exe reads the keyboard through its own DirectInput 8 device (GUID_SysKeyboard,
// c_dfDIKeyboard, GetDeviceState(256) every frame; the engine's window messages aren't used),
// created with DISCL_FOREGROUND, so it goes dead when another window has the focus. With the
// pipe on, that device's SetCooperativeLevel becomes DISCL_BACKGROUND | DISCL_NONEXCLUSIVE, and
// every state the game reads has the pipe's keys OR-ed in. The real keyboard still only counts
// while the game has the focus, so typing in another window stays there.
//
// That keyboard drives every screen, the Advanced Options panel (options_menu.hpp) included: its
// widgets handle WM_KEYUP, but the panel's per-frame input function (0x619070) makes those from
// DirectInput key releases (Esc, Enter, the arrows) and pad buttons; key messages posted to the
// window never reach them (its message filter, 0x6223d0, drops WM_KEYUP and the widgets ignore
// WM_KEYDOWN). So "tap DOWN" / "tap ENTER" / "tap LEFT" drive the panel too.
//
// The pipe is \\.\pipe\xml2-fix-input (or \\.\pipe\<PipeName>), one command per line, one reply line per command
// ("ok ..." or "error ..."): down/up/tap/hold KEYS [ms], release, screenshot PATH, script
// STATEMENT, console COMMAND, status, ping (test_input_rules.hpp has the grammar). Its thread
// never touches the game's; keys it holds expire after 10 s so a dead client can't wedge one.
// "screenshot" copies the Direct3D 8 back buffer just before Present (frame_capture.hpp), so it
// works with the window covered. "script" and "console" put a line in the game's own console
// queue (two commands of 127 characters at most; "script" sends "runscript STATEMENT"): the
// game's keyboard read queues it, on the thread that runs that queue, and the reply comes once
// it is queued ("ok queued LINE") or after 2 s ("error ..."). The game runs it at its next frame.
// tools/fixinput.py in the xml1-port repo is the client.

namespace test_input
{
	// Reads [Test] from xml2-fix.ini; when the pipe is on, starts its thread and asks the display
	// fix for a frame hook. Must run before display::install.
	void install();

	// Watches a fresh IDirectInput8 of the game's for its keyboard device. Nothing unless the pipe is on.
	void hook_direct_input8(void* direct_input);
}
