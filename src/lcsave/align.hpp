#pragma once

// legacy.align (the era model generator's alignment of an era's numbered-entity list to the reference's): a
// longest-common-subsequence map computed forwards and backwards with difflib.SequenceMatcher(autojunk=False)'s
// matching blocks, unique names matched by name, and the era ordinals the two maps disagree on reported as
// ambiguous. The fix never generates an era model; this is here so that the reference's vectors are checked in C++
// too and a tool written against the library can regenerate the zone deltas.

#include <algorithm>
#include <cstdint>
#include <map>
#include <tuple>
#include <vector>

namespace lcsave::detail
{
	// difflib.SequenceMatcher(None, a, b, autojunk=False).get_matching_blocks() without the final dummy block
	template <typename T> std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> matching_blocks(const std::vector<T>& a,
		const std::vector<T>& b)
	{
		std::map<T, std::vector<std::size_t>> b2j;
		for (std::size_t j = 0; j < b.size(); ++j) b2j[b[j]].push_back(j);
		auto longest = [&](std::size_t alo, std::size_t ahi, std::size_t blo, std::size_t bhi)
		{
			std::size_t besti = alo, bestj = blo, bestsize = 0;
			std::map<std::size_t, std::size_t> j2len;
			for (std::size_t i = alo; i < ahi; ++i)
			{
				std::map<std::size_t, std::size_t> newj2len;
				const auto f = b2j.find(a[i]);
				if (f != b2j.end())
				{
					for (const auto j : f->second)
					{
						if (j < blo) continue;
						if (j >= bhi) break;
						const auto prev = j ? j2len.find(j - 1) : j2len.end();
						const std::size_t k = newj2len[j] = (prev != j2len.end() ? prev->second : 0) + 1;
						if (k > bestsize)
						{
							besti = i - k + 1;
							bestj = j - k + 1;
							bestsize = k;
						}
					}
				}
				j2len = std::move(newj2len);
			}
			while (besti > alo && bestj > blo && a[besti - 1] == b[bestj - 1])
			{
				--besti;
				--bestj;
				++bestsize;
			}
			while (besti + bestsize < ahi && bestj + bestsize < bhi && a[besti + bestsize] == b[bestj + bestsize]) ++bestsize;
			return std::make_tuple(besti, bestj, bestsize);
		};
		std::vector<std::tuple<std::size_t, std::size_t, std::size_t, std::size_t>> queue{{0, a.size(), 0, b.size()}};
		std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> blocks;
		while (!queue.empty())
		{
			const auto [alo, ahi, blo, bhi] = queue.back();
			queue.pop_back();
			const auto x = longest(alo, ahi, blo, bhi);
			const auto [i, j, k] = x;
			if (k)
			{
				blocks.push_back(x);
				if (alo < i && blo < j) queue.emplace_back(alo, i, blo, j);
				if (i + k < ahi && j + k < bhi) queue.emplace_back(i + k, ahi, j + k, bhi);
			}
		}
		std::sort(blocks.begin(), blocks.end());
		std::vector<std::tuple<std::size_t, std::size_t, std::size_t>> merged;
		for (const auto& [i, j, k] : blocks)
		{
			if (!merged.empty())
			{
				auto& [i1, j1, k1] = merged.back();
				if (i1 + k1 == i && j1 + k1 == j)
				{
					k1 += k;
					continue;
				}
			}
			merged.emplace_back(i, j, k);
		}
		return merged;
	}

	// legacy.align(a, b) -> (1-based reference ordinal per era ordinal, 0 = no match; ambiguous era ordinals)
	template <typename T> std::pair<std::vector<std::uint32_t>, std::vector<std::uint32_t>> align(const std::vector<T>& a, const std::vector<T>& b)
	{
		auto lcs_map = [](const std::vector<T>& x, const std::vector<T>& y)
		{
			std::vector<std::uint32_t> m(x.size(), 0);
			for (const auto& [i, j, k] : matching_blocks(x, y))
			{
				for (std::size_t q = 0; q < k; ++q) m[i + q] = static_cast<std::uint32_t>(j + q + 1);
			}
			return m;
		};
		std::map<T, std::size_t> ca, cb;
		for (const auto& x : a) ++ca[x];
		for (const auto& y : b) ++cb[y];
		auto left = lcs_map(a, b);
		const auto rev = lcs_map(std::vector<T>(a.rbegin(), a.rend()), std::vector<T>(b.rbegin(), b.rend()));
		std::vector<std::uint32_t> right(a.size(), 0);
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			const auto v = rev[a.size() - 1 - i];
			right[i] = v == 0 ? 0 : static_cast<std::uint32_t>(b.size() - v + 1);
		}
		std::map<T, std::uint32_t> where;
		for (std::size_t j = 0; j < b.size(); ++j)
		{
			if (cb[b[j]] == 1) where[b[j]] = static_cast<std::uint32_t>(j + 1);
		}
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			const auto w = where.find(a[i]);
			if (ca[a[i]] == 1 && w != where.end()) left[i] = right[i] = w->second;
		}
		std::vector<std::uint32_t> ambiguous;
		for (std::size_t i = 0; i < a.size(); ++i)
		{
			const auto nb = cb.count(a[i]) ? cb[a[i]] : 0;
			if (left[i] != right[i] || !left[i] || (ca[a[i]] > 1 && ca[a[i]] != nb)) ambiguous.push_back(static_cast<std::uint32_t>(i + 1));
		}
		return {left, ambiguous};
	}
}
