#pragma once

// X-Men Legends II (PC) ships keyboard-only default bindings: every gamepad starts unbound
// and the game sends players to Advanced options to bind all 42 actions by hand. This gives
// the pads a console-style layout instead (as a Logitech Dual Action - see pad_profile):
//
//   left stick   move                  right stick  camera
//   A            Jump / Xtreme         X            Attack / Power 1
//   Y            Smash / Power 2       B            Use / Boost
//   RB (hold)    Use Powers            LB           Call Allies
//   LT           Health Pack           RT           Energy Pack
//   D-pad        choose hero           Back         map    Start  pause    RS click  stats
//
// Player 1 gets pad 1 as its secondary binding (the keyboard stays primary); players 2-4
// get pads 2-4 as their primary one.

namespace xml2_pad_bindings
{
	// Adds the pad layout to the game's built-in defaults (used on first run and by "Revert
	// to defaults"), and once to saved settings, filling only unbound or stock-default slots.
	// Must run before the game reads its settings.
	void install();
}
