// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: the ghosted objects of a Graal 3D world in the GameObject era (4.20r6
// and 5.x): GameObject, the Graal class every 3D thing is, and Sun. Their update
// layouts come from a static read of the 5.00r7 Graal3DEngine.dll (protocol 24,
// the same as 5.12): GameObject::packUpdate / unpackUpdate, the movement objects
// and Sun::packUpdate. Nothing here has been run against a real client yet.
//
// A GameObject update is thirteen sections, each one flag and then its payload:
//
//   1 name        string                       8 shape data     int14 length + data
//   2 velocity    3 floats                     9 chat           string
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
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <graal3d/TorqueBitStream.h>

namespace graal3d
{
	// GameObject dirty bits (the server's masks; the wire only has the flags).
	namespace GameObjectBits
	{
		inline constexpr uint32_t Name = 0x1;
		inline constexpr uint32_t Velocity = 0x2;
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
		std::array<float, 3> velocity{};
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

	// Sky, from the 5.12 Sky::packUpdate (0x100aacf0). The server sends it whole:
	// the init part (mask 1) and the material list (mask 0x80); the later
	// sections (storm, wind and fog changes) are left clear. Floats are raw 32-bit
	// values, the bools whole bytes. Field names come from the GS2 properties the
	// client registers for Sky; where the dump does not tie a value to a name, the
	// name here is a guess and the comment says so.
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

	// One object the server ghosts: its class and state.
	struct GhostObject
	{
		enum class Kind
		{
			GameObject,
			Sun,
			Camera, // `object` holds its name and transform
			Sky,
		};

		Kind kind = Kind::GameObject;
		GameObjectState object;
		SunState sun;
		SkyState sky;

		[[nodiscard]] std::string_view className() const
		{
			return kind == Kind::Sun ? "Sun" : kind == Kind::Camera ? "Camera" : kind == Kind::Sky ? "Sky" : "GameObject";
		}
		// The update for `mask` (ignored for Sun and Sky, which are always sent whole).
		void writeUpdate(BitWriter& writer, uint32_t mask) const;
		[[nodiscard]] uint32_t fullMask() const
		{
			return kind == Kind::Sun || kind == Kind::Sky ? 1u : gameObjectFullMask(object);
		}
		[[nodiscard]] uint32_t changesFrom(const GhostObject& before) const;
	};

	// What the server ghosts to every 3D client, from config/graal3dworld.txt.
	// One entry per line, words separated by spaces, `#` starts a comment:
	//
	//   object <name> <shape> <x> <y> <z> [<axis x> <axis y> <axis z> <angle>]
	//   sun <dir x> <dir y> <dir z> <r> <g> <b> <a> <ambient r> <g> <b> <a>
	//   playershape <shape>       the shape of every player's own object
	//   spawn <x> <y> <z>         where a player's object starts
	//   camera <x> <y> <z>        give each client a Camera, placed this far from
	//                             the spawn point (off without this line)
	//   sky <textures> [<visible distance> [<fog r> <g> <b>]]
	//                             a Sky; the textures are six names, comma
	//                             separated and without spaces ("-" for none)
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
	struct World
	{
		std::vector<GhostObject> objects; // the sun, if any, is one of them
		std::string playerShape;
		Transform spawn;
		std::optional<std::array<float, 3>> cameraOffset;
		struct Weapon3D
		{
			std::string name;
			std::string file; // empty: the server's weapon of that name
		};
		std::vector<Weapon3D> weapons;
		std::vector<std::string> errors;  // one per line that was not understood
	};

	World parseWorld(std::string_view text);

} // namespace graal3d

#endif
