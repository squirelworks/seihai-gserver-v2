// Modified by the Seihai project, 2026-11-13, 2026-11-19: see CHANGES-SEIHAI.md.
// New file: the protocol 19 game layer (Graal 3.153 and 4.04r2), the stock Torque
// Game Engine layer with Graal's changes, as far as a server needs it to take a
// client through the mission start to a control object.
//
// Source: a static read of the 4.04r2 Graal3DEngine.dll (its readers for each
// layout below), written up as behaviour in Graal_analysis torque/udp-session.md
// section 10 and torque/protocol19-server.md. Nothing here has been run against a
// real 3.153 or 4.04r2 client yet; the 3.153 Mac layouts are assumed to be the
// same as 4.04r2's (same Torque line, same net classes).
//
// What this server sends a protocol 19 client:
//
//   - one datablock: a PlayerData (or, with `tgecontrol camera` in the world file,
//     a CameraData), with no shape and stock TGE 1.x values;
//   - a Sky: the 4.04r2 client reads the scene's Sky on its first 3D frame
//     without a null check, so a world without one crashes it. Its update is the
//     same as 5.12's (read from 4.04r2's Sky::unpackUpdate);
//   - a ghost that uses the datablock, a Player (or a Camera), which becomes the
//     client's control object. The server applies the client's moves to a Player
//     and sends its packet data again whenever it moved.
//
// The Player, PlayerData and Player packet data layouts were read from the 4.04r2
// Graal3DEngine.dll on 2026-11-19 (PlayerData::unpackData 0x100a98b0,
// Player::unpackUpdate 0x100b3180, Player::readPacketData 0x100b2a70); they are
// stock TGE 1.x with Graal's md3 model name in PlayerData.

#ifndef SEIHAI_GRAAL3D_GRAAL3DTGE_H
#define SEIHAI_GRAAL3D_GRAAL3DTGE_H

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <graal3d/TorqueBitStream.h>

namespace graal3d::tge
{
	// Datablock ids start at 3 (DataBlockObjectIdFirst); the wire carries id - 3 in
	// 12 bits, in the SimDataBlockEvent and in a GameBase update.
	inline constexpr uint32_t FirstDatablockId = 3;
	inline constexpr int DatablockIdBits = 12;
	// This server's two datablocks. Only the one the control object uses is sent.
	inline constexpr uint32_t CameraDatablockId = FirstDatablockId;
	inline constexpr uint32_t PlayerDatablockId = FirstDatablockId + 1;

	// Connection messages (ConnectionMessageEvent: u32 sequence, int3 message,
	// int11 ghost count). The 4.04r2 client numbers them as TGE 1.x does: the five
	// NetConnection messages, then GameConnection's datablock pair at 5 and 6.
	// (Torque3D, which has a sixth NetConnection message, numbers that pair 6 and 7.)
	namespace message
	{
		inline constexpr uint32_t GhostAlwaysDone = 0;      // server: the ghost-always objects are out
		inline constexpr uint32_t ReadyForNormalGhosts = 1; // client: answer to 0, same sequence
		inline constexpr uint32_t EndGhosting = 2;
		inline constexpr uint32_t GhostAlwaysStarting = 3;  // server: count = the ghosts to come
		inline constexpr uint32_t SendNextDownloadRequest = 4;
		inline constexpr uint32_t DataBlocksDone = 5;         // server: every datablock is out
		inline constexpr uint32_t DataBlocksDownloadDone = 6; // client: they are loaded, same sequence
	} // namespace message

	// Camera modes (Camera::readPacketData reads them as a ranged int, 3 bits).
	// Orbit modes (3, 4) carry more fields; this server only sends the others.
	enum class CameraMode : uint32_t
	{
		Stationary = 0,
		FreeRotate = 1,
		Fly = 2,
		OrbitObject = 3,
		OrbitPoint = 4,
	};

	struct CameraState
	{
		std::array<float, 3> position{};
		float pitch = 0.0f; // rot.x, radians
		float yaw = 0.0f;   // rot.z, radians
		CameraMode mode = CameraMode::Fly;
	};

	// SimDataBlockEvent payload: int12 (id - 3), int12 datablock class id (the
	// server's numbering from the accept), int12 index, int13 total, then the
	// datablock's own data behind an int14 bit length.
	BitWriter simDataBlockEvent(uint32_t datablockId, uint32_t classId, uint32_t index, uint32_t total, const BitWriter& data);

	// ConnectionMessageEvent payload.
	BitWriter connectionMessageEvent(uint32_t sequence, uint32_t message, uint32_t ghostCount);

	// CameraData = ShapeBaseData's data with nothing of its own. Written with a
	// name and every optional field clear: no shape (the client then loads none),
	// no explosion or debris, no mounted images, the default masses and camera
	// distances.
	BitWriter cameraData(std::string_view name, bool packetStrings);

	// A Camera ghost update: GameBase (name, position, datablock), ShapeBase (its
	// main flag clear: no damage, images, threads or chat), then the Camera's own
	// flags. `create` adds the datablock id, which the first update must carry.
	void writeCameraUpdate(BitWriter& writer, const CameraState& camera, uint32_t datablockId, bool create);

	// The Camera's packet data, as the control object in the server's game
	// section: ShapeBase's energy and recharge rate (two floats), position,
	// rot.x, rot.z (five floats), the mode (int3).
	void writeCameraPacketData(BitWriter& writer, const CameraState& camera);
	std::optional<CameraState> readCameraPacketData(BitReader& reader);

	// PlayerData = ShapeBaseData, then (4.04r2 PlayerData::unpackData):
	//   flag renderFirstPerson; flag -> pstring md3 model name (Graal's);
	//   19 x f32: min/max look angle, max freelook angle, max time scale, max step
	//     height, run force, max forward/backward/side speed, the three underwater
	//     speeds, run surface angle, recover delay (an int sent as 4 bytes), recover
	//     run force scale, jump force, min/max jump speed, jump surface angle;
	//   int7 jump delay;
	//   17 x f32 (4 bytes each): swim force, horiz max/resist speed, resist factor,
	//     up max/resist speed, resist factor, splash velocity, angle, freq mod, vel
	//     epsilon, bubble emit time, medium/hard/exit splash sound velocity,
	//     footstep splash height, min impact speed;
	//   19 x (flag -> int12 audio profile id - 3);
	//   f32 x 3 bounding box;
	//   flag -> int12 foot puff emitter; u32 foot puff parts; f32 foot puff radius;
	//   flag -> int12 decal; f32 decal offset;
	//   flag -> int12 dust emitter; flag -> int12 splash;
	//   3 x (flag -> int12 splash emitter);
	//   9 x f32: ground impact min speed, shake freq (3), shake amp (3), shake
	//     duration, shake falloff.
	// Written with `shape` as its shape file, no sounds or effects and the 4.04r2
	// constructor's values. The shape must load: 4.04r2's Player::onNewDataBlock
	// (0x100ab580) fails without a shape instance, and the client then drops the
	// link with "Invalid packet GameBase::unpackUpdate()" (lab 2026-11-19).
	BitWriter playerData(std::string_view name, bool packetStrings, std::string_view shape);

	// A Sky material list a 4.04r2 client can draw: it reads entry 6 (the
	// environment map) with no null check on its first frame, so a list of six
	// sky-box textures or none crashes it (lab 2026-11-19). Shorter lists are
	// padded to seven entries with the first name, or with `fallback` when empty.
	// A name the client cannot find is enough.
	std::string tgeSkyMaterialList(std::string_view list, std::string_view fallback = "g3d_sky");

	// The two fields of PlayerData the server's walk uses.
	inline constexpr float PlayerMaxSpeed = 10.0f;         // max forward, backward and side speed
	inline constexpr float PlayerMaxLookAngle = 1.4f;      // max look angle (head.x limit)
	inline constexpr float PlayerMinLookAngle = -1.4f;
	inline constexpr float PlayerMaxFreelookAngle = 3.0f;  // head.z limit
	inline constexpr float TickSeconds = 0.032f;           // one move

	struct PlayerState
	{
		std::array<float, 3> position{};
		std::array<float, 3> velocity{};
		float rotZ = 0.0f;  // yaw, radians
		float headX = 0.0f; // pitch, radians
		float headZ = 0.0f; // free look yaw, radians
		uint32_t actionState = 0; // int3 (0: the move state)

		bool operator==(const PlayerState&) const = default;
	};

	// A Player ghost update (4.04r2 Player::unpackUpdate after GameBase and
	// ShapeBase):
	//   flag -> int5 impact sound; flag -> int3; flag -> action (int8, flag hold,
	//   flag at end, unless at end: flag -> signed float 6); flag -> u32;
	//   flag "controlled by this client": 1 ends the update;
	//   flag move -> { flag falling, flag, int3 state, flag -> int7 recover ticks,
	//     compressed point (scale 0.01; this server sends type 3, three raw f32),
	//     flag -> { normal vector (10), int13 speed x 32 }, float 7 rot.z / 2 pi,
	//     signed float 6 head.x / max look angle, signed float 6 head.z / max
	//     freelook angle, a Move, flag }.
	// Stock TGE leaves out the move section only for the client that controls the
	// object, and never on its first update, so `controlled` is ignored on create.
	void writePlayerUpdate(BitWriter& writer, const PlayerState& player, uint32_t datablockId, bool create, bool controlled);

	// The Player's packet data (4.04r2 Player::readPacketData): ShapeBase's energy
	// and recharge rate (f32 each), int3 action state, flag -> int7 recover ticks,
	// flag -> int7 jump delay, flag "not mounted" -> { position (3 f32), velocity
	// (3 f32), int4 ticks since jump surface contact }, head.x, head.z, rot.z (f32
	// each), flag -> { int12 ghost of an object it controls, that object's packet
	// data }. This server's Player is never mounted and controls nothing.
	void writePlayerPacketData(BitWriter& writer, const PlayerState& player);
	std::optional<PlayerState> readPlayerPacketData(BitReader& reader);

	// One Torque move as the 4.04r2 client packs it.
	struct Move
	{
		std::optional<uint32_t> yaw, pitch, roll; // int16 each, only when non-zero
		uint32_t x = 16, y = 16, z = 16;          // int6 each, 16 is "no movement"
		bool freeLook = false;
		std::array<bool, 10> triggers{};          // the flags after freeLook
	};

	Move readMove(BitReader& reader);
	void writeMove(BitWriter& writer, const Move& move);

	// Moves `player` by one move, as a plain walk: the move's yaw and pitch turn
	// the player and its head, x and y (16 is still, 0 and 32 are full speed)
	// set the velocity along the ground at PlayerMaxSpeed, and the position
	// advances by one tick. No gravity, collision or jumping: the server has no
	// terrain to stand on.
	void applyMove(PlayerState& player, const Move& move);

	// The client's game section (GameConnection::writePacket, 4.04r2), between the
	// rate fields and the events.
	struct ClientGame
	{
		bool firstPerson = false;
		std::array<float, 3> position{};
		std::optional<uint32_t> controlGhost;
		// Either the control object's own state (vehicles) ...
		bool sendsState = false;
		// ... or moves (a Camera or Player): the index of the first, then the moves.
		uint32_t firstMove = 0;
		std::vector<Move> moves;
		std::optional<uint32_t> fov;
	};

	// Reads the client's game section. A client that sends its control object's
	// state instead of moves is read as Camera packet data (the only control object
	// this server hands out); any other state has no length to skip by, so a
	// mismatch gives nullopt.
	std::optional<ClientGame> readClientGame(BitReader& reader);
	void writeClientGame(BitWriter& writer, const ClientGame& game);

	// The server's game section (GameConnection::readPacket on the client). The
	// control object's packet data is the Player's when `player` is set, else the
	// Camera's.
	struct ServerControl
	{
		uint32_t serverTime = 0;
		uint32_t ghost = 0;
		CameraState camera;
		std::optional<PlayerState> player;
	};

	void writeServerGame(BitWriter& writer, uint32_t lastMoveAck, const std::optional<ServerControl>& control);

} // namespace graal3d::tge

#endif
