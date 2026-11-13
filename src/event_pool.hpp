#pragma once

// The timed-event manager's record pool (event_pool_rules.hpp, docs/timed-event-pool.md): 1125 records in retail,
// unchecked. [Limits] TimedEvents=N raises it (default 8192 whenever the entity capacity is raised; 0 or 1125 keeps
// retail), and a diagnostic watchdog on the manager's reset / add / pump slots logs the first moment the queue's own
// counts disagree, the queue runs empty in a live zone, or an add meets a full pool (it is then dropped instead of
// overwriting a live record). Installs after every module that guards the manager's retail bytes (xml1 turrets, join
// in progress): its hooks chain to whatever those left in the slots.

#include <Windows.h>

#include <string>

namespace event_pool
{
	void install(HMODULE game);
	bool installed();     // the watchdog and the add guard are on the manager's slots
	bool raised();        // the pool is larger than retail
	std::string status(); // one line for the test pipe / logs
}
