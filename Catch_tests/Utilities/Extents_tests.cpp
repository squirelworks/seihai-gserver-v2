#include "catch2/catch_all.hpp"

#include <type_traits>

#include <utilities/Extents.h>

using namespace preagonal;

SCENARIO("toGlobalPixelPosition composes gmap-global pixels", "[extents][gmap]")
{
	GIVEN("a level-local position on map cell (0,0)")
	{
		const LocalPixelPosition local{160, 320};
		const auto global = toGlobalPixelPosition(MapPosition{0, 0}, local);

		THEN("global equals local (non-gmap or NW cell)")
		{
			REQUIRE(global.x() == 160);
			REQUIRE(global.y() == 320);
			REQUIRE(toMapPosition(global) == MapPosition{0, 0});
			REQUIRE(toLocalPixelPosition(global) == local);
		}
	}

	GIVEN("a level at a non-zero gmap offset")
	{
		const LocalPixelPosition local{100, 200};
		const MapPosition cell{2, 3};
		const auto global = toGlobalPixelPosition(cell, local);

		THEN("map cell and local round-trip through global")
		{
			REQUIRE(global.x() == 2 * 1024 + 100);
			REQUIRE(global.y() == 3 * 1024 + 200);
			REQUIRE(toMapPosition(global) == cell);
			REQUIRE(toLocalPixelPosition(global) == local);
		}

		AND_THEN("passing only local as if it were global loses the map offset")
		{
			const PixelPosition mistaken{local.x(), local.y()};
			REQUIRE(toMapPosition(mistaken) == MapPosition{0, 0});
			REQUIRE(mistaken.x() != global.x());
			REQUIRE(mistaken.y() != global.y());
		}

		AND_THEN("adding the sublevel origin twice doubles the map offset")
		{
			const auto origin = toGlobalPixelPosition(cell, LocalPixelPosition{0, 0});
			const PixelPosition doubled{global.x() + origin.x(), global.y() + origin.y()};
			REQUIRE(toMapPosition(doubled) == MapPosition{4, 6});
		}
	}

	GIVEN("adjacent gmap levels on the same row")
	{
		const LocalPixelPosition nearEastEdge{1000, 48};
		const LocalPixelPosition justIntoNext{16, 48};
		const auto onLeft = toGlobalPixelPosition(MapPosition{0, 0}, nearEastEdge);
		const auto onRight = toGlobalPixelPosition(MapPosition{1, 0}, justIntoNext);

		THEN("each keeps its own map cell")
		{
			REQUIRE(toMapPosition(onLeft) == MapPosition{0, 0});
			REQUIRE(toMapPosition(onRight) == MapPosition{1, 0});
			REQUIRE(toLocalPixelPosition(onLeft) == nearEastEdge);
			REQUIRE(toLocalPixelPosition(onRight) == justIntoNext);
		}

		AND_THEN("crossing from left local into the next cell is one map step east")
		{
			const auto stepped = PixelPosition{onLeft.x() + 48, onLeft.y()};
			REQUIRE(toMapPosition(stepped) == MapPosition{1, 0});
			REQUIRE(toLocalPixelPosition(stepped).x() == static_cast<int16_t>((1000 + 48) % 1024));
		}
	}

	GIVEN("a warp from a non-gmap level onto a gmap sublevel")
	{
		// Non-gmap players sit at map (0,0); the destination sublevel supplies the offset.
		const LocalPixelPosition fromSolo{488, 480};
		const MapPosition destCell{1, 2};
		const auto onGmap = toGlobalPixelPosition(destCell, fromSolo);

		THEN("local is preserved on the destination cell")
		{
			REQUIRE(toMapPosition(onGmap) == destCell);
			REQUIRE(toLocalPixelPosition(onGmap) == fromSolo);
			REQUIRE(onGmap.x() == 1 * 1024 + 488);
			REQUIRE(onGmap.y() == 2 * 1024 + 480);
		}
	}
}

SCENARIO("PixelPosition and LocalPixelPosition stay distinct types", "[extents]")
{
	STATIC_REQUIRE(!std::is_same_v<PixelPosition, LocalPixelPosition>);
	STATIC_REQUIRE(std::is_same_v<PixelPosition::ValueType, int32_t>);
	STATIC_REQUIRE(std::is_same_v<LocalPixelPosition::ValueType, int16_t>);
}

SCENARIO("Character-style global composition matches toGlobalPixelPosition", "[extents][gmap]")
{
	GIVEN("map cell (4,1) and local (16,32) as Character stores them")
	{
		const uint8_t mapX = 4;
		const uint8_t mapY = 1;
		const int16_t localPixelX = 16;
		const int16_t localPixelY = 32;
		const PixelPosition fromFields{
			static_cast<int32_t>((mapX * 1024) + localPixelX),
			static_cast<int32_t>((mapY * 1024) + localPixelY),
			0
		};

		THEN("toGlobalPixelPosition matches the Character formula")
		{
			REQUIRE(fromFields == toGlobalPixelPosition(MapPosition{mapX, mapY}, LocalPixelPosition{localPixelX, localPixelY}));
			REQUIRE(toMapPosition(fromFields) == MapPosition{mapX, mapY});
			REQUIRE(toLocalPixelPosition(fromFields) == LocalPixelPosition{localPixelX, localPixelY});
		}
	}
}
