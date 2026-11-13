// Modified by the Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
// New file: the protocol 19 game layer (3.153, 4.04r2). See Graal3DTge.h.

#include <graal3d/Graal3DTge.h>

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

	BitWriter cameraData(std::string_view name, bool packetStrings)
	{
		BitWriter w;
		w.setPacketStrings(packetStrings);

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
		return w;
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
			// The only control object this server hands out is a Camera, and a
			// Camera client sends moves; read the state as Camera data anyway.
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
			writeCameraPacketData(w, control->camera);
		}
		w.writeFlag(false); // camera: none (no camera clears it; the control object is the view)
		w.writeFlag(false); // field of view: unchanged
	}

} // namespace graal3d::tge
