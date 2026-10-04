#include "forced_teams_rules.hpp"
#include <cstdio>

struct fake_engine
{
	std::optional<std::string> name = "collector";
	std::optional<int> amount = 1;
	int calls = 0;
	int awarded = 0;
	std::optional<std::string> text_argument(void*, int) { return name; }
	std::optional<int> int_argument(void*, int) { return amount; }
	std::optional<std::vector<forced_teams_rules::grant>> grant_stat_points(const std::string& actor, int count)
	{
		++calls;
		if (actor != "collector") return std::vector<forced_teams_rules::grant>{};
		awarded += count;
		forced_teams_rules::grant g{};
		g.character = g.stats = true; g.before = 2; g.after = 2 + count;
		return std::vector<forced_teams_rules::grant>{g};
	}
	void log(const std::string&) {}
};

int main()
{
	using namespace forced_teams_rules;
	int failed = 0;
	auto check = [&](bool ok) { if (!ok) ++failed; };
	fake_engine e;
	add_stat_points(e, nullptr);
	check(e.calls == 1 && e.awarded == 1);
	for (int n : {-1, 0, 21}) { e.amount = n; add_stat_points(e, nullptr); }
	check(e.calls == 1 && e.awarded == 1);
	e.amount = std::nullopt; add_stat_points(e, nullptr);
	e.amount = 1; e.name = std::nullopt; add_stat_points(e, nullptr);
	check(e.calls == 1);
	e.name = "missing"; add_stat_points(e, nullptr);
	check(e.calls == 2 && e.awarded == 1);
	check(stat_point_total(3, 1) == 4);
	check(stat_point_total(32766, 1) == 32767);
	check(!stat_point_total(32767, 1) && !stat_point_total(-1, 1));
	check(feature_named("StatPoints") == feature::stat_points);
	check(table_count + builtin_count <= tree_capacity);
	check(std::string_view(functions[static_cast<std::size_t>(function::stat_points)].name) == "addStatPoints");
	std::printf("stat points rules: %d failures\n", failed);
	return failed ? 1 : 0;
}
