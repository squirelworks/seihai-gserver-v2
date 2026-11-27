// Modified by the Seihai project, 2026-11-27: see CHANGES-SEIHAI.md.
// New file: tests for big 3D worlds (Graal3DBigWorld.h): distance scoping and the
// large terrain's sliding window, ported from seihai-gserver #226 and #227.
#include "catch2/catch_all.hpp"

#include <set>
#include <string>
#include <vector>

#include <zlib.h>

#include <graal3d/Graal3DBigWorld.h>
#include <graal3d/Graal3DObjects.h>

using namespace graal3d;

namespace
{
	ScopeOptions opts()
	{
		ScopeOptions o;
		o.mode = ScopeOptions::Mode::On;
		o.radius = 100.0f;
		o.hysteresis = 20.0f;
		return o;
	}

	std::vector<ScopeCandidate> line(const std::vector<float>& xs)
	{
		std::vector<ScopeCandidate> out;
		for (size_t i = 0; i < xs.size(); ++i)
			out.push_back({i, {xs[i], 0.0f, 0.0f}});
		return out;
	}
} // namespace

SCENARIO("Distance scoping brings objects in within the radius and out past the hysteresis")
{
	const std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
	THEN("objects within the radius come in, nearest first")
	{
		const auto p = planScope({}, origin, line({50.0f, 100.0f, 101.0f, -99.0f}), opts(), 99);
		REQUIRE(p.add == std::vector<size_t>{0, 3, 1});
		REQUIRE(p.remove.empty());
	}
	THEN("the distance is three-dimensional")
	{
		std::vector<ScopeCandidate> objs{{0, {60.0f, 60.0f, 60.0f}}, {1, {0.0f, 0.0f, 99.0f}}};
		REQUIRE(planScope({}, origin, objs, opts(), 99).add == std::vector<size_t>{1});
	}
	THEN("an object in scope stays until past radius + hysteresis, and one in the band does not come in")
	{
		const std::set<size_t> scoped{0, 1, 2};
		const auto p = planScope(scoped, origin, line({110.0f, 120.0f, 120.5f}), opts(), 99);
		REQUIRE(p.add.empty());
		REQUIRE(p.remove == std::vector<size_t>{2});
		REQUIRE(planScope({}, origin, line({110.0f}), opts(), 99).add.empty());
	}
	THEN("walking out and back crosses each edge once")
	{
		std::set<size_t> scoped;
		std::vector<std::pair<bool, float>> events;
		std::vector<float> walk;
		for (int s = 0; s <= 30; ++s)
			walk.push_back(static_cast<float>(s) * 10.0f);
		for (int s = 29; s >= 0; --s)
			walk.push_back(static_cast<float>(s) * 10.0f);
		for (float x : walk)
		{
			const auto p = planScope(scoped, {x, 0.0f, 0.0f}, {{7, {0.0f, 0.0f, 0.0f}}}, opts(), 99);
			for (auto i : p.add)
				scoped.insert(i), events.emplace_back(true, x);
			for (auto i : p.remove)
				scoped.erase(i), events.emplace_back(false, x);
		}
		REQUIRE(events == std::vector<std::pair<bool, float>>{{true, 0.0f}, {false, 130.0f}, {true, 100.0f}});
	}
	THEN("the budget takes the nearest and keeps what is in")
	{
		const std::set<size_t> scoped{0, 1};
		REQUIRE(planScope(scoped, origin, line({90.0f, 95.0f, 40.0f, 10.0f, 30.0f, 20.0f}), opts(), 4).add == std::vector<size_t>{3, 5});
		REQUIRE(planScope(scoped, origin, line({90.0f, 95.0f, 1.0f}), opts(), 2).add.empty());
		REQUIRE(ScopeBudget == 3584);
	}
	THEN("auto turns on only past its threshold, and the modes parse")
	{
		ScopeOptions o = opts();
		o.mode = ScopeOptions::Mode::Auto;
		o.autoAbove = 2000;
		REQUIRE_FALSE(o.applies(2000));
		REQUIRE(o.applies(2001));
		o.mode = ScopeOptions::Mode::Off;
		REQUIRE_FALSE(o.applies(1000000));
		REQUIRE(ScopeOptions::parseMode("auto") == ScopeOptions::Mode::Auto);
		REQUIRE(ScopeOptions::parseMode("on") == ScopeOptions::Mode::On);
		REQUIRE(ScopeOptions::parseMode("off") == ScopeOptions::Mode::Off);
		REQUIRE_FALSE(ScopeOptions::parseMode("sometimes"));
	}
}

SCENARIO("A large terrain maps world chunks to the client's 16 x 16 slots")
{
	THEN("slots are X mod 16 for 48 chunks, X for 16, and centred for 32")
	{
		for (int64_t x : {0, 7, 15, 16, 31, 47})
			REQUIRE(terrainSlotOf(x, 48) == static_cast<size_t>(x % 16));
		for (int64_t x = 0; x < 16; ++x)
			REQUIRE(terrainSlotOf(x, 16) == static_cast<size_t>(x));
		REQUIRE(terrainSlotOf(16, 32) == 8);
		REQUIRE(terrainSlotOf(8, 32) == 0);
	}
	THEN("the window is the 16 chunks round the player")
	{
		const auto w = terrainWindow({0.0f, 0.0f, 0.0f}, 8, 48, 48);
		for (size_t i = 0; i < 256; ++i)
		{
			REQUIRE(w[i].first >= 16);
			REQUIRE(w[i].first < 32);
			REQUIRE(terrainSlotOf(w[i].first, 48) == i % 16);
			REQUIRE(terrainSlotOf(w[i].second, 48) == i / 16);
		}
		const auto plain = terrainWindow({900.0f, -900.0f, 0.0f}, 8, 16, 16);
		for (size_t i = 0; i < 256; ++i)
			REQUIRE(plain[i] == std::pair<uint32_t, uint32_t>{static_cast<uint32_t>(i % 16), static_cast<uint32_t>(i / 16)});
	}
	THEN("a chunk crossing changes 16 slots straight and 31 diagonally, none within a chunk")
	{
		const auto changed = [](const TerrainWindow& a, const TerrainWindow& b) {
			size_t n = 0;
			for (size_t i = 0; i < 256; ++i)
				n += a[i] != b[i];
			return n;
		};
		const auto w0 = terrainWindow({10.0f, 10.0f, 0.0f}, 8, 48, 48);
		const auto w1 = terrainWindow({138.0f, 10.0f, 0.0f}, 8, 48, 48);
		REQUIRE(changed(w0, w1) == 16);
		for (size_t i = 0; i < 256; ++i)
			if (w0[i] != w1[i])
			{
				REQUIRE(i % 16 == 0);
				REQUIRE(w1[i].first == 32);
			}
		REQUIRE(changed(w0, terrainWindow({138.0f, 138.0f, 0.0f}, 8, 48, 48)) == 31);
		REQUIRE(changed(w0, terrainWindow({100.0f, 100.0f, 0.0f}, 8, 48, 48)) == 0);
	}
	THEN("past the edge a slot keeps the nearest chunk of the terrain in it")
	{
		for (const auto& p : {std::array<float, 3>{1.0e4f, 0.0f, 0.0f}, std::array<float, 3>{-1.0e4f, -1.0e4f, 0.0f}})
		{
			const auto w = terrainWindow(p, 8, 48, 48);
			for (size_t i = 0; i < 256; ++i)
			{
				REQUIRE(w[i].first < 48);
				REQUIRE(w[i].second < 48);
				REQUIRE(terrainSlotOf(w[i].first, 48) == i % 16);
			}
		}
		REQUIRE(terrainChunkForSlot(0, 100, 48) == 32);
		REQUIRE(terrainChunkForSlot(15, -50, 48) == 15);
	}
}

SCENARIO("A terrain line may declare a large terrain")
{
	THEN("<width>x<height> after repeat makes one, in any order")
	{
		const auto world = parseWorld("terrain big 8 repeat 48x48\n");
		REQUIRE(world.errors.empty());
		REQUIRE(world.terrain);
		REQUIRE(world.terrain->isLarge());
		REQUIRE(world.terrain->largeWidth == 48);
		REQUIRE(world.terrain->large.size() == 48u * 48u);
		const auto w2 = parseWorld("terrain big 64X32 repeat 4\n");
		REQUIRE(w2.errors.empty());
		REQUIRE(w2.terrain->largeHeight == 32);
		REQUIRE(w2.terrain->squareSize == 4);
		REQUIRE_FALSE(parseWorld("terrain t 8 repeat 16x16\n").terrain->isLarge());
		REQUIRE_FALSE(parseWorld("terrain t 8 repeat\n").terrain->isLarge());
	}
	THEN("a bad size or no repeat is an error and no terrain")
	{
		for (const char* bad : {"terrain big 8 48x48\n", "terrain big 8 repeat 47x48\n", "terrain big 8 repeat 8x48\n",
								"terrain big 8 repeat 512x16\n", "terrain big 8 repeat 48xq\n"})
		{
			const auto w = parseWorld(bad);
			REQUIRE_FALSE(w.errors.empty());
			REQUIRE_FALSE(w.terrain);
		}
	}
	THEN("every chunk file of a large terrain is loaded")
	{
		auto world = parseWorld("terrain big 8 repeat 32x18\n");
		const std::string plain = "chunk";
		std::string packed(64, '\0');
		uLongf size = static_cast<uLongf>(packed.size());
		REQUIRE(compress(reinterpret_cast<Bytef*>(packed.data()), &size, reinterpret_cast<const Bytef*>(plain.data()), plain.size()) == Z_OK);
		packed.resize(size);
		const auto errors = loadTerrain(*world.terrain, [&](const std::string& path) -> std::optional<std::string> {
			if (path == "big/heights31-17.dat" || path == "big/heights0-0.dat")
				return packed;
			return std::nullopt;
		});
		REQUIRE(errors.empty());
		REQUIRE(world.terrain->largeChunk(31, 17));
		REQUIRE(world.terrain->largeChunk(0, 0));
		REQUIRE_FALSE(world.terrain->largeChunk(1, 1));
		REQUIRE_FALSE(world.terrain->largeChunk(32, 0));
		for (const auto& c : world.terrain->chunks)
			REQUIRE_FALSE(c);
	}
}
