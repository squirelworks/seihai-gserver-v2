// Modified by the Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the per-build Graal 3D capability table (see Graal3DCapabilities.h).
// Values and assumptions are Graal_analysis version-profiles/three-d.toml's.
#include <graal3d/Graal3DCapabilities.h>
#include <graal3d/Graal3DObjects.h>

#include <algorithm>
#include <array>
#include <cctype>

namespace graal3d
{
	namespace
	{
		using F = CapField;

		constexpr std::array<std::string_view, 3> kRefuseScriptsTer{ ".dso", ".cs", ".ter" };

		constexpr uint64_t allBut(std::initializer_list<CapField> fields) { return kAllCapFields & ~capMask(fields); }

		// 3.04 Linux: stock TGE 1.x networking, runs TorqueScript, no Graal net classes.
		constexpr Capabilities k304{
			.label = "3.04", .token = "G3D03014", .protocol = 9, .game = "Torque Game Engine Demo",
			.classTableTypes = 0, .classIds12Bit = false, .lengthPrefixes = false, .gameLayer = GameLayer::Tge,
			.missionPhases = 3, .phasesOptional = false, .clientMoves = true,
			.controlPlayer = true, .controlCamera = true, .controlGameObject = false,
			.terFile = true, .terMax = 3, .terrainChunks = false, .heightsCache = false, .terrainTextureSize = 0,
			.water = true, .torqueScript = true, .serverScripts = false, .terraformer = Terraformer::Full, .greyscaleRgba32Only = false, .terrainEditor = true,
			.dtsMin = 0, .dtsMax = 24, .difVersion = 44, .threeDs = false, .md3Version = 0, .stripPngTextures = false,
			.downloadRefused = {}, .bigdata = false, .bigdataChunkMax = 0, .terrainChunkMax = 0, .udpPortOffset = 1,
			.requiresSky = false, .skyGif = false, .drawn = Drawn::World, .mirrors2dPosition = false, .sees2dPlayers = false, .takes2dChat = false, .terrainTextures = false, .groundCover = false, .dropsUnknownGhost = false,
			.assumed = allBut({ F::Protocol, F::Game, F::ClassTable, F::ClassIds12Bit, F::LengthPrefixes, F::GameLayer,
								 F::TerrainChunks, F::ChunkCache, F::TorqueScript, F::Terraformer, F::TerrainEditor, F::Bigdata,
								 F::BigdataChunkMax, F::TerrainChunkMax, F::TerrainTextures }),
		};

		// 3.153 Mac: Graal's net changes, the TGE game layer, TorqueScript, both terrain paths.
		constexpr Capabilities k3153{
			.label = "3.153", .token = "G3D28015", .protocol = 19, .game = "Graal3D",
			.classTableTypes = 3, .classIds12Bit = true, .lengthPrefixes = true, .gameLayer = GameLayer::Tge,
			.missionPhases = 3, .phasesOptional = false, .clientMoves = true,
			.controlPlayer = true, .controlCamera = true, .controlGameObject = false,
			.terFile = true, .terMax = 3, .terrainChunks = true, .heightsCache = true, .terrainTextureSize = 0,
			.water = true, .torqueScript = true, .serverScripts = false, .terraformer = Terraformer::Full, .greyscaleRgba32Only = false, .terrainEditor = true,
			.dtsMin = 0, .dtsMax = 24, .difVersion = 44, .threeDs = true, .md3Version = 15, .stripPngTextures = false,
			.downloadRefused = kRefuseScriptsTer, .bigdata = true, .bigdataChunkMax = 191, .terrainChunkMax = 1023, .udpPortOffset = 1,
			.requiresSky = true, .skyGif = true, .drawn = Drawn::SkyOnly, .mirrors2dPosition = false, .sees2dPlayers = false, .takes2dChat = false, .terrainTextures = true, .groundCover = false, .dropsUnknownGhost = false,
			.assumed = capMask({ F::PhasesOptional, F::TerMax, F::TerrainTexture, F::Water, F::GreyscaleRgba32Only, F::DtsMin,
								 F::DtsMax, F::DifVersion, F::Md3Version, F::TextureNames, F::BigdataChunkMax, F::TerrainChunkMax,
								 F::Mirrors2dPosition, F::Sees2dPlayers, F::Takes2dChat, F::GroundCover, F::DropsUnknownGhost }),
		};

		// 4.04r2 Windows Graal3DEngine.dll: protocol 19, no TorqueScript compiler.
		constexpr Capabilities k404{
			.label = "4.04r2", .token = "G3D17026", .protocol = 19, .game = "Graal3D",
			.classTableTypes = 3, .classIds12Bit = true, .lengthPrefixes = true, .gameLayer = GameLayer::Tge,
			.missionPhases = 3, .phasesOptional = false, .clientMoves = true,
			.controlPlayer = true, .controlCamera = true, .controlGameObject = false,
			.terFile = true, .terMax = 3, .terrainChunks = true, .heightsCache = true, .terrainTextureSize = 0,
			.water = true, .torqueScript = false, .serverScripts = false, .terraformer = Terraformer::Greyscale, .greyscaleRgba32Only = true, .terrainEditor = true,
			.dtsMin = 0, .dtsMax = 24, .difVersion = 44, .threeDs = true, .md3Version = 15, .stripPngTextures = false,
			.downloadRefused = kRefuseScriptsTer, .bigdata = true, .bigdataChunkMax = 191, .terrainChunkMax = 1023,
			.udpPortOffset = 1,
			.requiresSky = false, .skyGif = false, .drawn = Drawn::SkyOnly, .mirrors2dPosition = false, .sees2dPlayers = false, .takes2dChat = false, .terrainTextures = true, .groundCover = false, .dropsUnknownGhost = false,
			.assumed = capMask({ F::PhasesOptional, F::ChunkCache, F::TerrainTexture, F::GreyscaleRgba32Only, F::TextureNames,
								 F::TerrainChunkMax, F::RequiresSky, F::SkyGif, F::Sees2dPlayers, F::Takes2dChat, F::TerrainTextures,
								 F::GroundCover }),
		};

		constexpr Capabilities with(Capabilities c, std::string_view label, std::string_view token, uint64_t assumed)
		{
			c.label = label;
			c.token = token;
			c.assumed = assumed;
			return c;
		}

		// 4.20r6 Mac: protocol 21, the GameObject layer, streamed terrain only.
		constexpr Capabilities k420{
			.label = "4.20r6", .token = "G3D27076", .protocol = 21, .game = "Graal3D",
			.classTableTypes = 2, .classIds12Bit = true, .lengthPrefixes = true, .gameLayer = GameLayer::GameObject,
			.missionPhases = 2, .phasesOptional = false, .clientMoves = false,
			.controlPlayer = false, .controlCamera = true, .controlGameObject = true,
			.terFile = false, .terMax = 0, .terrainChunks = true, .heightsCache = true, .terrainTextureSize = 256,
			.water = true, .torqueScript = false, .serverScripts = false, .terraformer = Terraformer::Greyscale, .greyscaleRgba32Only = true, .terrainEditor = true,
			.dtsMin = 17, .dtsMax = 24, .difVersion = 44, .threeDs = true, .md3Version = 0, .stripPngTextures = true,
			.downloadRefused = kRefuseScriptsTer, .bigdata = true, .bigdataChunkMax = 191, .terrainChunkMax = 1023,
			.udpPortOffset = 1,
			.requiresSky = false, .skyGif = false, .drawn = Drawn::World, .mirrors2dPosition = false, .sees2dPlayers = false, .takes2dChat = true, .terrainTextures = true, .groundCover = true, .dropsUnknownGhost = true,
			.assumed = allBut({ F::Protocol, F::Game, F::ClassTable, F::ClassIds12Bit, F::LengthPrefixes, F::GameLayer,
								 F::ClientMoves, F::TerrainChunks, F::Water, F::Bigdata }),
		};

		// 5.00r7 Windows: protocol 24, GameObject layer, streamed terrain.
		constexpr Capabilities k500{
			.label = "5.00r7", .token = "G3D22067", .protocol = 24, .game = "Graal3D",
			.classTableTypes = 2, .classIds12Bit = true, .lengthPrefixes = true, .gameLayer = GameLayer::GameObject,
			.missionPhases = 2, .phasesOptional = true, .clientMoves = false,
			.controlPlayer = false, .controlCamera = true, .controlGameObject = true,
			.terFile = false, .terMax = 0, .terrainChunks = true, .heightsCache = true, .terrainTextureSize = 256,
			.water = true, .torqueScript = false, .serverScripts = false, .terraformer = Terraformer::Greyscale, .greyscaleRgba32Only = true, .terrainEditor = true,
			.dtsMin = 17, .dtsMax = 24, .difVersion = 44, .threeDs = true, .md3Version = 0, .stripPngTextures = true,
			.downloadRefused = kRefuseScriptsTer, .bigdata = true, .bigdataChunkMax = 191, .terrainChunkMax = 1023,
			.udpPortOffset = 1,
			.requiresSky = false, .skyGif = false, .drawn = Drawn::World, .mirrors2dPosition = false, .sees2dPlayers = false, .takes2dChat = true, .terrainTextures = true, .groundCover = true, .dropsUnknownGhost = true,
			.assumed = allBut({ F::Protocol, F::Game, F::ClassTable, F::GameLayer, F::MissionPhases, F::TerrainChunks, F::ChunkCache,
								 F::TerrainTexture, F::TorqueScript, F::ServerScripts, F::ThreeDs }),
		};

		// Windows 4.20r8: 4.20r6's values except what was read. A lab run
		// (Graal_analysis PR #854) linked it as protocol 24; it takes the 5.12
		// TerrainBlock and chunk layout. Under Mesa (T3D-117) it draws streamed
		// terrain, but never fetches heights.info or terrain textures, so the
		// ground is untextured. (Its GDI Generic OpenGL 1.1 crash was the driver.)
		// It takes replicator ghosts but grows no cover, under GDI or Mesa.
		constexpr Capabilities k4208 = []
		{
			Capabilities c = with(k420, "4.20r8", "G3D20126",
								  allBut({ F::Protocol, F::Game, F::GameLayer, F::MissionPhases, F::TerrainChunks, F::ChunkCache,
									  F::TorqueScript, F::ServerScripts, F::Drawn, F::TerrainTextures, F::GroundCover,
									  F::DropsUnknownGhost }));
			c.protocol = 24;
			c.terrainTextures = false;
			c.groundCover = false;
			return c;
		}();

		// Linux 4.20r6 and 4.20r8: protocol 24, 5.12's row except what the
		// 2026-11-25 world lab read. Its loader takes DTS 17, but a version-17
		// shape froze it: serve DTS 24. Streamed terrain is drawn (lab).
		constexpr Capabilities k420Linux = []
		{
			Capabilities c = with(k500, "4.20 Linux", "G3D22126",
								  allBut({ F::Protocol, F::Game, F::GameLayer, F::MissionPhases, F::TerrainChunks, F::DtsMin, F::DtsMax,
									  F::DifVersion, F::ThreeDs, F::Drawn, F::TerrainTextures }));
			return c;
		}();

		constexpr std::array<Capabilities, 10> kRows{
			k304,
			k3153,
			k404,
			with(k404, "4.04r2 Mac", "G3D18026", kAllCapFields),
			k420,
			k4208,
			k500,
			with(k500, "5.12", "G3D14097", capMask({ F::RequiresSky, F::SkyGif })),
			with(k500, "5.12 Mac", "G3D15097", kAllCapFields),
			k420Linux,
		};

		bool endsWithNoCase(std::string_view s, std::string_view suffix)
		{
			if (s.size() < suffix.size())
				return false;
			return std::equal(suffix.begin(), suffix.end(), s.end() - suffix.size(), [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
			});
		}
	} // namespace

	std::string_view capFieldKey(CapField field)
	{
		static constexpr std::array<std::string_view, static_cast<size_t>(CapField::Count)> kKeys{
			"protocol", "game", "class_table", "class_ids_12bit", "length_prefixes", "game_layer",
			"mission_phases", "phases_optional", "client_moves", "control_objects", "ter_file", "ter_max",
			"terrain_chunks", "chunk_cache", "terrain_texture", "water", "torque_script", "server_scripts", "terraformer",
			"greyscale_rgba32_only", "terrain_editor", "dts_min", "dts_max", "dif_version", "three_ds", "md3_version", "texture_names",
			"download_refused", "bigdata", "bigdata_chunk_max", "terrain_chunk_max", "udp_port_offset",
			"requires_sky", "sky_gif", "drawn", "mirrors_2d_position", "sees_2d_players", "takes_2d_chat", "terrain_textures", "ground_cover", "drops_unknown_ghost",
		};
		const auto i = static_cast<size_t>(field);
		return i < kKeys.size() ? kKeys[i] : std::string_view{};
	}

	bool Capabilities::requestsDownload(std::string_view name) const
	{
		return std::none_of(downloadRefused.begin(), downloadRefused.end(),
							[name](std::string_view ext) { return endsWithNoCase(name, ext); });
	}

	std::string_view Capabilities::textureRequestName(std::string_view texture) const
	{
		if (stripPngTextures && texture.size() > 4 && endsWithNoCase(texture, ".png"))
			return texture.substr(0, texture.size() - 4);
		return texture;
	}

	std::string Capabilities::skyTextureRequestName(std::string_view texture) const
	{
		if (!skyGif)
			return std::string(texture);
		auto stem = texture;
		const auto dot = texture.rfind('.');
		if (dot != std::string_view::npos && texture.find_first_of("/\\", dot) == std::string_view::npos)
			stem = texture.substr(0, dot);
		return std::string(stem) + ".gif";
	}

	std::string Capabilities::fixtureRow() const
	{
		auto b = [](bool v) { return std::string(v ? "1" : "0"); };
		auto n = [](unsigned v) { return std::to_string(v); };
		std::string control;
		for (auto [on, name] : { std::pair{ controlPlayer, "Player" }, std::pair{ controlGameObject, "GameObject" },
								 std::pair{ controlCamera, "Camera" } })
		{
			if (!on)
				continue;
			if (!control.empty())
				control += ',';
			control += name;
		}
		std::string refused;
		for (auto ext : downloadRefused)
		{
			if (!refused.empty())
				refused += ',';
			refused += ext;
		}
		std::string assumedText;
		if (allAssumed())
			assumedText = "all";
		else
		{
			for (uint8_t i = 0; i < static_cast<uint8_t>(CapField::Count); ++i)
			{
				if (assumed & (uint64_t{ 1 } << i))
				{
					if (!assumedText.empty())
						assumedText += ',';
					assumedText += capFieldKey(static_cast<CapField>(i));
				}
			}
		}
		const std::string texture = terrainTextureSize ? n(terrainTextureSize) + "x" + n(terrainTextureSize) + "x24" : "-";
		const char* former = terraformer == Terraformer::Full ? "full" : terraformer == Terraformer::Greyscale ? "greyscale" : "none";
		std::string row;
		for (const std::string& col : {
				 std::string(label), n(protocol), std::string(game), n(classTableTypes), b(classIds12Bit),
				 b(lengthPrefixes), std::string(gameLayer == GameLayer::Tge ? "tge" : "gameobject"), n(missionPhases),
				 b(phasesOptional), b(clientMoves), control, b(terFile), n(terMax), b(terrainChunks),
				 std::string(heightsCache ? "heights" : "none"), texture, b(water),
				 std::string(torqueScript ? "compiles" : "none"), b(serverScripts), std::string(former), b(greyscaleRgba32Only), b(terrainEditor), n(dtsMin), n(dtsMax),
				 n(difVersion), b(threeDs), n(md3Version), std::string(stripPngTextures ? "strip_png" : "as_named"),
				 refused.empty() ? std::string("-") : refused, b(bigdata), n(bigdataChunkMax), n(terrainChunkMax),
				 n(udpPortOffset), b(requiresSky), b(skyGif),
				 std::string(drawn == Drawn::World ? "world" : drawn == Drawn::SkyOnly ? "sky_only" : "nothing"),
				 b(mirrors2dPosition), b(sees2dPlayers), b(takes2dChat), b(terrainTextures), b(groundCover), b(dropsUnknownGhost), assumedText.empty() ? std::string("-") : assumedText })
		{
			if (!row.empty())
				row += '\t';
			row += col;
		}
		return row;
	}

	std::span<const Capabilities> allCapabilities() { return kRows; }

	const Capabilities* capabilitiesForToken(std::string_view token)
	{
		for (const auto& c : kRows)
			if (c.token == token)
				return &c;
		return nullptr;
	}

	const Capabilities* capabilitiesFor(std::string_view token, uint32_t protocol)
	{
		if (const auto* c = capabilitiesForToken(token); c && c->protocol == protocol)
			return c;
		std::string_view fallback;
		switch (protocol)
		{
			case 19: fallback = "4.04r2"; break;
			case 21: fallback = "4.20r6"; break;
			case 24: fallback = "5.12"; break;
			default: return nullptr;
		}
		for (const auto& c : kRows)
			if (c.label == fallback)
				return &c;
		return nullptr;
	}
	bool offersWorldObject(const Capabilities* caps, const GhostObject& object)
	{
		if (!object.layout512)
			return true;
		if (object.rawClass == "TerrainBlock")
			return caps && caps->takesStreamedTerrain512();
		if (object.rawClass == "WaterBlock")
			return caps && caps->takesWater512();
		// Ground cover: the layout is read from 5.12 only, and it grows on terrain.
		// 4.20r8 takes the ghost but grows nothing (lab), so it is not sent.
		if (object.rawClass == "DynamicShapeReplicator" || object.rawClass == "DynamicGrass" || object.rawClass == "DynamicGrassReplicator")
			return caps && caps->takesStreamedTerrain512() && caps->groundCover;
		return true;
	}
} // namespace graal3d
