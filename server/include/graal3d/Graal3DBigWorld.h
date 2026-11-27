// Modified by the Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: big 3D worlds, ported from seihai-gserver (graal3d::scope and the
// large terrain of graal3d::ground, seihai-gserver #226 and #227).
//
// Distance scoping. A GameObject-era client (4.20r6, 4.20r8, 5.x) has a ghost
// table of 4096 entries; a real 5.12 (lab, 2026-11-27) took about 130 creates
// a second (4000 objects: 31 s), drew a black view all session once world
// objects filled the table before its own object and Camera, and never draws
// an object more than about 1000 units from its camera. With graal3dscope a
// world `object` is ghosted only while it is within the radius of the
// player's own object (3D distance) and deleted once it is farther than the
// radius plus the hysteresis. On the same 5.12, 20,164 objects at radius 1100
// and hysteresis 200 joined in about 3 s and peaked at about 1400 entries.
//
// Large terrains. A client holds a grid of 16 x 16 terrain chunks. A terrain
// of width x height chunks (`terrain <folder> <size> repeat <w>x<h>`) is
// centred on the origin: world chunk X covers x from (X - width / 2) * span to
// (X - width / 2 + 1) * span, span being 16 squares. With `repeat` the client
// tiles its grid, so world chunk X lies in slot (X - width / 2 + 8) mod 16 (X
// mod 16 for a width of 48). Each slot holds the world chunk of the 16 round
// the player's chunk (8 behind, 7 ahead) that falls in it; past the terrain's
// edge, the nearest chunk of the terrain that falls in it.

#ifndef SEIHAI_GRAAL3D_GRAAL3DBIGWORLD_H
#define SEIHAI_GRAAL3D_GRAAL3DBIGWORLD_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace graal3d
{
	// graal3dscope: off (the default), on (every world), or auto (a world with
	// more than `autoAbove` world objects).
	struct ScopeOptions
	{
		enum class Mode
		{
			Off,
			On,
			Auto,
		};
		Mode mode = Mode::Off;
		size_t autoAbove = 2000;
		float radius = 1100.0f;
		float hysteresis = 200.0f;

		[[nodiscard]] bool applies(size_t worldObjects) const
		{
			return mode == Mode::On || (mode == Mode::Auto && worldObjects > autoAbove);
		}

		static std::optional<Mode> parseMode(std::string_view v)
		{
			if (v == "off" || v == "false" || v.empty())
				return Mode::Off;
			if (v == "on" || v == "true")
				return Mode::On;
			if (v == "auto")
				return Mode::Auto;
			return std::nullopt;
		}
	};

	// How often each client's scope and terrain window are looked at.
	constexpr uint64_t BigWorldPassMs = 200;
	// Ghost table entries kept free of scoped objects for players and the rest
	// of the world: a scoped client holds at most 4096 - 512 world objects.
	constexpr size_t ScopeReserved = 512;
	constexpr size_t ScopeBudget = 4096 - ScopeReserved;

	inline float distance3(const std::array<float, 3>& a, const std::array<float, 3>& b)
	{
		const float dx = a[0] - b[0], dy = a[1] - b[1], dz = a[2] - b[2];
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}

	struct ScopeCandidate
	{
		size_t index;
		std::array<float, 3> position;
	};

	struct ScopePlan
	{
		std::vector<size_t> add; // nearest first
		std::vector<size_t> remove;
	};

	// One pass: `scoped` is what the client has. An object not in scope comes
	// in within the radius; one in scope goes past radius + hysteresis. At
	// most `budget` are in scope after the pass, the nearest first and the
	// ones already in kept.
	inline ScopePlan planScope(const std::set<size_t>& scoped, const std::array<float, 3>& at, const std::vector<ScopeCandidate>& objects,
							   const ScopeOptions& opts, size_t budget)
	{
		ScopePlan out;
		std::vector<std::pair<float, size_t>> near;
		size_t kept = 0;
		for (const auto& o : objects)
		{
			const float d = distance3(at, o.position);
			if (scoped.contains(o.index))
			{
				if (d > opts.radius + opts.hysteresis)
					out.remove.push_back(o.index);
				else
					++kept;
			}
			else if (d <= opts.radius)
				near.emplace_back(d, o.index);
		}
		std::sort(near.begin(), near.end());
		const size_t room = budget > kept ? budget - kept : 0;
		for (size_t i = 0; i < near.size() && i < room; ++i)
			out.add.push_back(near[i].second);
		return out;
	}

	// The most chunks a side of a large terrain.
	constexpr uint32_t MaxLargeChunks = 256;

	// The client slot of world chunk `c` along an axis of `n` chunks.
	inline size_t terrainSlotOf(int64_t c, uint32_t n)
	{
		const int64_t s = (c - static_cast<int64_t>(n / 2) + 8) % 16;
		return static_cast<size_t>(s < 0 ? s + 16 : s);
	}

	// The world chunk slot `slot` holds with the player in world chunk `at`.
	inline uint32_t terrainChunkForSlot(size_t slot, int64_t at, uint32_t n)
	{
		const int64_t first = at - 8;
		int64_t step = (static_cast<int64_t>(slot) - static_cast<int64_t>(terrainSlotOf(first, n))) % 16;
		if (step < 0)
			step += 16;
		int64_t c = first + step;
		const auto nn = static_cast<int64_t>(n);
		if (c < 0)
			c += 16 * ((-c + 15) / 16);
		if (c >= nn)
			c -= 16 * ((c - nn + 16) / 16);
		return static_cast<uint32_t>(c);
	}

	// The world chunk a coordinate is in (maybe off the terrain).
	inline int64_t terrainChunkAt(float v, float span, uint32_t n)
	{
		if (!std::isfinite(v))
			return n / 2;
		return static_cast<int64_t>(std::floor(v / span)) + static_cast<int64_t>(n / 2);
	}

	// Each slot's world chunk (x, y), index gy * 16 + gx.
	using TerrainWindow = std::array<std::pair<uint32_t, uint32_t>, 256>;

	inline TerrainWindow terrainWindow(const std::array<float, 3>& position, uint32_t squareSize, uint32_t width, uint32_t height)
	{
		const float span = static_cast<float>(16 * squareSize);
		const auto ax = terrainChunkAt(position[0], span, width);
		const auto ay = terrainChunkAt(position[1], span, height);
		TerrainWindow w{};
		for (size_t i = 0; i < 256; ++i)
			w[i] = {terrainChunkForSlot(i % 16, ax, width), terrainChunkForSlot(i / 16, ay, height)};
		return w;
	}
} // namespace graal3d

#endif
