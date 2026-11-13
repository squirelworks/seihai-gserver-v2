// Modified by the Seihai project, 2026-11-13: see CHANGES-SEIHAI.md.
// New file: tests for the protocol 19 (3.153, 4.04r2) game layer. The client side
// here is written from the 4.04r2 client's readers as Graal_analysis
// torque/protocol19-server.md describes them, not from the server's writers, so a
// layout the two disagree on fails here. No real protocol 19 client has run
// against this yet.
#include "catch2/catch_all.hpp"

#include <optional>
#include <string>
#include <vector>

#include <graal3d/Graal3DConnection.h>
#include <graal3d/Graal3DObjects.h>
#include <graal3d/Graal3DProtocol.h>
#include <graal3d/Graal3DTge.h>
#include <graal3d/TorqueBitStream.h>

using namespace graal3d;

namespace
{
	struct Event19
	{
		std::string name;
		std::string payload;
		size_t bits = 0;
	};

	struct Ghost19
	{
		uint32_t index = 0;
		bool deleted = false;
		uint32_t classId = 0;
		std::string payload;
		size_t bits = 0;
	};

	struct Camera19
	{
		std::array<float, 3> position{};
		float rotX = 0, rotZ = 0;
		uint32_t mode = 0;
	};

	struct Packet19
	{
		PacketHeader header;
		uint32_t moveAck = 0;
		bool control = false;
		uint32_t serverTime = 0;
		uint32_t controlGhost = 0;
		Camera19 controlData;
		std::vector<Event19> events;
		std::vector<Ghost19> ghosts;
	};

	std::string copyBits(BitReader& r, size_t bits)
	{
		BitWriter copy;
		for (size_t i = 0; i < bits; ++i)
			copy.writeFlag(r.readFlag());
		return copy.toString();
	}

	// Camera::readPacketData on the client: ShapeBase's two floats, position,
	// rot.x, rot.z, the mode (int3), orbit fields for modes 3 and 4.
	Camera19 readCameraPacket(BitReader& r)
	{
		Camera19 c;
		r.readF32();
		r.readF32();
		for (auto& v : c.position)
			v = r.readF32();
		c.rotX = r.readF32();
		c.rotZ = r.readF32();
		c.mode = r.readInt(3);
		REQUIRE(c.mode <= 2); // this server sends no orbit modes
		return c;
	}

	// GameConnection::readPacket on the 4.04r2 client, then NetConnection's
	// events and ghosts.
	Packet19 readServerPacket(const Profile& profile, std::string_view datagram)
	{
		BitReader r(datagram);
		std::string buffer;
		r.setStringBuffer(&buffer);
		Packet19 out;
		auto h = readPacketHeader(r);
		REQUIRE(h);
		out.header = *h;
		if (h->type != PacketType::Data)
			return out;

		REQUIRE_FALSE(r.readFlag()); // rate
		REQUIRE_FALSE(r.readFlag()); // max rate
		out.moveAck = r.readU32();
		if (r.readFlag())
		{
			if (r.readFlag())
				r.readInt(7); // damage flash
			if (r.readFlag())
				r.readInt(7); // white-out
		}
		if (r.readFlag())
		{
			REQUIRE(r.readFlag()); // this server never sends a bare compression point
			out.control = true;
			out.serverTime = r.readU32();
			out.controlGhost = r.readInt(12);
			out.controlData = readCameraPacket(r);
		}
		REQUIRE_FALSE(r.readFlag()); // camera
		REQUIRE_FALSE(r.readFlag()); // fov

		// Unguaranteed events, then guaranteed-ordered ones.
		while (r.readFlag())
		{
			Event19 ev;
			ev.name = std::string(eventClassName(profile, r.readInt(12)));
			ev.bits = r.readInt(14);
			ev.payload = copyBits(r, ev.bits);
			out.events.push_back(std::move(ev));
		}
		while (r.readFlag())
		{
			if (!r.readFlag())
				r.readInt(7);
			Event19 ev;
			ev.name = std::string(eventClassName(profile, r.readInt(12)));
			ev.bits = r.readInt(14);
			ev.payload = copyBits(r, ev.bits);
			out.events.push_back(std::move(ev));
		}
		if (r.readFlag())
		{
			const auto width = r.readInt(4);
			while (r.readFlag())
			{
				Ghost19 g;
				g.index = r.readInt(static_cast<int>(width));
				g.deleted = r.readFlag();
				if (!g.deleted)
				{
					g.classId = r.readInt(12);
					g.bits = r.readInt(14);
					g.payload = copyBits(r, g.bits);
				}
				out.ghosts.push_back(std::move(g));
			}
		}
		REQUIRE_FALSE(r.overrun());
		REQUIRE(r.bitsLeft() < 8);
		return out;
	}

	// ShapeBaseData::unpackData as the 4.04r2 client reads it (CameraData adds
	// nothing), every optional branch included.
	struct CameraData19
	{
		std::string name;
		std::string shape;
		bool fits = false;
	};

	CameraData19 readCameraData(const std::string& payload, size_t bits)
	{
		BitReader r(payload);
		std::string buffer;
		r.setStringBuffer(&buffer);
		CameraData19 d;
		d.name = r.readString();      // SimDataBlock
		r.readString();               // GameBaseData
		r.readString();
		if (r.readFlag())             // shape CRC
			r.readU32();
		d.shape = r.readString();
		for (int i = 0; i < 9; ++i)
		{
			if (r.readFlag())
				r.readF32();
		}
		r.readString();
		r.readFlag();
		if (r.readFlag())
			r.readInt(12);            // debris
		r.readFlag();
		r.readFlag();
		r.readFlag();
		if (r.readFlag())
			r.readInt(12);            // explosion
		if (r.readFlag())
			r.readInt(12);            // underwater explosion
		r.readFlag();
		for (int i = 0; i < 8 && r.readFlag(); ++i)
			FAIL("mounted image data is not modelled");
		r.readFlag();
		r.readFlag();
		r.readU32();
		r.readFlag();
		const float count = r.readF32();
		REQUIRE(count == 0.0f);
		r.readFlag();
		d.fits = !r.overrun() && r.bitPosition() == bits;
		return d;
	}

	// GameBase, ShapeBase and Camera unpackUpdate as the client reads them.
	struct CameraGhost19
	{
		std::optional<uint32_t> datablock;
		std::optional<std::array<float, 3>> position;
		bool clientControlled = false;
		std::optional<Camera19> move;
		bool fits = false;
	};

	CameraGhost19 readCameraGhost(const std::string& payload, size_t bits)
	{
		BitReader r(payload);
		std::string buffer;
		r.setStringBuffer(&buffer);
		CameraGhost19 g;
		if (r.readFlag())
			r.readString(); // object name
		if (r.readFlag())
		{
			std::array<float, 3> p{};
			for (auto& v : p)
				v = r.readF32();
			g.position = p;
		}
		if (r.readFlag())
			g.datablock = r.readInt(12) + 3;
		REQUIRE_FALSE(r.readFlag()); // ShapeBase's main flag: no damage, images, threads or chat
		g.clientControlled = r.readFlag();
		if (!g.clientControlled && r.readFlag())
		{
			Camera19 c;
			for (auto& v : c.position)
				v = r.readF32();
			c.rotX = r.readF32();
			c.rotZ = r.readF32();
			g.move = c;
		}
		g.fits = !r.overrun() && r.bitPosition() == bits;
		return g;
	}

	struct OutEvent19
	{
		std::string className;
		BitWriter payload;
	};

	// A 4.04r2 client data packet: its game section (moves), then guaranteed events.
	std::string clientPacket(const Profile& profile, uint32_t seq, uint32_t highestAck, uint32_t ackMask, const tge::ClientGame& game,
							 const std::vector<OutEvent19>& events, int32_t firstEventSeq)
	{
		BitWriter w;
		PacketHeader h;
		h.sequence = seq;
		h.highestAck = highestAck;
		h.ackByteCount = 1;
		h.ackMask = ackMask;
		writePacketHeader(w, h);
		w.writeFlag(false);
		w.writeFlag(false);
		tge::writeClientGame(w, game);
		w.writeFlag(false); // no unguaranteed events
		int32_t prev = -2;
		int32_t seqNo = firstEventSeq;
		for (const auto& ev : events)
		{
			w.writeFlag(true);
			if (!w.writeFlag(seqNo == prev + 1))
				w.writeInt(static_cast<uint32_t>(seqNo) & 0x7F, 7);
			prev = seqNo++;
			w.writeInt(*eventClassId(profile, ev.className), 12);
			w.writeInt(static_cast<uint32_t>(ev.payload.bitPosition()), 14);
			w.writeBits(ev.payload);
		}
		w.writeFlag(false);
		w.writeFlag(false); // no ghosts
		return w.toString();
	}

	BitWriter commandEvent(std::vector<std::string> args)
	{
		std::vector<CommandArg> packed;
		for (auto& a : args)
			packed.push_back(CommandArg{false, 0, a});
		return remoteCommandEvent(packed, true);
	}

	// The commands in a packet, with their arguments, tags resolved.
	std::vector<std::vector<std::string>> commandsIn(const Packet19& p, std::array<std::string, 32>& strings)
	{
		std::vector<std::vector<std::string>> out;
		for (const auto& ev : p.events)
		{
			BitReader r(ev.payload);
			std::string buffer;
			r.setStringBuffer(&buffer);
			if (ev.name == "NetStringEvent")
			{
				const auto id = r.readInt(StringTableBits);
				strings[id] = r.readString();
			}
			else if (ev.name == "RemoteCommandEvent")
			{
				std::vector<std::string> args;
				const auto argc = r.readInt(CommandArgsBits);
				for (uint32_t i = 0; i < argc; ++i)
				{
					auto a = unpackCommandArg(r);
					args.push_back(a.isTag ? strings[a.tag] : a.text);
				}
				out.push_back(std::move(args));
			}
		}
		return out;
	}

	struct ConnMsg
	{
		uint32_t sequence, message, count;
	};

	std::vector<ConnMsg> messagesIn(const Packet19& p)
	{
		std::vector<ConnMsg> out;
		for (const auto& ev : p.events)
		{
			if (ev.name != "ConnectionMessageEvent")
				continue;
			REQUIRE(ev.bits == 32 + 3 + 11);
			BitReader r(ev.payload);
			ConnMsg m;
			m.sequence = r.readU32();
			m.message = r.readInt(3);
			m.count = r.readInt(11);
			out.push_back(m);
		}
		return out;
	}

	// Drives one server connection as a 4.04r2 (bare acks) or 3.153 (acks with
	// the sequence) client would.
	struct Client19
	{
		const Profile& profile = *profileForProtocol(19);
		uint64_t now = 0;
		std::vector<std::string> sent;
		std::vector<std::string> logs;
		Connection conn{profile, 0x400, 0, Connection::Callbacks{[this](std::string_view d) { sent.emplace_back(d); }, nullptr, [this](std::string_view t) { logs.emplace_back(t); }, nullptr, nullptr}};
		uint32_t seq = 0;
		uint32_t lastRecvd = 0;
		uint32_t ackMask = 0;
		int32_t nextEventSeq = 0;
		std::array<std::string, 32> strings{};

		void send(const std::vector<OutEvent19>& events, const tge::ClientGame& game = {})
		{
			++seq;
			conn.receive(clientPacket(profile, seq, lastRecvd, ackMask, game, events, nextEventSeq), now);
			nextEventSeq += static_cast<int32_t>(events.size());
		}

		// Runs the server, reads what it sent, and acks every data packet.
		std::vector<Packet19> pump()
		{
			now += 100;
			conn.update(now);
			std::vector<Packet19> out;
			for (const auto& d : sent)
			{
				auto p = readServerPacket(profile, d);
				if (p.header.type == PacketType::Data)
				{
					ackMask = (ackMask << (p.header.sequence - lastRecvd)) | 1u;
					lastRecvd = p.header.sequence;
				}
				out.push_back(std::move(p));
			}
			sent.clear();
			return out;
		}
	};

	const Packet19* firstData(const std::vector<Packet19>& packets)
	{
		for (const auto& p : packets)
		{
			if (p.header.type == PacketType::Data)
				return &p;
		}
		return nullptr;
	}
} // namespace

TEST_CASE("protocol 19 accept lists Camera, CameraData and the events with three class types", "[graal3d][tge]")
{
	const auto* profile = profileForProtocol(19);
	REQUIRE(profile);
	REQUIRE(profile->tgePackets);
	const auto accept = buildConnectAccept(0x400, *profile);
	BitReader r(accept);
	REQUIRE(r.readU8() == static_cast<uint8_t>(InfoPacket::ConnectAccept));
	REQUIRE(r.readU32() == 0x400);
	std::vector<std::tuple<uint32_t, uint32_t, std::string>> table;
	while (r.readFlag())
	{
		const auto type = r.readInt(12);
		const auto id = r.readInt(12);
		table.emplace_back(type, id, r.readString());
	}
	REQUIRE(r.readU32() == 19);
	REQUIRE(std::get<2>(table[0]) == "Camera");
	REQUIRE(std::get<0>(table[0]) == 0);
	REQUIRE(std::get<2>(table[1]) == "CameraData");
	REQUIRE(std::get<0>(table[1]) == 1);
	REQUIRE(std::get<1>(table[1]) == 0);
	for (size_t i = 2; i < table.size(); ++i)
		REQUIRE(std::get<0>(table[i]) == 2);
}

TEST_CASE("protocol 19 moves and the client game section round trip", "[graal3d][tge]")
{
	tge::ClientGame g;
	g.firstPerson = true;
	g.position = {1, 2, 3};
	g.controlGhost = 5;
	g.firstMove = 40;
	tge::Move m;
	m.yaw = 0x1234;
	m.x = 20;
	m.triggers[0] = true;
	g.moves = {m, tge::Move{}};
	g.fov = 90;

	BitWriter w;
	tge::writeClientGame(w, g);
	// A move with nothing set is 3 + 18 + 11 bits.
	REQUIRE(w.bitPosition() == 1 + 96 + 1 + 12 + 1 + 32 + 5 + (32 + 16) + 32 + 1 + 8);
	const auto bytes = w.toString();
	BitReader r(bytes);
	const auto back = tge::readClientGame(r);
	REQUIRE(back);
	REQUIRE(back->firstMove == 40);
	REQUIRE(back->moves.size() == 2);
	REQUIRE(back->moves[0].yaw == 0x1234u);
	REQUIRE(back->moves[0].x == 20);
	REQUIRE(back->moves[0].triggers[0]);
	REQUIRE(back->moves[1].x == 16);
	REQUIRE(back->fov == 90u);
}

TEST_CASE("a 4.04r2 client goes through the three phases to a Camera control object", "[graal3d][tge]")
{
	Client19 c;
	GhostObject camera;
	camera.kind = GhostObject::Kind::TgeCamera;
	camera.tgeCamera.position = {10, 20, 5};
	c.conn.setGhost(7, camera);
	c.conn.setControlObject(7);
	// GameObject-era objects are not for this client.
	GhostObject other;
	other.object.name = "G3DPlayer_x";
	c.conn.setGhost(8, other);
	REQUIRE(c.conn.ghostCount() == 1);

	// First client packet: the link is up, phase 1 goes out with sequence and mission.
	c.send({});
	auto packets = c.pump();
	auto* p = firstData(packets);
	REQUIRE(p);
	auto commands = commandsIn(*p, c.strings);
	REQUIRE(commands.size() == 1);
	REQUIRE(commands[0] == std::vector<std::string>{"MissionStartPhase1", "1", "Graal3D"});
	REQUIRE(p->ghosts.empty());
	REQUIRE(c.conn.missionState() == MissionState::Phase1Sent);

	// 4.04r2 acks bare. The datablock follows, then DataBlocksDone (5).
	c.send({{"RemoteCommandEvent", commandEvent({"MissionStartPhase1Ack"})}});
	packets = c.pump();
	p = firstData(packets);
	REQUIRE(p);
	REQUIRE(p->ghosts.empty()); // no ghost before the datablocks are loaded
	const Event19* datablock = nullptr;
	for (const auto& ev : p->events)
	{
		if (ev.name == "SimDataBlockEvent")
			datablock = &ev;
	}
	REQUIRE(datablock);
	{
		BitReader r(datablock->payload);
		REQUIRE(r.readInt(12) + 3 == 3);              // datablock id
		REQUIRE(r.readInt(12) == *datablockClassId(c.profile, "CameraData"));
		REQUIRE(r.readInt(12) == 0);                  // index
		REQUIRE(r.readInt(13) == 1);                  // total
		const auto bits = r.readInt(14);
		REQUIRE(r.bitPosition() + bits == datablock->bits);
		const auto data = readCameraData(copyBits(r, bits), bits);
		REQUIRE(data.fits);
		REQUIRE(data.shape.empty());
	}
	auto msgs = messagesIn(*p);
	REQUIRE(msgs.size() == 1);
	REQUIRE(msgs[0].message == tge::message::DataBlocksDone);
	REQUIRE(msgs[0].sequence == 1);

	// The client loaded it: DataBlocksDownloadDone (6) with the sequence.
	c.send({{"ConnectionMessageEvent", tge::connectionMessageEvent(1, tge::message::DataBlocksDownloadDone, 0)}});
	packets = c.pump();
	p = firstData(packets);
	REQUIRE(p);
	commands = commandsIn(*p, c.strings);
	REQUIRE(commands.size() == 1);
	REQUIRE(commands[0][0] == "MissionStartPhase2");
	msgs = messagesIn(*p);
	REQUIRE(msgs.size() == 1);
	REQUIRE(msgs[0].message == tge::message::GhostAlwaysStarting);
	REQUIRE(msgs[0].count == 1);
	REQUIRE(p->ghosts.size() == 1);
	REQUIRE(std::string(c.profile.objectClasses[p->ghosts[0].classId]) == "Camera");
	{
		const auto g = readCameraGhost(p->ghosts[0].payload, p->ghosts[0].bits);
		REQUIRE(g.fits);
		REQUIRE(g.datablock == 3u);
		REQUIRE(g.position);
		REQUIRE((*g.position)[0] == 10.0f);
		REQUIRE_FALSE(g.clientControlled);
		REQUIRE(g.move);
		REQUIRE(g.move->position[2] == 5.0f);
	}
	const uint32_t cameraGhost = p->ghosts[0].index;

	// Phase 2 ack (bare), in a packet that also acks the ghost create.
	c.send({{"RemoteCommandEvent", commandEvent({"MissionStartPhase2Ack"})}});
	packets = c.pump();
	REQUIRE(c.conn.ghostDelivered(7));
	// The create was acked by that packet; the next update sends phase 3.
	if (c.conn.missionState() != MissionState::Phase3Sent)
		packets = c.pump();
	REQUIRE(c.conn.missionState() == MissionState::Phase3Sent);
	std::vector<std::vector<std::string>> all;
	std::vector<ConnMsg> allMsgs;
	for (const auto& q : packets)
	{
		for (auto& cmd : commandsIn(q, c.strings))
			all.push_back(cmd);
		for (auto& m : messagesIn(q))
			allMsgs.push_back(m);
	}
	REQUIRE(all.size() == 1);
	REQUIRE(all[0][0] == "MissionStartPhase3");
	REQUIRE(allMsgs.size() == 1);
	REQUIRE(allMsgs[0].message == tge::message::GhostAlwaysDone);

	// Phase 3 ack after lighting: the Camera becomes the control object, with its
	// packet data at the spawn point.
	c.send({{"ConnectionMessageEvent", tge::connectionMessageEvent(1, tge::message::ReadyForNormalGhosts, 0)},
			{"RemoteCommandEvent", commandEvent({"MissionStartPhase3Ack"})}});
	packets = c.pump();
	REQUIRE(c.conn.missionState() == MissionState::Done);
	p = firstData(packets);
	if (!p || !p->control)
	{
		packets = c.pump();
		p = firstData(packets);
	}
	REQUIRE(p);
	REQUIRE(p->control);
	REQUIRE(p->controlGhost == cameraGhost);
	REQUIRE(p->controlData.position == std::array<float, 3>{10, 20, 5});
	REQUIRE(p->controlData.mode == static_cast<uint32_t>(tge::CameraMode::Fly));

	// The client now sends moves; the server acks them in its move ack.
	tge::ClientGame game;
	game.controlGhost = cameraGhost;
	game.firstMove = 0;
	game.moves.assign(3, tge::Move{});
	c.send({}, game);
	game.firstMove = 2; // a resend of move 2, then 3 and 4
	c.send({}, game);
	packets = c.pump();
	p = firstData(packets);
	REQUIRE(p);
	REQUIRE(p->moveAck == 5);
	REQUIRE(c.conn.movesReceived() == 5);
	REQUIRE_FALSE(p->control); // sent once, and the client got it
}

TEST_CASE("a 3.153 ack with another mission sequence is ignored", "[graal3d][tge]")
{
	Client19 c;
	c.send({});
	c.pump();
	c.send({{"RemoteCommandEvent", commandEvent({"MissionStartPhase1Ack", "7"})}});
	auto packets = c.pump();
	for (const auto& p : packets)
		REQUIRE(messagesIn(p).empty());

	// The right sequence starts the datablocks, as the 3.153 script sends it.
	c.send({{"RemoteCommandEvent", commandEvent({"MissionStartPhase1Ack", "1"})}});
	packets = c.pump();
	auto* p = firstData(packets);
	REQUIRE(p);
	const auto msgs = messagesIn(*p);
	REQUIRE(msgs.size() == 1);
	REQUIRE(msgs[0].message == tge::message::DataBlocksDone);
}
