#include "ladder_paths_rules.hpp"
#include <cstdio>
#include <vector>

struct fake_engine
{
	bool ladder = true, active = true, original_result = true;
	ladder_paths_rules::tracking tracker;
	ladder_paths_rules::identity current{12, 513, 100, 200};
	ladder_paths_rules::identity key(std::uint32_t) { return current; }
	int originals = 0, scheduled = 0, goals = 0;
	std::vector<int> order;
	bool original_update(std::uint32_t, float delay, bool force)
	{ ++originals; order.push_back(1); return original_result && delay == -.5f && !force; }
	bool is_ladder_path(std::uint32_t) { return ladder; }
	bool path_active(std::uint32_t) { return active; }
	void schedule_native(std::uint32_t, float delay, bool force)
	{ if (delay == -.5f && !force) { ++scheduled; order.push_back(2); } }
	bool original_goal_complete(std::uint32_t) { ++goals; return true; }
};

int main()
{
	using namespace ladder_paths_rules;
	int failures = 0;
	auto check = [&](bool value, const char* name) { if (!value) { ++failures; std::printf("FAIL %s\n", name); } };
	check(!known_path(0, {0, 0}), "null assignment is never a ladder");
	check(!known_path(5, {6, 7}) && known_path(6, {6, 7}) && known_path(7, {6, 7}), "exact assigned path identity");
	fake_engine e;
	check(update(e, 12, -.5f, false), "original return value retained");
	check(e.order == std::vector<int>({1, 2}), "native character update precedes scheduling");
	e.ladder = false;
	update(e, 12, -.5f, false);
	check(e.scheduled == 1 && e.originals == 2, "ordinary paths do not gain scheduling");
	e.ladder = true; e.active = false;
	update(e, 12, -.5f, false);
	check(e.scheduled == 1 && e.originals == 3, "expired path does not reschedule");
	check(!goal_complete(e, 12, true) && e.goals == 0, "final evaluator callback cannot snap to an unrelated goal");
	check(goal_complete(e, 12, false) && e.goals == 1, "normal character goal logic remains native");
	update(e, 12, -.5f, false, true);
	check(goal_complete(e, 12, true) && e.goals == 2, "completed timer no longer overrides a later evaluator");
	e.current.entity_id += 256;
	check(!goal_complete(e, 12, true), "new entity generation cannot inherit completed state");
	e.ladder = false;
	check(goal_complete(e, 12, true) && e.goals == 3, "unrelated evaluator call stays native");
	e.ladder = true; e.active = true; e.original_result = false;
	check(!update(e, 12, -.5f, false) && e.scheduled == 2, "scheduling cannot replace the original result");
	std::printf("ladder path rules: %d failures\n", failures);
	return failures ? 1 : 0;
}
