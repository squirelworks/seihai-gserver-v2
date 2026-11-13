// Modified by the Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
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
//   - one datablock, a CameraData with every optional field clear and no shape;
//   - one ghost, a Camera that uses it, which becomes the client's control object.
//
// A Camera is the smallest control object a TGE client takes: its update and its
// packet data have no shape or Player fields, so it needs nothing that is still
// unread (the Player and PlayerData layouts).

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

	// The server's game section (GameConnection::readPacket on the client).
	struct ServerControl
	{
		uint32_t serverTime = 0;
		uint32_t ghost = 0;
		CameraState camera;
	};

	void writeServerGame(BitWriter& writer, uint32_t lastMoveAck, const std::optional<ServerControl>& control);

} // namespace graal3d::tge

#endif
