// Modified by the Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: what each Graal 3D client build can take, so the 3D service offers a
// client only what its build has (streamed terrain, the 5.x TerrainBlock and
// WaterBlock layouts, TorqueScript files, a Camera or GameObject control object).
//
// The table mirrors seihai-core's VersionProfile::three_d, whose source of truth is
// Graal_analysis version-profiles/three-d.toml. Every field is either read for that
// build (static read, lab run or capture) or assumed (carried from the nearest read
// build or from stock TGE 1.x); `assumed` lists the assumed fields so the service
// can log when it relies on one. Catch_tests/Graal3D/Graal3DCapabilities_tests.cpp
// compares every row with the mirrored fixture.

#ifndef SEIHAI_GRAAL3D_GRAAL3DCAPABILITIES_H
#define SEIHAI_GRAAL3D_GRAAL3DCAPABILITIES_H

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>

namespace graal3d
{
	struct GhostObject;

	// One field of Capabilities, in the order of three-d.toml.
	enum class CapField : uint8_t
	{
		Protocol,
		Game,
		ClassTable,
		ClassIds12Bit,
		LengthPrefixes,
		GameLayer,
		MissionPhases,
		PhasesOptional,
		ClientMoves,
		ControlObjects,
		TerFile,
		TerMax,
		TerrainChunks,
		ChunkCache,
		TerrainTexture,
		Water,
		TorqueScript,
		ServerScripts,
		Terraformer,
		GreyscaleRgba32Only,
		TerrainEditor,
		DtsMin,
		DtsMax,
		DifVersion,
		ThreeDs,
		Md3Version,
		TextureNames,
		DownloadRefused,
		Bigdata,
		BigdataChunkMax,
		TerrainChunkMax,
		UdpPortOffset,
		RequiresSky,
		SkyGif,
		Drawn,
		Mirrors2dPosition,
		Sees2dPlayers,
		Takes2dChat,
		TerrainTextures,
		DropsUnknownGhost,
		Count,
	};

	std::string_view capFieldKey(CapField field);

	constexpr uint64_t capMask(std::initializer_list<CapField> fields)
	{
		uint64_t m = 0;
		for (auto f : fields)
			m |= uint64_t{ 1 } << static_cast<uint8_t>(f);
		return m;
	}

	static_assert(static_cast<uint8_t>(CapField::Count) <= 64, "the assumed mask is 64 bits");
	inline constexpr uint64_t kAllCapFields = ~uint64_t{ 0 } >> (64 - static_cast<uint8_t>(CapField::Count));

	enum class GameLayer : uint8_t
	{
		Tge,        // datablocks, Player, moves (3.04, 3.153, 4.04r2)
		GameObject, // Graal's GameObject model (4.20 and 5.x)
	};

	enum class Terraformer : uint8_t
	{
		Full,      // the stock procedural generator
		Greyscale, // loadGreyscale, saveGreyscale and setTerrain only
		None,
	};

	// The most a build has been seen to draw on this gserver: a lab state, not a
	// limit of the client.
	enum class Drawn : uint8_t
	{
		Nothing, // stayed on the loading picture (3.153)
		SkyOnly, // the Sky gradient only (4.04r2 under Wine)
		World,   // terrain, shapes or GameObjects
	};

	struct Capabilities
	{
		std::string_view label; // e.g. "5.12"
		std::string_view token; // login token, e.g. "G3D14097"
		uint32_t protocol;
		std::string_view game;
		uint8_t classTableTypes; // 0 (stock), 3 (with datablocks), 2
		bool classIds12Bit;
		bool lengthPrefixes;
		GameLayer gameLayer;
		uint8_t missionPhases;
		bool phasesOptional; // builds the world with no mission phases
		bool clientMoves;    // sends Torque Moves; otherwise reports its position
		bool controlPlayer;
		bool controlCamera;
		bool controlGameObject;
		bool terFile;       // loads a local .ter
		uint8_t terMax;     // highest .ter version (0: none)
		bool terrainChunks; // TerrainChunkEvent / TerrainRequestUpdatesEvent
		bool heightsCache;  // levels3d/heights/<server>/heights<x>-<y>.dat + heights.info
		uint16_t terrainTextureSize; // terrain textures must be N x N x 24 (0: not read)
		bool water;
		bool torqueScript; // compiles and runs .cs / .dso (its own shipped scripts)
		bool serverScripts; // a server can make it run new TorqueScript: no read build can
		Terraformer terraformer;
		bool greyscaleRgba32Only; // loadGreyscale is only safe with a 32-bit RGBA PNG (5.12 lab)
		bool terrainEditor;
		uint8_t dtsMin; // DTS versions dtsMin..dtsMax (low byte of the version word)
		uint8_t dtsMax;
		uint8_t difVersion;
		bool threeDs;
		uint8_t md3Version;
		bool stripPngTextures; // asks for and stores a .png texture under its bare name
		std::span<const std::string_view> downloadRefused;
		bool bigdata;
		uint8_t bigdataChunkMax;
		uint16_t terrainChunkMax;
		uint8_t udpPortOffset;
		bool requiresSky;       // crashes unless a Sky is ghosted (3.153 lab)
		bool skyGif;            // asks for sky textures as .gif, never .png (3.153 lab)
		Drawn drawn;            // the most it has drawn on this gserver
		bool mirrors2dPosition; // its 3D walk moves its 2D player (no read build does)
		bool sees2dPlayers;     // draws 2D players in its level (5.12 does not, lab)
		bool takes2dChat;       // gets 2D players' chat as onRemotePlayerChats (5.12 lab)
		bool terrainTextures;  // fetches terrain textures and heights.info; 4.20r8 draws streamed terrain untextured (lab T3D-117)
		bool dropsUnknownGhost; // logs and drops an update to an uncreated ghost; 4.04r2 crashes (fuzz tier 1)
		uint64_t assumed; // capMask of the fields that are assumed, not read

		[[nodiscard]] bool confirmed(CapField f) const { return (assumed & capMask({ f })) == 0; }
		[[nodiscard]] bool allAssumed() const { return assumed == kAllCapFields; }

		// The server may stream terrain in the 5.x TerrainBlock and chunk layout.
		// 3.153 and 4.04r2 take chunks too, but their TerrainBlock is stock TGE's.
		[[nodiscard]] bool takesStreamedTerrain512() const { return terrainChunks && gameLayer == GameLayer::GameObject; }
		// The 5.x WaterBlock layout (Graal_analysis torque/terrain-512.md section 5).
		[[nodiscard]] bool takesWater512() const { return water && gameLayer == GameLayer::GameObject; }
		// The client reads a DTS shape of this version.
		[[nodiscard]] bool readsDts(uint8_t version) const { return version >= dtsMin && version <= dtsMax; }
		// The client asks the gserver for a missing resource with this name.
		[[nodiscard]] bool requestsDownload(std::string_view name) const;
		// The name the client asks for when a material names `texture`.
		[[nodiscard]] std::string_view textureRequestName(std::string_view texture) const;
		// The name the client asks for when a Sky names `texture` (3.153: <stem>.gif).
		[[nodiscard]] std::string skyTextureRequestName(std::string_view texture) const;
		// 2D clients see this player walk only if the server moves its 2D player.
		[[nodiscard]] bool needs2dPositionRelay() const { return !mirrors2dPosition; }
		// One line per row in the fixture's columns (Graal3DCapabilities_tests).
		[[nodiscard]] std::string fixtureRow() const;
	};

	// Every row, in the order of three-d.toml.
	std::span<const Capabilities> allCapabilities();

	// The row for a login token (e.g. "G3D14097"), or nullptr.
	const Capabilities* capabilitiesForToken(std::string_view token);

	// The row a client with this token and Torque protocol gets: the token's row
	// when its protocol matches, else the default build for the protocol (19:
	// 4.04r2, 21: 4.20r6, 24: 5.12), else nullptr.
	const Capabilities* capabilitiesFor(std::string_view token, uint32_t protocol);

	// Whether a client of this build (nullptr: unknown) is sent this world object.
	// The `terrain` and `water` world lines write the 5.x TerrainBlock and
	// WaterBlock layouts, which 3.153's and 4.04r2's stock TGE classes do not
	// read; every other object goes to every client.
	bool offersWorldObject(const Capabilities* caps, const GhostObject& object);
} // namespace graal3d

#endif
