// Modified by the Seihai project, 2026-11-12, 2026-11-13, 2026-11-19, 2026-11-20, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: the ghosted objects of a Graal 3D world in the GameObject era (4.20r6
// and 5.x): GameObject, the Graal class every 3D thing is, and Sun. Their update
// layouts come from a static read of the 5.00r7 Graal3DEngine.dll (protocol 24,
// the same as 5.12): GameObject::packUpdate / unpackUpdate, the movement objects
// and Sun::packUpdate. Nothing here has been run against a real client yet.
//
// A GameObject update is thirteen sections, each one flag and then its payload:
//
//   1 name        string                       8 shape data     int14 length + data
//   2 scale       3 floats                     9 chat           string
//   3 shape       string (.dts .dif .3ds .ft) 10 script classes string
//   4 skin        string                      11 attr[1..8]     8 x (flag -> string)
//   5 transform   position (3 floats), then   12 nickname       string + R, G, B, A bytes
//                 axis (3 floats), angle      13 flags          int16
//   6 movetype    string (interpolate, list, rigid)
//   7 movement data, no length: present only while a movement object exists
//
// A Camera update is a GameObject update followed by two more flags: camera mode
// (flag -> int1) and the object it follows (flag -> flag -> int12 ghost index).
// The server sends neither, so the client's scripts choose the mode.
//
// The control object's packet data (GameConnection read/writePacket) is the same
// update, with no length prefix. The client sends its own control object's update
// back in every packet, so the server must be able to read one.

#ifndef SEIHAI_GRAAL3D_GRAAL3DOBJECTS_H
#define SEIHAI_GRAAL3D_GRAAL3DOBJECTS_H

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <graal3d/Graal3DTge.h>
#include <graal3d/TorqueBitStream.h>

namespace graal3d
{
	// GameObject dirty bits (the server's masks; the wire only has the flags).
	namespace GameObjectBits
	{
		inline constexpr uint32_t Name = 0x1;
		inline constexpr uint32_t Scale = 0x2; // section 2: scale (lab 2026-11-25, real 4.206)
		inline constexpr uint32_t Shape = 0x8;
		inline constexpr uint32_t Skin = 0x10;
		inline constexpr uint32_t Transform = 0x20;
		inline constexpr uint32_t MoveData = 0x40;
		inline constexpr uint32_t MoveType = 0x80;
		inline constexpr uint32_t ShapeData = 0xF00;
		inline constexpr uint32_t Chat = 0x1000;
		inline constexpr uint32_t Classes = 0x2000; // never sent: the client's own writer never sets it
		inline constexpr uint32_t Attr1 = 0x4000;   // attr[i] is Attr1 << (i - 1), i = 1..8
		inline constexpr uint32_t Attrs = 0x3FC000;
		inline constexpr uint32_t Nickname = 0x400000;
		inline constexpr uint32_t Flags = 0x800000;
	} // namespace GameObjectBits

	// Section 13.
	namespace GameObjectFlags
	{
		inline constexpr uint16_t EditorObject = 0x04;
		inline constexpr uint16_t Solid = 0x08;
		inline constexpr uint16_t Shadow = 0x10;
		inline constexpr uint16_t ShowTerrain = 0x20;
		inline constexpr uint16_t Collision = 0x40;
	} // namespace GameObjectFlags

	// Position, then a rotation as Torque's AngAxisF (angle in radians).
	struct Transform
	{
		std::array<float, 3> position{};
		std::array<float, 3> axis{0.0f, 0.0f, 1.0f};
		float angle = 0.0f;

		bool operator==(const Transform&) const = default;
	};

	struct GameObjectState
	{
		std::string name;
		std::array<float, 3> scale{1.0f, 1.0f, 1.0f};
		std::string shape;
		std::string skin;
		Transform transform;
		std::string chat;
		std::array<std::string, 8> attr;
		std::string nickname;
		std::array<uint8_t, 4> nickColor{255, 255, 255, 255};
		uint16_t flags = 0;

		bool operator==(const GameObjectState&) const = default;
	};

	// The bits that differ between two states (what an update must carry).
	uint32_t gameObjectChanges(const GameObjectState& before, const GameObjectState& after);

	// Everything a client needs to create the object from scratch.
	uint32_t gameObjectFullMask(const GameObjectState& state);

	// Writes the thirteen sections for `mask`. The server never sends movetype, so
	// the client never has a movement object from us and section 7 is always 0.
	void writeGameObjectUpdate(BitWriter& writer, const GameObjectState& state, uint32_t mask);

	// Reads an update into `state`. `moveType` is the movetype the writer's object
	// has (it decides whether section 7 is present) and is updated from section 6.
	// Returns the dirty bits of the sections it carried. Check reader.overrun()
	// for a truncated update.
	uint32_t readGameObjectUpdate(BitReader& reader, GameObjectState& state, std::string& moveType);

	// True for the movetypes that create a movement object.
	bool hasMovementObject(std::string_view moveType);

	// Sun: one flag, then name, light direction (3 floats), colour RGBA (4 floats),
	// ambient RGBA (4 floats), in stock TGE's order.
	struct SunState
	{
		std::string name = "Sun";
		std::array<float, 3> direction{0.57735f, 0.57735f, -0.57735f};
		std::array<float, 4> color{0.8f, 0.8f, 0.8f, 1.0f};
		std::array<float, 4> ambient{0.3f, 0.3f, 0.35f, 1.0f};

		bool operator==(const SunState&) const = default;
	};

	void writeSunUpdate(BitWriter& writer, const SunState& sun);
	SunState readSunUpdate(BitReader& reader);

	// Sky, in the layout the 5.12 client reads a Sky update in; the 4.04r2 client's
	// Sky::unpackUpdate reads the same layout. The server sends it whole:
	// the init part (mask 1) and the material list (mask 0x80); the later
	// sections (storm, wind and fog changes) are left clear. Floats are raw 32-bit
	// values, the bools whole bytes. Field names come from the GS2 properties the
	// client registers for Sky; where the client's behaviour does not tie a value to
	// a name, the name here is a guess and the comment says so.
	struct SkyState
	{
		std::string name = "Sky";
		std::string materialList;        // the sky-box textures, comma separated: front, right, back, left, top, bottom
		float visibleDistance = 500.0f;   // the float sent with the material list (guess: visible distance)
		std::array<float, 3> fogColor{0.5f, 0.6f, 0.8f};
		std::array<uint8_t, 3> bools{1, 0, 0}; // +0xeb1, +0xeb2, +0xea7 (guess: useskytextures, renderbottomtexture, norenderbans)
		std::array<float, 3> solidColor{0.4f, 0.6f, 0.9f}; // guess: skysolidcolor
		std::array<float, 3> cloudHeight{};
		std::array<float, 3> cloudSpeed{};
		std::array<float, 3> windVelocity{};

		bool operator==(const SkyState&) const = default;
	};

	void writeSkyUpdate(BitWriter& writer, const SkyState& sky);
	SkyState readSkyUpdate(BitReader& reader);

	// A lab field list for `rawobject` and `rawevent` world lines: the bits of a
	// ghost update or an event payload, written in order, for classes whose
	// layout is not read yet (TerrainBlock, WaterBlock, TerrainChunkEvent and the
	// like). The client skips what it does not read, since each update and event
	// carries its length. Tokens (see bin/docs/graal3d.md):
	//
	//   f0 f1                 a flag
	//   i<N>:<v>              an N-bit int (1..32), decimal, 0x hex or negative
	//   u8:<v> u16:<v> u32:<v> the same as i8, i16, i32
	//   f32:<v>               a raw 32-bit float
	//   str:<text>            a string (in the packet's string mode); %20 for a
	//                         space, %25 for %, - for the empty string
	//   hex:<bytes>           bytes, as hex
	//   file:<path>           a file's bytes
	//   zfile:<path>          a file's bytes, zlib-compressed (with the header)
	//
	// hex, file and zfile take an optional length prefix: `file@10:<path>` writes
	// the byte count as an int10 first.
	struct RawField
	{
		enum class Type
		{
			Flag,
			Int,
			F32,
			String,
			Bytes,
		};
		Type type = Type::Flag;
		int bits = 0;          // Int: its width; Bytes: the width of its length prefix (0: none)
		uint32_t value = 0;    // Flag, Int
		float f32 = 0.0f;
		std::string data;      // String: the text; Bytes: the bytes (after loadRawFiles)
		std::string file;      // Bytes from a file, read by loadRawFiles
		bool compress = false; // zlib the file's bytes

		bool operator==(const RawField&) const = default;
	};

	// Parses tokens; on a bad one returns nothing and says why in `error`.
	std::optional<std::vector<RawField>> parseRawFields(const std::vector<std::string_view>& tokens, std::string& error);

	// Writes the fields. A Bytes field whose file was not loaded writes nothing
	// but its length prefix (0).
	void writeRawFields(BitWriter& writer, const std::vector<RawField>& fields);

	// The payload's length in bits (strings as plain ones).
	size_t rawFieldBits(const std::vector<RawField>& fields);

	// Reads the file of every file and zfile field with `read` (empty optional:
	// cannot read it). Returns one error per file that could not be read or is
	// too long for its length prefix.
	std::vector<std::string> loadRawFiles(std::vector<RawField>& fields,
										  const std::function<std::optional<std::string>(const std::string&)>& read);

	// Streamed terrain for 5.x clients (Graal_analysis torque/terrain-512.md,
	// read from the real 5.12 client): a TerrainBlock ghost that carries only
	// settings, then up to 256 TerrainChunkEvents of 16 x 16 squares each.
	// Chunk (x, y) is the file heights<x>-<y>.dat in `folder`: zlib-compressed
	// chunk data, the same files the client keeps in its cache. The client
	// sends the stamp of every chunk it holds once the block arrives; the
	// server sends the chunks whose stamp differs. The client stamps its own
	// re-encoding of a chunk, so files written in that form (mode 2, ids
	// ascending) are not sent again (terrain lab, 2026-11-25).
	struct TerrainChunk
	{
		std::string packed;  // zlib bytes, at most 1023
		uint32_t stamp = 0;  // Torque calculateCRC of the inflated bytes
	};

	struct Terrain
	{
		std::string folder;
		uint32_t squareSize = 8;
		bool repeat = false;
		std::array<std::optional<TerrainChunk>, 256> chunks; // y * 16 + x, filled by loadTerrain
	};

	// Torque's calculateCRC: the CRC-32 table, starting at 0xFFFFFFFF, without
	// the final inversion.
	uint32_t terrainStamp(std::string_view plain);

	// The TerrainBlock update: init, the name "Terrain", 0, two empty strings,
	// 1.0, 0.01, 8, the square size, the repeat byte.
	std::vector<RawField> terrainBlockFields(const Terrain& terrain);

	// Reads heights<x>-<y>.dat from the terrain folder with `read`. A missing
	// file leaves its chunk empty. Returns one error per file that is too long
	// for the event's int10 length or does not inflate.
	std::vector<std::string> loadTerrain(Terrain& terrain,
										 const std::function<std::optional<std::string>(const std::string&)>& read);

	// The image extensions a 3D client drops from a texture name before it asks
	// for the file, and that the server tries, in this order, for a bare name.
	inline constexpr const char* kImageExtensions[] = {".png", ".jpg", ".jpeg", ".gif", ".bmp", ".tga", ".dds"};

	// A Sky's comma-separated texture list with any image extension dropped, so a
	// world file can name the real files ("sky_fr.png,..." becomes "sky_fr,...").
	std::string skyTextureNames(std::string_view list);

	// Checks a .3ds file's structure. Each of these crashed the real Linux 4.206
	// client (lab, 2026-11-25): no materials, a cut file, a chunk length past its
	// parent. Returns why the file is unsafe, or nothing.
	std::optional<std::string> unsafe3ds(std::string_view data);

	// Checks a .dts shape's header: it must be version 24, the only one the real
	// 5.12 draws, and the memory buffer it claims must fit the file.
	inline constexpr uint32_t kDtsVersion = 24;
	std::optional<std::string> unsafeDts(std::string_view data);

	// A PNG's width and height from its header, or nothing if it is not a PNG.
	std::optional<std::pair<uint32_t, uint32_t>> pngSize(std::string_view data);

	// Checks a PNG's header for a sane width and height; other images pass.
	inline constexpr uint32_t kMaxImageSide = 8192;
	// The largest side 5.12 drew; 2048 and 4096 drew white (live, 2026-11-25).
	inline constexpr uint32_t kMaxDrawnImageSide = 1024;
	std::optional<std::string> unsafeImage(std::string_view data);

	// unsafe3ds, unsafeDts or unsafeImage, by the file's extension.
	std::optional<std::string> unsafeAsset(std::string_view extension, std::string_view data);

	// A WaterBlock for 4.20r6 and 5.x (Graal_analysis torque/terrain-512.md
	// section 5, a static read of the real 5.12 client; not yet run live). The
	// client takes the scale only when x and y are both at least 1024.
	struct WaterSettings
	{
		std::array<float, 3> position{};
		std::array<float, 3> scale{1024.0f, 1024.0f, 10.0f};
		std::string surfaceTexture; // the others are left empty
		uint32_t liquidType = 0;    // stock TGE: 0 water, 1 ocean, 2 river, 3 stagnant, 4 lava ...
	};

	// The update: the name, the position with no rotation, the scale, the
	// textures, the liquid type, then stock TGE 1.x's default look.
	std::vector<RawField> waterBlockFields(const WaterSettings& water);

	// A DynamicShapeReplicator, DynamicGrass or DynamicGrassReplicator: ground
	// cover scattered around a point on a grid of 8-unit cells (Graal_analysis
	// torque/replicators-512.md, a static read of the 5.12 client; not yet seen
	// live). The rest of the update uses fixed lab defaults (see
	// replicatorFields).
	struct ReplicatorSettings
	{
		std::string cls = "DynamicShapeReplicator";
		std::array<float, 3> position{};
		uint32_t radius = 4;         // grid cells; the client clamps to 1-64
		uint32_t minPerCell = 1;     // the client clamps both to 0-20
		uint32_t maxPerCell = 3;
		std::string shapeFiles;
		uint32_t seed = 1;
		// The world-space x/y region cells may grow in: a cell grows only if its
		// corner is inside [min, max) (5.12, static read; z is not used there).
		// The default covers the whole world.
		std::array<int, 2> boxMin{-32767, -32767};
		std::array<int, 2> boxMax{32767, 32767};
		float scaleMin = 1.0f; // each copy is scaled by a random factor between these
		float scaleMax = 1.0f;
	};

	// The update: an empty GameObject update (13 cleared section flags), then the
	// replicator's part: position with no rotation, seed, shapes, radius, grid
	// counts, the box (z -32767..32767), the scale range, rotation 0 to 360
	// about z, no height offset, terrain only, slopes up to 90 degrees, no
	// alignment, no surface exclusion, shown.
	std::vector<RawField> replicatorFields(const ReplicatorSettings& replicator);

	// One object the server ghosts: its class and state.
	struct GhostObject
	{
		enum class Kind
		{
			GameObject,
			Sun,
			Camera, // `object` holds its name and transform
			Sky,
			TgeCamera, // protocol 19: a stock TGE Camera, `tgeCamera` and `datablockId`
			TgePlayer, // protocol 19: a stock TGE Player, `tgePlayer` and `datablockId`
			Raw,       // a lab object: `rawClass`, with `raw` as its update (rawobject)
		};

		Kind kind = Kind::GameObject;
		GameObjectState object;
		SunState sun;
		SkyState sky;
		tge::CameraState tgeCamera;
		tge::PlayerState tgePlayer;
		std::string tgeShape; // TgePlayer: its PlayerData's shape file
		std::string rawClass;
		std::vector<RawField> raw;
		// Raw objects built by the `terrain` and `water` world lines carry the 5.x
		// TerrainBlock and WaterBlock layouts: the service sends them only to
		// builds whose capabilities take those (Graal3DCapabilities.h).
		bool layout512 = false;
		uint32_t datablockId = tge::FirstDatablockId;

		// TgeCamera and TgePlayer masks: 1 = position and rotation, 2 = the
		// datablock (creates only).
		static constexpr uint32_t TgeMove = 1;
		static constexpr uint32_t TgeDatablock = 2;

		[[nodiscard]] std::string_view className() const
		{
			switch (kind)
			{
				case Kind::Sun: return "Sun";
				case Kind::Camera:
				case Kind::TgeCamera: return "Camera";
				case Kind::TgePlayer: return "Player";
				case Kind::Sky: return "Sky";
				case Kind::Raw: return rawClass;
				default: return "GameObject";
			}
		}
		// The protocol 19 kinds; the GameObject era ghosts every other one.
		[[nodiscard]] bool isTge() const { return kind == Kind::TgeCamera || kind == Kind::TgePlayer; }
		// Whether a client of that era takes this kind: a Sky goes to both, and a
		// raw object to any client whose class table has its class.
		[[nodiscard]] bool sentTo(bool tgeEra) const { return kind == Kind::Sky || kind == Kind::Raw || isTge() == tgeEra; }
		// The update for `mask` (ignored for Sun and Sky, which are always sent whole).
		// `controlled`: the receiving client controls this object (a TgePlayer then
		// leaves its move section out of updates after the first).
		void writeUpdate(BitWriter& writer, uint32_t mask, bool controlled = false) const;
		[[nodiscard]] uint32_t fullMask() const
		{
			if (isTge())
				return TgeMove | TgeDatablock;
			return kind == Kind::Sun || kind == Kind::Sky || kind == Kind::Raw ? 1u : gameObjectFullMask(object);
		}
		[[nodiscard]] uint32_t changesFrom(const GhostObject& before) const;
	};

	// What the server ghosts to every 3D client, from config/graal3dworld.txt.
	// One entry per line, words separated by spaces, `#` starts a comment:
	//
	//   object <name> <shape> <x> <y> <z> [<axis x> <axis y> <axis z> <angle> [<sx> <sy> <sz>]]
	//                             (the scale is section 2: lab 2026-11-25)
	//   sun <dir x> <dir y> <dir z> <r> <g> <b> <a> <ambient r> <g> <b> <a>
	//   playershape <shape>       the shape of every player's own object
	//   spawn <x> <y> <z>         where a player's object starts
	//   camera <x> <y> <z> [<axis x> <axis y> <axis z> <angle>]
	//                             where each client's Camera sits from the spawn
	//                             point (default 0 -5 2), and how it turns; without
	//                             a rotation it faces the spawn point
	//   sky <textures> [<visible distance> [<fog r> <g> <b>]]
	//                             a Sky; the textures are six names, comma
	//                             separated and without spaces ("-" for none)
	//   tgecontrol player|camera  the control object of a protocol 19 client
	//                             (3.153, 4.04r2): a stock TGE Player that walks
	//                             with the client's moves (the default), or the
	//                             Camera of the 2026-11-14 labs. A Player needs a
	//                             shape the client can load (tgeplayershape);
	//                             without one these clients get the Camera
	//   tgeplayershape <file>     the Player's shape, a TGE .dts (protocol 19)
	//   skyoptions <b1> <b2> <b3> <solid r> <g> <b>
	//                             the Sky's three unnamed bools (0/1) and solid colour
	//   weapon3d <name> [<file>]  a script the client runs in 3D mode, sent over the
	//                             3D text channel (addweapon, weaponscript): the
	//                             file's text, or without a file the server's
	//                             weapon <name> (its client bytecode, else its
	//                             clientside source). The 5.x client's 3D key
	//                             bindings call moveForward2, moveBackward2,
	//                             moveLeft2, moveRight2 and jump, which only a
	//                             script like this defines.
	//   rawobject <class> <fields...>
	//                             lab: ghost an object of <class> (TerrainBlock,
	//                             WaterBlock, ...) whose update is the raw fields
	//                             (see RawField), to every client whose class
	//                             table has the class
	//   rawevent [late] <class> <fields...>
	//                             lab: post one guaranteed event of <class>
	//                             (TerrainChunkEvent, ...) with the raw fields as
	//                             its payload, when the world is ghosted, or with
	//                             `late` once the client has its control object
	//   water <x> <y> <z> <sx> <sy> <sz> [<surface texture> [<liquid type>]]
	//                             a WaterBlock (see WaterSettings), for 4.20r6
	//                             and 5.x
	//   replicator <class> <x> <y> <z> <radius> <min> <max> <shapes>
	//              [<seed> [<x0> <y0> <x1> <y1>]] [scale=<min>[,<max>]]
	//                             lab: ground cover (see ReplicatorSettings) for
	//                             5.x; <class> is DynamicShapeReplicator,
	//                             DynamicGrass or DynamicGrassReplicator; the
	//                             box limits where it grows (default: anywhere)
	//   terrain <folder> [<square size>] [repeat]
	//                             streamed terrain for 4.20r6 and 5.x (see
	//                             Terrain): a TerrainBlock ghost, then the
	//                             chunks in <folder> (relative to the server
	//                             folder) as the client asks for them
	struct World
	{
		std::vector<GhostObject> objects; // the sun, if any, is one of them
		std::string playerShape;
		Transform spawn;
		std::optional<std::array<float, 3>> cameraOffset;
		std::optional<std::array<float, 4>> cameraRotation; // axis and angle (radians); none: face the spawn
		enum class TgeControl
		{
			Player,
			Camera,
		};
		TgeControl tgeControl = TgeControl::Player;
		std::string tgePlayerShape;
		std::vector<std::string> replicatorShapes; // every shape a replicator line names, for the start-up check
		struct Weapon3D
		{
			std::string name;
			std::string file; // empty: the server's weapon of that name
		};
		std::vector<Weapon3D> weapons;
		struct RawEvent
		{
			std::string className;
			std::vector<RawField> fields;
			bool late = false;
		};
		std::vector<RawEvent> rawEvents;
		std::optional<Terrain> terrain; // its TerrainBlock ghost is in `objects`, as a Raw object
		std::vector<std::string> errors;  // one per line that was not understood
	};

	World parseWorld(std::string_view text);

	// Where a protocol 21/24 client's Camera sits from the spawn point when the
	// world file has no camera line: behind it and above.
	inline constexpr std::array<float, 3> DefaultCameraOffset{0.0f, -5.0f, 2.0f};

	// At `position`, turned so its forward axis (+y, Torque's) points at
	// `target`: a turn about z, then a tilt about the turned x. The clients
	// turn the opposite way from the right-hand rule (lab 2026-11-20, 5.12 and
	// 4.20r6): a positive angle about +x tilts the view down, one about +z
	// turns it right. So the angle is negated.
	Transform facing(const std::array<float, 3>& position, const std::array<float, 3>& target);

	// Where the forward axis (+y) points after the transform's rotation, as
	// the clients turn it.
	std::array<float, 3> forwardOf(const Transform& t);

	// The Camera every protocol 21/24 client gets: the world's camera offset
	// (or DefaultCameraOffset) from the spawn, turned by the camera line's
	// rotation, or else facing the spawn point.
	Transform cameraTransform(const World& world);

} // namespace graal3d

#endif
