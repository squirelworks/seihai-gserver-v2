// Modified by the Seihai project, 2026-11-13, 2026-11-19: see CHANGES-SEIHAI.md.
// New file: the protocol 19 game layer (3.153, 4.04r2). See Graal3DTge.h.

#include <graal3d/Graal3DTge.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace graal3d::tge
{
	BitWriter simDataBlockEvent(uint32_t datablockId, uint32_t classId, uint32_t index, uint32_t total, const BitWriter& data)
	{
		BitWriter w;
		w.writeInt(datablockId - FirstDatablockId, DatablockIdBits);
		w.writeInt(classId, 12);
		w.writeInt(index, 12);
		w.writeInt(total, 13);
		w.writeInt(static_cast<uint32_t>(data.bitPosition()), 14);
		w.writeBits(data);
		return w;
	}

	BitWriter connectionMessageEvent(uint32_t sequence, uint32_t message, uint32_t ghostCount)
	{
		BitWriter w;
		w.writeU32(sequence);
		w.writeInt(message, 3);
		w.writeInt(ghostCount, 11);
		return w;
	}

	namespace
	{
		constexpr float TwoPi = 2.0f * std::numbers::pi_v<float>;

		// SimDataBlock, GameBaseData and ShapeBaseData, as CameraData and
		// PlayerData both start.
		void writeShapeBaseData(BitWriter& w, std::string_view name)
		{
			// SimDataBlock: the datablock's name.
			w.writeString(name);
			// GameBaseData: two strings (category and class name in the 4.04r2 client).
			w.writeString("");
			w.writeString("");

			// ShapeBaseData.
			w.writeFlag(false);       // shape CRC check: off (else u32 CRC)
			w.writeString("");        // shape file: none, so the client loads no shape
			for (int i = 0; i < 9; ++i)
				w.writeFlag(false);   // mass, drag, density, max energy, camera distances and fov: defaults (each flag -> f32)
			w.writeString("");        // a second file name (cloak texture): none
			w.writeFlag(false);       // a bool
			w.writeFlag(false);       // debris datablock (flag -> int12 id - 3)
			w.writeFlag(false);       // three bools
			w.writeFlag(false);
			w.writeFlag(false);
			w.writeFlag(false);       // explosion datablock (flag -> int12 id - 3)
			w.writeFlag(false);       // underwater explosion datablock (flag -> int12 id - 3)
			w.writeFlag(false);       // a bool
			w.writeFlag(false);       // mounted images: none (each is flag 1 + its data, up to 8)
			w.writeFlag(false);       // two bools
			w.writeFlag(false);
			w.writeU32(0);            // a 32-bit field
			w.writeFlag(false);       // a bool
			w.writeF32(0.0f);         // count of the per-sequence lists that follow: none
			w.writeFlag(false);       // second list present (read only with a count)
		}

		// Torque's writeFloat / writeSignedFloat: the value scaled to the int's
		// range and rounded (the 4.04r2 readers divide by 2^bits - 1).
		void writeUnitFloat(BitWriter& w, float value, int bits)
		{
			const float max = static_cast<float>((1u << bits) - 1);
			w.writeInt(static_cast<uint32_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * max)), bits);
		}

		void writeSignedUnitFloat(BitWriter& w, float value, int bits)
		{
			writeUnitFloat(w, (std::clamp(value, -1.0f, 1.0f) + 1.0f) * 0.5f, bits);
		}

		float wrapAngle(float a)
		{
			a = std::fmod(a, TwoPi);
			return a < 0.0f ? a + TwoPi : a;
		}
	} // namespace

	BitWriter cameraData(std::string_view name, bool packetStrings)
	{
		BitWriter w;
		w.setPacketStrings(packetStrings);
		writeShapeBaseData(w, name);
		return w;
	}

	BitWriter playerData(std::string_view name, bool packetStrings)
	{
		BitWriter w;
		w.setPacketStrings(packetStrings);
		writeShapeBaseData(w, name);

		w.writeFlag(true);  // renderFirstPerson
		w.writeFlag(false); // md3 model: none

		// The 4.04r2 PlayerData constructor's values.
		const float first[] = {
			PlayerMinLookAngle, PlayerMaxLookAngle, PlayerMaxFreelookAngle, // min and max look angle, max freelook angle
			1.5f,  // max time scale
			1.0f,  // max step height
			360.0f, // run force
			PlayerMaxSpeed, PlayerMaxSpeed, PlayerMaxSpeed, // max forward, backward, side speed
			PlayerMaxSpeed, PlayerMaxSpeed, PlayerMaxSpeed, // the same underwater
			80.0f, // run surface angle
		};
		for (auto v : first)
			w.writeF32(v);
		w.writeU32(30);      // recover delay (ticks)
		w.writeF32(1.0f);    // recover run force scale
		w.writeF32(75.0f);   // jump force
		w.writeF32(500.0f);  // min jump speed
		w.writeF32(1000.0f); // max jump speed
		w.writeF32(78.0f);   // jump surface angle
		w.writeInt(30, 7);   // jump delay (ticks)

		const float second[] = {
			75.0f,                      // swim force
			80.0f, 38.0f, 1.0f,         // horiz max speed, resist speed, resist factor
			80.0f, 38.0f, 1.0f,         // up max speed, resist speed, resist factor
			1.0f, 45.0f, 300.0f, 0.25f, // splash velocity, angle, freq mod, vel epsilon
			0.4f,                       // bubble emit time
			2.0f, 3.0f, 2.0f,           // medium, hard, exit splash sound velocity
			0.1f,                       // footstep splash height
			25.0f,                      // min impact speed
		};
		for (auto v : second)
			w.writeF32(v);

		for (int i = 0; i < 19; ++i)
			w.writeFlag(false); // sounds: none

		w.writeF32(1.0f); // bounding box
		w.writeF32(1.0f);
		w.writeF32(2.3f);

		w.writeFlag(false); // foot puff emitter: none
		w.writeU32(15);     // foot puff parts
		w.writeF32(0.25f);  // foot puff radius
		w.writeFlag(false); // decal: none
		w.writeF32(0.0f);   // decal offset
		w.writeFlag(false); // dust emitter: none
		w.writeFlag(false); // splash: none
		for (int i = 0; i < 3; ++i)
			w.writeFlag(false); // splash emitters: none

		const float impact[] = {10.0f, 10.0f, 10.0f, 10.0f, 20.0f, 20.0f, 20.0f, 1.0f, 10.0f};
		for (auto v : impact)
			w.writeF32(v);
		return w;
	}

	void writePlayerUpdate(BitWriter& w, const PlayerState& player, uint32_t datablockId, bool create, bool controlled)
	{
		// GameBase.
		w.writeFlag(false); // object name: none
		w.writeFlag(true);  // position
		for (auto v : player.position)
			w.writeF32(v);
		if (w.writeFlag(create))
			w.writeInt(datablockId - FirstDatablockId, DatablockIdBits);

		// ShapeBase: its main flag clear ends ShapeBase's part.
		w.writeFlag(false);

		// Player.
		w.writeFlag(false); // impact sound
		w.writeFlag(false); // the int3 field
		w.writeFlag(false); // action
		w.writeFlag(false); // the u32 field
		if (w.writeFlag(controlled && !create))
			return; // the client predicts it from its own moves and the packet data

		w.writeFlag(true);  // move section
		w.writeFlag(false); // falling
		w.writeFlag(false);
		w.writeInt(player.actionState & 7u, 3);
		w.writeFlag(false); // recover ticks
		w.writeInt(3, 2);   // compressed point type 3: the position as three raw floats
		for (auto v : player.position)
			w.writeF32(v);
		w.writeFlag(false); // velocity: none here (the packet data carries it)
		writeUnitFloat(w, wrapAngle(player.rotZ) / TwoPi, 7);
		writeSignedUnitFloat(w, player.headX / PlayerMaxLookAngle, 6);
		writeSignedUnitFloat(w, player.headZ / PlayerMaxFreelookAngle, 6);
		writeMove(w, Move{});
		w.writeFlag(false);
	}

	void writePlayerPacketData(BitWriter& w, const PlayerState& player)
	{
		w.writeF32(0.0f); // ShapeBase: energy
		w.writeF32(0.0f); // ShapeBase: recharge rate
		w.writeInt(player.actionState & 7u, 3);
		w.writeFlag(false); // recover ticks
		w.writeFlag(false); // jump delay
		w.writeFlag(true);  // not mounted
		for (auto v : player.position)
			w.writeF32(v);
		for (auto v : player.velocity)
			w.writeF32(v);
		w.writeInt(15, 4);  // ticks since it touched a jump surface (15: long ago)
		w.writeF32(player.headX);
		w.writeF32(player.headZ);
		w.writeF32(player.rotZ);
		w.writeFlag(false); // controls no other object
	}

	std::optional<PlayerState> readPlayerPacketData(BitReader& r)
	{
		PlayerState p;
		r.readF32();
		r.readF32();
		p.actionState = r.readInt(3);
		if (r.readFlag())
			r.readInt(7);
		if (r.readFlag())
			r.readInt(7);
		if (r.readFlag())
		{
			for (auto& v : p.position)
				v = r.readF32();
			for (auto& v : p.velocity)
				v = r.readF32();
			r.readInt(4);
		}
		p.headX = r.readF32();
		p.headZ = r.readF32();
		p.rotZ = r.readF32();
		if (r.readFlag())
			return std::nullopt; // a controlled object's packet data: not read here
		if (r.overrun())
			return std::nullopt;
		return p;
	}

	void applyMove(PlayerState& p, const Move& m)
	{
		// Move::unclamp in 4.04r2: angles are signed 16-bit fractions of a turn,
		// x, y and z are (value - 16) / 16.
		const auto angle = [](const std::optional<uint32_t>& v) {
			return v ? static_cast<float>(static_cast<int16_t>(static_cast<uint16_t>(*v))) * (TwoPi / 65536.0f) : 0.0f;
		};
		p.rotZ = wrapAngle(p.rotZ + angle(m.yaw));
		p.headX = std::clamp(p.headX + angle(m.pitch), PlayerMinLookAngle, PlayerMaxLookAngle);

		const float x = std::clamp((static_cast<float>(m.x) - 16.0f) / 16.0f, -1.0f, 1.0f);
		const float y = std::clamp((static_cast<float>(m.y) - 16.0f) / 16.0f, -1.0f, 1.0f);
		// Torque's z rotation: forward (0, 1) turns to (sin, cos), right (1, 0) to (cos, -sin).
		const float s = std::sin(p.rotZ);
		const float c = std::cos(p.rotZ);
		p.velocity = {(x * c + y * s) * PlayerMaxSpeed, (-x * s + y * c) * PlayerMaxSpeed, 0.0f};
		p.position[0] += p.velocity[0] * TickSeconds;
		p.position[1] += p.velocity[1] * TickSeconds;
	}

	void writeCameraUpdate(BitWriter& w, const CameraState& camera, uint32_t datablockId, bool create)
	{
		// GameBase.
		w.writeFlag(false); // object name: none
		w.writeFlag(true);  // position
		for (auto v : camera.position)
			w.writeF32(v);
		if (w.writeFlag(create))
			w.writeInt(datablockId - FirstDatablockId, DatablockIdBits);

		// ShapeBase: its main flag clear ends ShapeBase's part.
		w.writeFlag(false);

		// Camera: "the client controls it" clear, then the move section.
		w.writeFlag(false);
		w.writeFlag(true);
		for (auto v : camera.position)
			w.writeF32(v);
		w.writeF32(camera.pitch);
		w.writeF32(camera.yaw);
	}

	void writeCameraPacketData(BitWriter& w, const CameraState& camera)
	{
		w.writeF32(0.0f); // ShapeBase: energy
		w.writeF32(0.0f); // ShapeBase: recharge rate
		for (auto v : camera.position)
			w.writeF32(v);
		w.writeF32(camera.pitch);
		w.writeF32(camera.yaw);
		const auto mode = camera.mode == CameraMode::OrbitObject || camera.mode == CameraMode::OrbitPoint ? CameraMode::Fly : camera.mode;
		w.writeInt(static_cast<uint32_t>(mode), 3);
	}

	std::optional<CameraState> readCameraPacketData(BitReader& r)
	{
		CameraState c;
		r.readF32();
		r.readF32();
		for (auto& v : c.position)
			v = r.readF32();
		c.pitch = r.readF32();
		c.yaw = r.readF32();
		const auto mode = r.readInt(3);
		if (mode > 4)
			return std::nullopt;
		c.mode = static_cast<CameraMode>(mode);
		if (c.mode == CameraMode::OrbitObject || c.mode == CameraMode::OrbitPoint)
		{
			// Minimum, maximum and current orbit distance.
			r.readF32();
			r.readF32();
			r.readF32();
			if (c.mode == CameraMode::OrbitObject)
			{
				r.readFlag();
				r.readInt(12);
			}
			else
				return std::nullopt; // a compressed point: not read here
		}
		if (r.overrun())
			return std::nullopt;
		return c;
	}

	Move readMove(BitReader& r)
	{
		Move m;
		if (r.readFlag())
			m.yaw = r.readInt(16);
		if (r.readFlag())
			m.pitch = r.readInt(16);
		if (r.readFlag())
			m.roll = r.readInt(16);
		m.x = r.readInt(6);
		m.y = r.readInt(6);
		m.z = r.readInt(6);
		m.freeLook = r.readFlag();
		for (auto& t : m.triggers)
			t = r.readFlag();
		return m;
	}

	void writeMove(BitWriter& w, const Move& m)
	{
		if (w.writeFlag(m.yaw.has_value()))
			w.writeInt(*m.yaw, 16);
		if (w.writeFlag(m.pitch.has_value()))
			w.writeInt(*m.pitch, 16);
		if (w.writeFlag(m.roll.has_value()))
			w.writeInt(*m.roll, 16);
		w.writeInt(m.x, 6);
		w.writeInt(m.y, 6);
		w.writeInt(m.z, 6);
		w.writeFlag(m.freeLook);
		for (auto t : m.triggers)
			w.writeFlag(t);
	}

	std::optional<ClientGame> readClientGame(BitReader& r)
	{
		ClientGame g;
		g.firstPerson = r.readFlag();
		for (auto& v : g.position)
			v = r.readF32();
		if (r.readFlag())
			g.controlGhost = r.readInt(12);
		g.sendsState = r.readFlag();
		if (g.sendsState)
		{
			// Only a control object with the vehicle type bit sends its state; the
			// Camera and the Player this server hands out send moves. Read the state
			// as Camera data anyway.
			if (!readCameraPacketData(r))
				return std::nullopt;
		}
		else
		{
			g.firstMove = r.readU32();
			const auto count = r.readInt(5);
			for (uint32_t i = 0; i < count && !r.overrun(); ++i)
				g.moves.push_back(readMove(r));
		}
		if (r.readFlag())
			g.fov = r.readInt(8);
		if (r.overrun())
			return std::nullopt;
		return g;
	}

	void writeClientGame(BitWriter& w, const ClientGame& g)
	{
		w.writeFlag(g.firstPerson);
		for (auto v : g.position)
			w.writeF32(v);
		if (w.writeFlag(g.controlGhost.has_value()))
			w.writeInt(*g.controlGhost, 12);
		w.writeFlag(false);
		w.writeU32(g.firstMove);
		w.writeInt(static_cast<uint32_t>(g.moves.size()), 5);
		for (const auto& m : g.moves)
			writeMove(w, m);
		if (w.writeFlag(g.fov.has_value()))
			w.writeInt(*g.fov, 8);
	}

	void writeServerGame(BitWriter& w, uint32_t lastMoveAck, const std::optional<ServerControl>& control)
	{
		w.writeU32(lastMoveAck);
		w.writeFlag(false); // no damage flash or white-out
		// Control: flag, then "dirty" with the server time, ghost and packet data.
		// (Control set but not dirty would be followed by a compression point.)
		if (w.writeFlag(control.has_value()))
		{
			w.writeFlag(true);
			w.writeU32(control->serverTime);
			w.writeInt(control->ghost, 12);
			if (control->player)
				writePlayerPacketData(w, *control->player);
			else
				writeCameraPacketData(w, control->camera);
		}
		w.writeFlag(false); // camera: none (no camera clears it; the control object is the view)
		w.writeFlag(false); // field of view: unchanged
	}

} // namespace graal3d::tge
