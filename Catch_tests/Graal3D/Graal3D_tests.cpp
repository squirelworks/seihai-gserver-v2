// Modified by the Seihai project, 2026-11-11, 2026-11-12, 2026-11-13, 2026-11-15, 2026-11-20, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: tests for the Graal 3D (Torque) UDP side. The client bytes are from the
// 2026-10-30 Wine lab, where the real 5.12 client joined a test 3D server
// (Graal_analysis torque/network.md, "The full handshake, live").
#include "catch2/catch_all.hpp"

#include <cmath>
#include <string>
#include <vector>

#include <zlib.h>

#include <graal3d/Graal3DConnection.h>
#include <graal3d/Graal3DObjects.h>
#include <graal3d/Graal3DProtocol.h>
#include <graal3d/TorqueBitStream.h>

using namespace graal3d;

namespace
{
	std::string fromHex(std::string_view hex)
	{
		std::string out;
		for (size_t i = 0; i + 1 < hex.size(); i += 2)
			out.push_back(static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
		return out;
	}

	// The real 5.12 client's packets.
	const std::string kChallenge = fromHex("1a00040000");
	const std::string kConnect = fromHex(
		"2000040000000102030405060708090a0b0c0d0e0f1d6213fd6a4af4185809000000e0ffffffff41ac650e47670c00"
		"00000c00008008d1e976dc8548169b000000c082de7f0300");
	const std::string kFirstData = fromHex("0500000441b800000000000000000000000000");
	const std::string kSecondData = fromHex("0900000800000000000000000000000000");

	const Digest kLabDigest{0x03020100, 0x07060504, 0x0b0a0908, 0x0f0e0d0c};

	// --- A small client, written from the same notes, to read what the server sends.

	struct ClientEvent
	{
		int32_t seq;
		uint32_t classId;
		std::string payload;
		size_t bits;
	};

	struct GhostEntry
	{
		uint32_t index;
		bool deleted;
		uint32_t classId = 0;
		std::string payload;
		size_t bits = 0;
	};

	struct ServerPacket
	{
		PacketHeader header;
		bool control = false;
		uint32_t controlIndex = 0;
		bool camera = false;
		uint32_t cameraIndex = 0;
		uint32_t controlMask = 0;
		std::vector<ClientEvent> events;
		uint32_t ghostIdBits = 0;
		std::vector<GhostEntry> ghosts;
	};

	// A reader for one event or ghost payload, with the packet string buffer the
	// client keeps. The server never shares prefixes, so one buffer per payload
	// reads the same as one per packet.
	struct PayloadReader
	{
		std::string buffer;
		BitReader r;
		explicit PayloadReader(const std::string& payload)
			: r(payload) { r.setStringBuffer(&buffer); }
	};

	ServerPacket readServerPacket(std::string_view datagram, bool cameraControl = false)
	{
		BitReader r(datagram);
		std::string stringBuffer;
		r.setStringBuffer(&stringBuffer);
		ServerPacket out;
		auto h = readPacketHeader(r);
		REQUIRE(h);
		out.header = *h;
		if (h->type != PacketType::Data)
			return out;

		REQUIRE_FALSE(r.readFlag()); // rate
		REQUIRE_FALSE(r.readFlag()); // max rate
		// Control object: flag A, flag B; both set carry it.
		REQUIRE(r.readFlag());
		if (r.readFlag())
		{
			out.control = true;
			r.readU32(); // server time
			out.controlIndex = r.readInt(12);
			GameObjectState state;
			std::string moveType;
			out.controlMask = readGameObjectUpdate(r, state, moveType);
			if (cameraControl)
			{
				REQUIRE_FALSE(r.readFlag()); // camera mode
				REQUIRE_FALSE(r.readFlag()); // followed object
			}
		}
		if (r.readFlag())
		{
			out.camera = true;
			out.cameraIndex = r.readInt(12);
			REQUIRE_FALSE(r.readFlag()); // no camera packet data
		}
		REQUIRE_FALSE(r.readFlag()); // fov
		REQUIRE_FALSE(r.readFlag()); // no unguaranteed events
		int32_t prev = -2;
		while (r.readFlag())
		{
			int32_t seq = r.readFlag() ? ((prev + 1) & 0x7F) : static_cast<int32_t>(r.readInt(7));
			prev = seq;
			ClientEvent ev{seq, r.readInt(12), {}, r.readInt(14)};
			BitWriter copy;
			for (size_t i = 0; i < ev.bits; ++i)
				copy.writeFlag(r.readFlag());
			ev.payload = copy.toString();
			out.events.push_back(std::move(ev));
		}
		if (r.readFlag())
		{
			out.ghostIdBits = r.readInt(4);
			while (r.readFlag())
			{
				GhostEntry g{r.readInt(static_cast<int>(out.ghostIdBits)), r.readFlag()};
				if (!g.deleted)
				{
					g.classId = r.readInt(12);
					g.bits = r.readInt(14);
					BitWriter copy;
					for (size_t i = 0; i < g.bits; ++i)
						copy.writeFlag(r.readFlag());
					g.payload = copy.toString();
				}
				out.ghosts.push_back(std::move(g));
			}
		}
		REQUIRE_FALSE(r.overrun());
		REQUIRE(r.bitsLeft() < 8);
		return out;
	}

	// A client data packet in the 5.12 layout carrying guaranteed events.
	struct OutEvent
	{
		std::string className;
		BitWriter payload;
	};

	// The client's own control object section: nullptr for none.
	struct ClientControl
	{
		std::array<float, 3> position{};
		uint32_t ghostIndex = 0;
		const BitWriter* update = nullptr; // the GameObject update the client echoes
	};

	std::string clientDataPacket(const Profile& profile, uint32_t seq, uint32_t highestAck, uint32_t ackMask,
								 const std::vector<OutEvent>& events, int32_t firstEventSeq, const ClientControl* control = nullptr)
	{
		BitWriter w;
		PacketHeader h;
		h.sequence = seq;
		h.highestAck = highestAck;
		h.ackByteCount = 1;
		h.ackMask = ackMask;
		writePacketHeader(w, h);
		w.writeFlag(false); // rate
		w.writeFlag(false); // max rate
		w.writeFlag(true);
		const ClientControl none;
		const auto& c = control ? *control : none;
		for (auto v : c.position)
			w.writeF32(v);
		if (w.writeFlag(control != nullptr))
			w.writeInt(c.ghostIndex, 12);
		if (w.writeFlag(c.update != nullptr))
			w.writeBits(*c.update);
		w.writeFlag(false); // fov
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
		return w.toString();
	}

	std::string remoteCommandName(const ClientEvent& ev, const std::vector<std::string>& strings)
	{
		PayloadReader p(ev.payload);
		auto& r = p.r;
		REQUIRE(r.readInt(CommandArgsBits) == 1);
		auto arg = unpackCommandArg(r);
		return arg.isTag ? strings.at(arg.tag) : arg.text;
	}

	// Runs a connection with a fake clock and collects what it sends.
	struct Harness
	{
		const Profile& profile = *profileForProtocol(24);
		uint64_t now = 1000;
		std::vector<std::string> sent;
		std::vector<TextMessage> texts;
		std::vector<MissionState> mission;
		std::vector<ClientState> states;
		std::vector<std::string> logs;
		std::vector<std::array<uint32_t, 256>> terrainRequests;
		Connection conn{profile, 0x400, now, Connection::Callbacks{
												  [this](std::string_view d) { sent.emplace_back(d); },
												  [this](const TextMessage& m) { texts.push_back(m); },
												  [this](std::string_view l) { logs.emplace_back(l); },
												  [this](MissionState s) { mission.push_back(s); },
												  [this](const ClientState& c) { states.push_back(c); },
												  [this](const std::array<uint32_t, 256>& s) { terrainRequests.push_back(s); },
											  }};

		void tick(uint64_t ms)
		{
			now += ms;
			REQUIRE(conn.update(now));
		}

		// The client's MissionStartPhase1Ack (its event 0) in its data packet 2,
		// acking the server's packet 1: ghosts go out only after it.
		void phase1Ack()
		{
			std::vector<OutEvent> evs;
			evs.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "MissionStartPhase1Ack"}})});
			conn.receive(clientDataPacket(profile, 2, 1, 1, evs, 0), now);
			tick(60);
		}
	};
} // namespace

SCENARIO("The handshake matches the real 5.12 client")
{
	GIVEN("the client's challenge request")
	{
		THEN("its sequence is read and the response has the lab's bytes")
		{
			REQUIRE(parseChallengeRequest(kChallenge) == 0x400u);
			REQUIRE(buildChallengeResponse(0x400, kLabDigest) == fromHex("1e00040000000102030405060708090a0b0c0d0e0f"));
		}
	}

	GIVEN("the client's connect request")
	{
		const auto req = parseConnectRequest(kConnect);
		REQUIRE(req);

		THEN("every field reads as in the lab")
		{
			REQUIRE(req->sequence == 0x400);
			REQUIRE(req->digest == kLabDigest);
			REQUIRE(req->className == "GameConnection");
			REQUIRE(req->classGroup == 0);
			REQUIRE(req->classCrc == 0xFFFFFFFF);
			REQUIRE(req->game == "Graal3D");
			REQUIRE(req->currentProtocol == 24);
			REQUIRE(req->minimumProtocol == 24);
			REQUIRE(req->joinPassword == "7Y3bdzjf");
			REQUIRE(req->args == std::vector<std::string>{"guest", ""});
			REQUIRE_FALSE(checkConnectRequest(*req));
		}

		THEN("writing the same fields gives the same bytes (Huffman strings)")
		{
			BitWriter w;
			w.writeU8(0x20);
			w.writeU32(req->sequence);
			for (auto d : req->digest)
				w.writeU32(d);
			w.writeString(req->className);
			w.writeU32(req->classGroup);
			w.writeU32(req->classCrc);
			w.writeString(req->game);
			w.writeU32(req->currentProtocol);
			w.writeU32(req->minimumProtocol);
			w.writeString(req->joinPassword);
			w.writeU32(static_cast<uint32_t>(req->args.size()));
			for (const auto& a : req->args)
				w.writeString(a);
			REQUIRE(w.toString() == kConnect);
		}
	}

	GIVEN("other games and versions")
	{
		auto req = *parseConnectRequest(kConnect);
		THEN("a non-Graal game, an unknown protocol and no account are refused")
		{
			auto bad = req;
			bad.game = "Torque Game Engine Demo";
			REQUIRE(checkConnectRequest(bad) == "CHR_GAME");
			bad = req;
			bad.currentProtocol = bad.minimumProtocol = 9;
			REQUIRE(checkConnectRequest(bad) == "CHR_PROTOCOL");
			bad = req;
			bad.args.clear();
			REQUIRE(checkConnectRequest(bad) == "CR_INVALID_ARGS");
		}
		THEN("protocols 19 and 21 have profiles")
		{
			REQUIRE(profileForProtocol(19) != nullptr);
			REQUIRE(profileForProtocol(21) != nullptr);
			REQUIRE(profileForProtocol(19)->eventType() == 2);
			REQUIRE(profileForProtocol(24)->eventType() == 1);
		}
	}
}

SCENARIO("The connect accept carries the class table and the protocol")
{
	GIVEN("a profile with no classes")
	{
		Profile empty{24, "test", false, true, 2, {}, {}};
		THEN("it is the lab's accept, which the real client took")
		{
			REQUIRE(buildConnectAccept(0x400, empty) == fromHex("24000400003000000000"));
		}
	}

	GIVEN("the 5.x profile")
	{
		const auto& profile = *profileForProtocol(24);
		const auto accept = buildConnectAccept(0x400, profile);

		THEN("the client's reader finds every class with type < 2, then protocol 24")
		{
			BitReader r(accept);
			REQUIRE(r.readU8() == 0x24);
			REQUIRE(r.readU32() == 0x400);
			std::vector<std::string> events;
			size_t objects = 0;
			while (r.readFlag())
			{
				const auto type = r.readInt(12);
				const auto id = r.readInt(12);
				const auto name = r.readString();
				REQUIRE(type < 2);
				REQUIRE(id < 256);
				if (type == 1)
				{
					REQUIRE(id == events.size());
					events.push_back(name);
				}
				else
					++objects;
			}
			REQUIRE(r.readU32() == 24);
			REQUIRE(r.bitsLeft() < 8);
			REQUIRE(objects == profile.objectClasses.size());
			REQUIRE(events.size() == profile.eventClasses.size());
			REQUIRE(eventClassId(profile, "BigDataStartEvent") == 1u);
		}
	}
}

SCENARIO("A 5.x connection follows the real client's packets")
{
	GIVEN("an accepted connection")
	{
		Harness h;

		WHEN("the client's first data packet arrives")
		{
			h.conn.receive(kFirstData, h.now);
			h.tick(60);

			THEN("the connection is up and phase 1 is sent as a tagged command")
			{
				REQUIRE(h.conn.established());
				REQUIRE(h.mission == std::vector<MissionState>{MissionState::Phase1Sent});
				REQUIRE(h.sent.size() == 1);

				const auto pkt = readServerPacket(h.sent[0]);
				REQUIRE(pkt.header.sequence == 1);
				REQUIRE(pkt.header.highestAck == 1); // the client's packet 1
				REQUIRE(pkt.header.ackByteCount == 1);
				REQUIRE((pkt.header.ackMask & 1) == 1);
				REQUIRE(pkt.events.size() == 2);
				REQUIRE(pkt.events[0].classId == *eventClassId(h.profile, "NetStringEvent"));
				REQUIRE(pkt.events[1].classId == *eventClassId(h.profile, "RemoteCommandEvent"));

				PayloadReader s(pkt.events[0].payload);
				const auto index = s.r.readInt(StringTableBits);
				std::vector<std::string> strings(32);
				strings[index] = s.r.readString();
				REQUIRE(remoteCommandName(pkt.events[1], strings) == "MissionStartPhase1");
			}

			AND_WHEN("the client's next packet (seq 2) acks nothing yet and a ping follows")
			{
				h.sent.clear();
				h.conn.receive(kSecondData, h.now);
				h.conn.receive(fromHex("15001000"), h.now); // a ping, as the client sends
				THEN("the ping is answered with an ack packet")
				{
					REQUIRE_FALSE(h.sent.empty());
					BitReader r(h.sent.back());
					auto hdr = readPacketHeader(r);
					REQUIRE(hdr);
					REQUIRE(hdr->type == PacketType::Ack);
				}
			}
		}
	}
}

SCENARIO("Mission phases, the text channel and lost packets")
{
	Harness h;
	h.conn.receive(kFirstData, h.now);
	h.tick(60);
	REQUIRE(h.sent.size() == 1);
	const auto& profile = h.profile;

	WHEN("the server's packet is lost (the client's ack mask has it as 0)")
	{
		h.sent.clear();
		h.conn.receive(clientDataPacket(profile, 2, 1, 0, {}, 0), h.now);
		h.tick(60);
		THEN("the two events go out again in the next packet")
		{
			REQUIRE_FALSE(h.sent.empty());
			const auto pkt = readServerPacket(h.sent.back());
			REQUIRE(pkt.header.sequence == 2);
			REQUIRE(pkt.events.size() == 2);
			REQUIRE(pkt.events[0].seq == 0);
			REQUIRE(pkt.events[1].seq == 1);
		}
	}

	WHEN("the client acks it and answers phase 1 as 5.12 does (a plain string)")
	{
		std::vector<OutEvent> evs;
		evs.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "MissionStartPhase1Ack"}})});
		h.sent.clear();
		h.conn.receive(clientDataPacket(profile, 2, 1, 1, evs, 0), h.now);
		h.tick(60);

		THEN("phase 2 follows and nothing is left unacked from phase 1")
		{
			REQUIRE(h.mission.back() == MissionState::Phase2Sent);
			const auto pkt = readServerPacket(h.sent.back());
			REQUIRE(pkt.events.size() == 2);
			REQUIRE(pkt.events[0].seq == 2);
			PayloadReader s(pkt.events[0].payload);
			std::vector<std::string> strings(32);
			const auto index = s.r.readInt(StringTableBits);
			strings[index] = s.r.readString();
			REQUIRE(remoteCommandName(pkt.events[1], strings) == "MissionStartPhase2");
			REQUIRE(h.conn.unackedEvents() == 2);
		}

		AND_WHEN("the phase 2 ack comes as a tagged string")
		{
			std::vector<OutEvent> evs2;
			evs2.push_back({"NetStringEvent", netStringEvent(3, "MissionStartPhase2Ack")});
			evs2.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{true, 3, {}}})});
			h.conn.receive(clientDataPacket(profile, 3, 2, 1, evs2, 1), h.now);
			THEN("the mission is done")
			{
				REQUIRE(h.mission.back() == MissionState::Done);
			}
		}
	}

	WHEN("the client sends its game events over BigData, zlib compressed")
	{
		TextMessage msg{"", "events", "", "s client.test=1\nw Bomb\nt weapon,-System,hello"};
		std::string packed(compressBound(static_cast<uLong>(msg.data.size())), '\0');
		uLongf packedLen = static_cast<uLongf>(packed.size());
		compress(reinterpret_cast<Bytef*>(packed.data()), &packedLen, reinterpret_cast<const Bytef*>(msg.data.data()),
				 static_cast<uLong>(msg.data.size()));
		packed.resize(packedLen);

		std::vector<OutEvent> evs;
		evs.push_back({"BigDataStartEvent", bigDataStartEvent(7, msg, static_cast<uint32_t>(packed.size()), BigDataMode::Zlib)});
		evs.push_back({"BigDataChunkEvent", bigDataChunkEvent(7, packed)});
		h.conn.receive(clientDataPacket(profile, 2, 1, 1, evs, 0), h.now);

		THEN("the server gets the four strings back")
		{
			REQUIRE(h.texts.size() == 1);
			REQUIRE(h.texts[0].type == "events");
			REQUIRE(h.texts[0].data == msg.data);
		}
	}

	WHEN("the server sends a long text")
	{
		const std::string data(500, 'x');
		h.conn.sendText(TextMessage{"", "set", "", data});
		h.sent.clear();
		h.conn.receive(clientDataPacket(profile, 2, 1, 1, {}, 0), h.now);
		h.tick(60);
		h.tick(60);
		h.tick(60);
		THEN("it goes as a start event and 191-byte chunks, in packets under the size limit")
		{
			std::vector<ClientEvent> events;
			for (const auto& d : h.sent)
			{
				REQUIRE(d.size() <= Connection::MaxPacketBytes);
				for (auto& ev : readServerPacket(d).events)
					events.push_back(ev);
			}
			REQUIRE(events.size() == 4);
			REQUIRE(events[0].seq == 2);
			REQUIRE(events[3].seq == 5);
			BigDataAssembler a;
			PayloadReader start(events[0].payload);
			a.start(start.r);
			std::optional<TextMessage> got;
			for (size_t i = 1; i < events.size(); ++i)
			{
				REQUIRE(events[i].classId == *eventClassId(profile, "BigDataChunkEvent"));
				PayloadReader c(events[i].payload);
				got = a.chunk(c.r);
			}
			REQUIRE(got);
			REQUIRE(got->type == "set");
			REQUIRE(got->data == data);
		}
	}

	WHEN("the client goes quiet")
	{
		h.now += Connection::TimeoutMs + 1;
		THEN("update reports the connection as timed out")
		{
			REQUIRE_FALSE(h.conn.update(h.now));
		}
	}
}

SCENARIO("A 3.153 or 4.04r2 connection starts the mission on its first data packet")
{
	const auto& profile = *profileForProtocol(19);
	std::vector<std::string> sent;
	Connection conn{profile, 0x401, 0, Connection::Callbacks{[&](std::string_view d) { sent.emplace_back(d); }, nullptr, nullptr, nullptr}};

	// A header-only data packet with the odd connect-sequence bit. Its game
	// section is missing, so nothing in it is read, but the link is up.
	BitWriter w;
	PacketHeader hdr;
	hdr.connectSequenceBit = 1;
	hdr.sequence = 1;
	writePacketHeader(w, hdr);
	conn.receive(w.toString(), 10);
	REQUIRE(conn.update(100));

	THEN("it is answered with a data packet carrying MissionStartPhase1, and text can be queued")
	{
		REQUIRE(sent.size() == 1);
		BitReader r(sent[0]);
		auto h = readPacketHeader(r);
		REQUIRE(h);
		REQUIRE(h->type == PacketType::Data);
		REQUIRE(h->connectSequenceBit == 1);
		REQUIRE(h->highestAck == 1);
		REQUIRE(conn.missionState() == MissionState::Phase1Sent);
		conn.sendText(TextMessage{"", "set", "", "x"});
		REQUIRE(conn.pendingEvents() > 0);
	}
}

SCENARIO("Strings inside game packets carry the packet string flag")
{
	GIVEN("a packet writer and a reader with the client's string buffer")
	{
		BitWriter w;
		w.setPacketStrings(true);
		w.writeString("MissionStartPhase1");
		w.writeString("");

		THEN("each string starts with a 0 prefix flag and reads back")
		{
			const auto bytes = w.toString();
			BitReader plain(bytes);
			REQUIRE_FALSE(plain.readFlag());

			std::string buffer;
			const auto data = w.toString();
			BitReader r(data);
			r.setStringBuffer(&buffer);
			REQUIRE(r.readString() == "MissionStartPhase1");
			REQUIRE(r.readString().empty());
			REQUIRE_FALSE(r.overrun());
		}
	}

	GIVEN("a client string that reuses a prefix of the one before it")
	{
		BitWriter w;
		w.writeFlag(false);
		w.writeString("MissionStartPhase1Ack");
		w.writeFlag(true);
		w.writeInt(17, 8);
		w.writeString("2Ack");

		THEN("the reader puts the prefix back")
		{
			std::string buffer;
			const auto data = w.toString();
			BitReader r(data);
			r.setStringBuffer(&buffer);
			REQUIRE(r.readString() == "MissionStartPhase1Ack");
			REQUIRE(r.readString() == "MissionStartPhase2Ack");
		}
	}
}

SCENARIO("GameObject updates follow the 5.00r7 layout")
{
	GameObjectState obj;
	obj.name = "G3DPlayer_guest";
	obj.shape = "player.dts";
	obj.transform.position = {1.0f, 2.0f, 3.0f};
	obj.transform.angle = 1.5f;
	obj.nickname = "guest";
	obj.flags = GameObjectFlags::Shadow;

	GIVEN("an update with no sections")
	{
		BitWriter w;
		writeGameObjectUpdate(w, obj, 0);
		THEN("it is thirteen 0 flags")
		{
			REQUIRE(w.bitPosition() == 13);
			REQUIRE(w.toString() == std::string("\x00\x00", 2));
		}
	}

	GIVEN("a full update")
	{
		BitWriter w;
		w.setPacketStrings(true);
		const auto mask = gameObjectFullMask(obj);
		writeGameObjectUpdate(w, obj, mask);

		THEN("it reads back field for field, the transform as 7 raw floats")
		{
			REQUIRE(mask == (GameObjectBits::Name | GameObjectBits::Shape | GameObjectBits::Transform | GameObjectBits::Nickname |
							 GameObjectBits::Flags));
			std::string buffer;
			const auto data = w.toString();
			BitReader r(data);
			r.setStringBuffer(&buffer);
			GameObjectState got;
			std::string moveType;
			REQUIRE(readGameObjectUpdate(r, got, moveType) == mask);
			REQUIRE(got == obj);
			REQUIRE(r.bitPosition() == w.bitPosition());
		}
	}

	GIVEN("a client echo that sets movetype interpolate, then sends movement data")
	{
		BitWriter w;
		w.setPacketStrings(true);
		for (int i = 0; i < 5; ++i)
			w.writeFlag(false);
		w.writeFlag(true);
		w.writeString("interpolate");
		w.writeFlag(true);
		for (int i = 0; i < 183; ++i)
			w.writeFlag(i % 3 == 0); // 3 floats, a quaternion, two vectors, one flag
		for (int i = 0; i < 5; ++i)
			w.writeFlag(false);
		w.writeFlag(true);
		w.writeInt(GameObjectFlags::Solid, 16);

		THEN("the reader skips the 183 bits and reads the sections after them")
		{
			std::string buffer;
			const auto data = w.toString();
			BitReader r(data);
			r.setStringBuffer(&buffer);
			GameObjectState got;
			std::string moveType;
			const auto mask = readGameObjectUpdate(r, got, moveType);
			REQUIRE(moveType == "interpolate");
			REQUIRE((mask & GameObjectBits::MoveData) != 0);
			REQUIRE(got.flags == GameObjectFlags::Solid);
			REQUIRE(r.bitPosition() == w.bitPosition());
		}
	}
}

SCENARIO("Ghosting a world and handing the client its control object")
{
	Harness h;
	const auto& profile = h.profile;

	GhostObject ground;
	ground.object.name = "Ground";
	ground.object.shape = "ground.dts";
	GhostObject sun;
	sun.kind = GhostObject::Kind::Sun;
	GhostObject me;
	me.object.name = "G3DPlayer_guest";
	me.object.shape = "player.dts";
	me.object.transform.position = {10.0f, 20.0f, 1.0f};

	h.conn.setGhost(1, ground);
	h.conn.setGhost(2, sun);
	h.conn.setGhost(3, me);
	h.conn.setControlObject(3);
	h.conn.receive(kFirstData, h.now);
	h.tick(60);

	// No ghost before the client's MissionStartPhase1Ack: a 5.12 on a return
	// visit dropped them and stalled on "Received unknown camera object" (lab
	// 2026-11-25).
	REQUIRE(h.sent.size() == 1);
	REQUIRE(readServerPacket(h.sent[0]).ghosts.empty());
	h.phase1Ack();
	REQUIRE(h.sent.size() == 2);
	REQUIRE(h.mission.back() == MissionState::Phase2Sent);
	const auto first = readServerPacket(h.sent[1]);
	REQUIRE(first.header.sequence == 2);

	THEN("the first packet creates every ghost, with its class id and a full update")
	{
		REQUIRE_FALSE(first.control); // not before the client has the ghost
		REQUIRE(first.ghosts.size() == 3);
		REQUIRE(first.ghostIdBits == 2); // highest index 2
		REQUIRE(first.ghosts[0].index == 0);
		REQUIRE(first.ghosts[0].classId == *objectClassId(profile, "GameObject"));
		REQUIRE(first.ghosts[1].classId == *objectClassId(profile, "Sun"));

		PayloadReader p(first.ghosts[2].payload);
		GameObjectState got;
		std::string moveType;
		readGameObjectUpdate(p.r, got, moveType);
		REQUIRE(got == me.object);
		REQUIRE(p.r.bitPosition() == first.ghosts[2].bits);

		PayloadReader ps(first.ghosts[1].payload);
		REQUIRE(readSunUpdate(ps.r) == sun.sun);
	}

	WHEN("the client acks that packet but has not finished the mission start")
	{
		h.sent.clear();
		h.conn.receive(clientDataPacket(profile, 3, 2, 3, {}, 0), h.now);
		h.tick(60);
		h.tick(1000);

		THEN("the ghosts are in, and the control object waits")
		{
			REQUIRE(h.conn.ghostDelivered(3));
			REQUIRE_FALSE(h.sent.empty());
			for (const auto& data : h.sent)
				REQUIRE_FALSE(readServerPacket(data).control);
		}
	}

	WHEN("the client acks that packet and answers the second mission phase")
	{
		REQUIRE_FALSE(first.control); // MissionStartPhase2 would undo login step 5

		std::vector<OutEvent> phase2Ack;
		phase2Ack.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "MissionStartPhase2Ack"}})});
		h.sent.clear();
		h.conn.receive(clientDataPacket(profile, 3, 2, 3, phase2Ack, 1), h.now);
		h.tick(60);

		THEN("the control object follows the mission start, as 13 empty sections")
		{
			REQUIRE(h.mission.back() == MissionState::Done);
			REQUIRE(h.conn.ghostDelivered(3));
			REQUIRE_FALSE(h.sent.empty());
			const auto pkt = readServerPacket(h.sent.back());
			REQUIRE(pkt.header.sequence == 3);
			REQUIRE(pkt.control);
			REQUIRE(pkt.controlIndex == *h.conn.ghostIndex(3));
			REQUIRE(pkt.controlMask == 0);
			REQUIRE(pkt.ghosts.empty());
		}

		AND_WHEN("that is acked and the client reports a move and a chat on its object")
		{
			h.conn.receive(clientDataPacket(profile, 4, 3, 7, {}, 0), h.now);
			REQUIRE(h.conn.controlObjectDelivered());

			GameObjectState moved = me.object;
			moved.transform.position = {11.0f, 20.0f, 1.0f};
			moved.chat = "hello";
			BitWriter echo;
			echo.setPacketStrings(true);
			writeGameObjectUpdate(echo, moved, GameObjectBits::Transform | GameObjectBits::Chat);
			ClientControl control{{11.0f, 20.0f, 1.0f}, *h.conn.ghostIndex(3), &echo};
			std::vector<OutEvent> evs;
			evs.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "Hello"}})});
			h.conn.receive(clientDataPacket(profile, 5, 3, 7, evs, 2, &control), h.now);

			THEN("the server sees the new position and chat, and the events after them still read")
			{
				REQUIRE_FALSE(h.states.empty());
				const auto& st = h.states.back();
				REQUIRE(st.controlling);
				REQUIRE(st.position[0] == 11.0f);
				REQUIRE(st.changed == (GameObjectBits::Transform | GameObjectBits::Chat));
				REQUIRE(st.object.chat == "hello");
				REQUIRE(st.object.shape == "player.dts");
				REQUIRE_FALSE(h.logs.empty());
				REQUIRE(h.logs.back() == "remote command ignored: Hello"); // the event after the echo read whole
				for (const auto& line : h.logs)
					REQUIRE(line.find("does not fill its length") == std::string::npos);
			}
		}

		AND_WHEN("an object changes and another is removed")
		{
			h.sent.clear();
			ground.object.transform.position = {0.0f, 0.0f, 5.0f};
			h.conn.setGhost(1, ground);
			h.conn.removeGhost(2);
			h.tick(60);

			THEN("the delete and an update with only the transform go out")
			{
				const auto pkt = readServerPacket(h.sent.back());
				REQUIRE(pkt.ghosts.size() == 2);
				REQUIRE(pkt.ghosts[0].deleted);
				REQUIRE(pkt.ghosts[0].index == 1);
				PayloadReader p(pkt.ghosts[1].payload);
				GameObjectState got;
				std::string moveType;
				REQUIRE(readGameObjectUpdate(p.r, got, moveType) == GameObjectBits::Transform);
				REQUIRE(got.transform.position[2] == 5.0f);
			}
		}
	}

	WHEN("the packet with the creates is lost")
	{
		h.sent.clear();
		h.conn.receive(clientDataPacket(profile, 3, 2, 2, {}, 0), h.now); // 1 in, 2 lost
		h.tick(60);

		THEN("the creates go out again")
		{
			const auto pkt = readServerPacket(h.sent.back());
			REQUIRE(pkt.ghosts.size() == 3);
			REQUIRE_FALSE(h.conn.ghostDelivered(3));
		}
	}
}

SCENARIO("The world file")
{
	const auto world = parseWorld(
		"# a test world\n"
		"sun 0 0.5 -1  1 1 1 1  0.2 0.2 0.3 1\n"
		"object Ground ground.dts 0 0 0\n"
		"object Tower tower.dif 5 6 7  0 0 1 1.57   # turned a quarter\r\n"
		"playershape player.dts\n"
		"spawn 1 2 3\n"
		"object Broken\n");

	THEN("objects, the sun, the player shape and the spawn are read, and bad lines reported")
	{
		REQUIRE(world.objects.size() == 3);
		REQUIRE(world.objects[0].kind == GhostObject::Kind::Sun);
		REQUIRE(world.objects[0].sun.direction[1] == 0.5f);
		REQUIRE(world.objects[0].sun.ambient[2] == 0.3f);
		REQUIRE(world.objects[1].object.name == "Ground");
		REQUIRE(world.objects[2].object.shape == "tower.dif");
		REQUIRE(world.objects[2].object.transform.position[2] == 7.0f);
		REQUIRE(world.objects[2].object.transform.angle == 1.57f);
		REQUIRE(world.playerShape == "player.dts");
		REQUIRE(world.spawn.position[1] == 2.0f);
		REQUIRE(world.errors.size() == 1);
		REQUIRE(world.errors[0].starts_with("line 7"));
	}
}

SCENARIO("A Camera ghost becomes the client's camera once it is in")
{
	Harness h;
	const auto& profile = h.profile;

	GhostObject camera;
	camera.kind = GhostObject::Kind::Camera;
	camera.object.name = "G3DCamera_guest";
	camera.object.transform.position = {0.0f, -3.0f, 2.0f};
	h.conn.setGhost(7, camera);
	h.conn.setCameraObject(7);
	h.conn.receive(kFirstData, h.now);
	h.tick(60);
	REQUIRE_FALSE(readServerPacket(h.sent.back()).camera);
	h.phase1Ack();

	const auto first = readServerPacket(h.sent.back());
	THEN("it is created as a Camera, and not named before the client has it")
	{
		REQUIRE_FALSE(first.camera);
		REQUIRE(first.ghosts.size() == 1);
		REQUIRE(first.ghosts[0].classId == *objectClassId(profile, "Camera"));
		PayloadReader p(first.ghosts[0].payload);
		GameObjectState got;
		std::string moveType;
		readGameObjectUpdate(p.r, got, moveType);
		REQUIRE(got == camera.object);
		REQUIRE_FALSE(p.r.readFlag()); // camera mode
		REQUIRE_FALSE(p.r.readFlag()); // followed object
		REQUIRE(p.r.bitPosition() == first.ghosts[0].bits);
	}

	WHEN("the create is acked")
	{
		h.conn.receive(clientDataPacket(profile, 3, 2, 3, {}, 0), h.now);
		h.tick(60);
		h.tick(1000);
		THEN("every packet names it as the camera")
		{
			const auto pkt = readServerPacket(h.sent.back());
			REQUIRE(pkt.camera);
			REQUIRE(pkt.cameraIndex == *h.conn.ghostIndex(7));
		}

		AND_WHEN("it is removed")
		{
			h.conn.removeGhost(7);
			h.tick(1000);
			THEN("the packet with its delete no longer names it")
			{
				const auto pkt = readServerPacket(h.sent.back());
				REQUIRE_FALSE(pkt.camera);
				REQUIRE(pkt.ghosts.size() == 1);
				REQUIRE(pkt.ghosts[0].deleted);
			}
		}
	}

	THEN("the world file's camera line sets the offset")
	{
		const auto world = parseWorld("camera 0 -3 2\n");
		REQUIRE(world.errors.empty());
		REQUIRE(world.cameraOffset);
		REQUIRE((*world.cameraOffset)[1] == -3.0f);
		REQUIRE_FALSE(world.cameraRotation);
	}

	THEN("the camera line may turn the camera")
	{
		const auto world = parseWorld("camera 0 -8 3 1 0 0 -0.3\ncamera 0 -8 3 1 0 0\n");
		REQUIRE(world.errors.size() == 1); // the second line has no angle
		REQUIRE(world.cameraRotation);
		REQUIRE((*world.cameraRotation)[3] == -0.3f);
		const auto t = cameraTransform(world);
		REQUIRE(t.position == std::array<float, 3>{0.0f, -8.0f, 3.0f});
		REQUIRE(t.angle == -0.3f);
	}
}

SCENARIO("Without a camera rotation the Camera faces the spawn point")
{
	const auto near = [](std::array<float, 3> a, std::array<float, 3> b) {
		for (size_t i = 0; i < 3; ++i)
			if (std::abs(a[i] - b[i]) > 1e-4f)
				return false;
		return true;
	};
	const std::array<float, 3> from{0.0f, -5.0f, 3.0f};
	for (const auto& to : std::vector<std::array<float, 3>>{
			 {0.0f, 0.0f, 1.0f}, {3.0f, 4.0f, 0.0f}, {-2.0f, -9.0f, 3.0f}, {0.0f, 5.0f, 3.0f}, {0.0f, -5.0f, -4.0f}})
	{
		const auto t = facing(from, to);
		std::array<float, 3> d{to[0] - from[0], to[1] - from[1], to[2] - from[2]};
		const float l = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
		REQUIRE(near(forwardOf(t), {d[0] / l, d[1] / l, d[2] / l}));
		REQUIRE(t.position == from);
	}
	REQUIRE(facing(from, {0.0f, 0.0f, 3.0f}).angle == 0.0f);

	// What the lab saw: +angle about +x looks down, about +z turns right.
	Transform down;
	down.axis = {1.0f, 0.0f, 0.0f};
	down.angle = 0.38f;
	REQUIRE(forwardOf(down)[2] < 0.0f);
	Transform right;
	right.angle = 0.3f;
	REQUIRE(forwardOf(right)[0] > 0.0f);

	GIVEN("a world with a spawn point and no camera line")
	{
		const auto world = parseWorld("spawn 1 2 3\n");
		const auto t = cameraTransform(world);
		REQUIRE(t.position == std::array<float, 3>{1.0f, -3.0f, 5.0f});
		REQUIRE(near(forwardOf(t), {0.0f, 5.0f / std::sqrt(29.0f), -2.0f / std::sqrt(29.0f)}));
	}
}

SCENARIO("A Sky follows the 5.12 Sky::packUpdate layout")
{
	GIVEN("a world file with a sky and its options")
	{
		const auto world = parseWorld("sky a,b,c,d,e,f 800 0.1 0.2 0.3\nskyoptions 0 1 0 0.5 0.5 1\nskyoptions 1 1 1 0 0 0 0\n");
		REQUIRE(world.errors.size() == 1); // the second skyoptions line has a word too many
		REQUIRE(world.objects.size() == 1);
		const auto& sky = world.objects[0];
		REQUIRE(sky.kind == GhostObject::Kind::Sky);
		REQUIRE(sky.className() == "Sky");
		REQUIRE(sky.sky.materialList == "a,b,c,d,e,f");
		REQUIRE(sky.sky.visibleDistance == 800.0f);
		REQUIRE(sky.sky.fogColor[2] == 0.3f);
		REQUIRE(sky.sky.bools == std::array<uint8_t, 3>{0, 1, 0});
		REQUIRE(sky.sky.solidColor[2] == 1.0f);

		THEN("its update reads back field for field, to the last bit")
		{
			BitWriter w;
			w.setPacketStrings(true);
			sky.writeUpdate(w, sky.fullMask());

			std::string buffer;
			const auto data = w.toString();
			BitReader r(data);
			r.setStringBuffer(&buffer);
			REQUIRE(readSkyUpdate(r) == sky.sky);
			REQUIRE(r.bitPosition() == w.bitPosition());
		}
	}

	THEN("weapon3d lines name a file or a server weapon")
	{
		const auto world = parseWorld("weapon3d -move3d config/move3d.gs2\nweapon3d -System\nweapon3d\n");
		REQUIRE(world.errors.size() == 1);
		REQUIRE(world.weapons.size() == 2);
		REQUIRE(world.weapons[0].name == "-move3d");
		REQUIRE(world.weapons[0].file == "config/move3d.gs2");
		REQUIRE(world.weapons[1].file.empty());
	}

	THEN("protocol 21 and 24 both have a Sky class")
	{
		REQUIRE(objectClassId(*profileForProtocol(24), "Sky"));
		REQUIRE(objectClassId(*profileForProtocol(21), "Sky"));
	}
}

SCENARIO("A ghost can be sent whole again")
{
	Harness h;
	GhostObject sky;
	sky.kind = GhostObject::Kind::Sky;
	sky.sky.materialList = "sky.dml";
	h.conn.setGhost(1, sky);
	h.conn.resendGhost(1); // not created yet: nothing extra
	h.conn.receive(kFirstData, h.now);
	h.tick(60);
	h.phase1Ack();
	REQUIRE(readServerPacket(h.sent.back()).ghosts.size() == 1);

	h.sent.clear();
	h.conn.receive(clientDataPacket(h.profile, 3, 2, 3, {}, 0), h.now);
	h.tick(60);
	h.tick(1000);
	for (const auto& data : h.sent)
		REQUIRE(readServerPacket(data).ghosts.empty());

	WHEN("it is resent once the client has it")
	{
		h.sent.clear();
		h.conn.resendGhost(1);
		h.tick(1000);
		THEN("the next packet carries its whole update")
		{
			const auto pkt = readServerPacket(h.sent.back());
			REQUIRE(pkt.ghosts.size() == 1);
			PayloadReader p(pkt.ghosts[0].payload);
			REQUIRE(readSkyUpdate(p.r) == sky.sky);
		}
	}
}

SCENARIO("Bit counts and patches stay inside the stream")
{
	// 32 bits each way is the widest value the stream stores.
	BitWriter exact;
	exact.writeInt(0xFFFFFFFFu, 32);
	REQUIRE(exact.bitPosition() == 32);
	BitReader exactRead(exact.bytes().data(), exact.bytes().size());
	REQUIRE(exactRead.readInt(32) == 0xFFFFFFFFu);

	// A wider count used to shift a 32-bit value by 32.
	BitWriter wide;
	wide.writeInt(0xFFFFFFFFu, 40);
	REQUIRE(wide.bitPosition() == 40);
	BitReader wideRead(wide.bytes().data(), wide.bytes().size());
	REQUIRE(wideRead.readInt(40) == 0xFFFFFFFFu);
	REQUIRE(wideRead.bitPosition() == 40);

	unsigned char ones[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	BitReader onesRead(ones, sizeof ones);
	REQUIRE(onesRead.readInt(40) == 0xFFFFFFFFu);

	// Patching a position the buffer does not have used to index an empty vector.
	BitWriter empty;
	empty.patchInt(0, 1, 8);
	REQUIRE(empty.byteSize() == 0);
	empty.patchInt(0, 1, -3);
	REQUIRE(empty.byteSize() == 0);

	BitWriter patched;
	patched.writeInt(0, 8);
	patched.patchInt(0, 0xA5u, 8);
	patched.patchInt(0, 0xFFFFFFFFu, 16);
	REQUIRE(patched.byteSize() == 1);
	BitReader patchedRead(patched.bytes().data(), patched.bytes().size());
	REQUIRE(patchedRead.readInt(8) == 0xFFu);

	uint32_t state = 0xA5A5u;
	for (int n = 0; n < 64; ++n)
	{
		state = state * 1664525u + 1013904223u;
		unsigned char raw[16];
		const int len = static_cast<int>(state % 16);
		for (int i = 0; i < len; ++i)
		{
			state = state * 1664525u + 1013904223u;
			raw[i] = static_cast<unsigned char>(state);
		}
		BitReader fuzz(raw, static_cast<size_t>(len));
		fuzz.readInt(static_cast<int>(state % 48));
		fuzz.readString();
		fuzz.readBytes(static_cast<size_t>(state % 32));
	}
}

SCENARIO("Raw lab fields parse and write in order")
{
	std::string error;
	const auto fields = parseRawFields({"f1", "i4:9", "u16:0x1234", "i10:-1", "f32:1.5", "str:a%20b", "hex@10:0a0b", "f0"}, error);
	REQUIRE(fields);
	REQUIRE(fields->size() == 8);

	BitWriter w;
	writeRawFields(w, *fields);
	REQUIRE(rawFieldBits(*fields) == w.bitPosition());

	const auto bytes = w.toString();
	BitReader r(bytes);
	REQUIRE(r.readFlag());
	REQUIRE(r.readInt(4) == 9);
	REQUIRE(r.readInt(16) == 0x1234);
	REQUIRE(r.readInt(10) == 0x3FF);
	REQUIRE(r.readU32() == 0x3FC00000u); // 1.5f
	REQUIRE(r.readString() == "a b");
	REQUIRE(r.readInt(10) == 2);
	REQUIRE(r.readBytes(2) == std::string("\x0a\x0b"));
	REQUIRE_FALSE(r.readFlag());
	REQUIRE_FALSE(r.overrun());

	THEN("bad tokens are refused with a reason")
	{
		for (std::string_view bad : {"i4:16", "i33:0", "u8:x", "str:%2", "hex:abc", "f32:", "q:1", "u8@4:1", "file@40:x", "f2"})
		{
			error.clear();
			REQUIRE_FALSE(parseRawFields({bad}, error));
			REQUIRE_FALSE(error.empty());
		}
	}
}

SCENARIO("Raw fields read files, zlib-compressed on request")
{
	std::string error;
	auto fields = parseRawFields({"i4:3", "zfile@10:chunk.bin", "file:missing.bin"}, error);
	REQUIRE(fields);
	const std::string chunk(768, '\x07');
	const auto errors = loadRawFiles(*fields, [&](const std::string& path) -> std::optional<std::string> {
		if (path == "chunk.bin")
			return chunk;
		return std::nullopt;
	});
	REQUIRE(errors.size() == 1);
	REQUIRE(errors[0] == "cannot read missing.bin");

	BitWriter w;
	writeRawFields(w, *fields);
	const auto bytes = w.toString();
	BitReader r(bytes);
	REQUIRE(r.readInt(4) == 3);
	const auto length = r.readInt(10);
	const auto packed = r.readBytes(length);
	std::string unpacked(chunk.size(), '\0');
	uLongf size = static_cast<uLongf>(unpacked.size());
	REQUIRE(uncompress(reinterpret_cast<Bytef*>(unpacked.data()), &size, reinterpret_cast<const Bytef*>(packed.data()),
					   static_cast<uLong>(packed.size())) == Z_OK);
	REQUIRE(unpacked == chunk);

	THEN("a file too long for its length prefix is an error")
	{
		auto small = parseRawFields({"file@4:big.bin"}, error);
		REQUIRE(small);
		const auto errs = loadRawFiles(*small, [](const std::string&) { return std::optional<std::string>(std::string(16, 'x')); });
		REQUIRE(errs.size() == 1);
	}
}

SCENARIO("A replicator line ghosts ground cover in the 5.12 layout")
{
	const auto world = parseWorld("replicator DynamicGrass 10 -20 1.5 4 1 3 grass.dts 7\n"
								  "replicator DynamicGrass 10 -20 1.5 65 1 3 grass.dts\n"  // radius past 64
								  "replicator DynamicGrass 10 -20 1.5 4 5 3 grass.dts\n"   // min above max
								  "replicator Sky 10 -20 1.5 4 1 3 grass.dts\n");          // not a replicator
	REQUIRE(world.objects.size() == 1);
	REQUIRE(world.errors.size() == 3);
	const auto& object = world.objects[0];
	REQUIRE(object.className() == "DynamicGrass");
	REQUIRE(object.layout512);

	BitWriter w;
	writeRawFields(w, object.raw);
	const auto bytes = w.toString();
	BitReader r(bytes);
	for (int i = 0; i < 13; ++i)
		REQUIRE_FALSE(r.readFlag()); // empty GameObject update
	REQUIRE(r.readFlag());
	REQUIRE(r.readF32() == 10.0f);
	REQUIRE(r.readF32() == -20.0f);
	REQUIRE(r.readF32() == 1.5f);
	for (int i = 0; i < 3; ++i)
		REQUIRE(r.readF32() == 0.0f);
	REQUIRE_FALSE(r.readFlag());
	REQUIRE(r.readU32() == 7);
	REQUIRE(r.readString() == "grass.dts");
	REQUIRE(r.readInt(8) == 4);
	REQUIRE(r.readInt(8) == 1);
	REQUIRE(r.readInt(8) == 3);
	const auto signedInt = [&](int bits) {
		const bool negative = r.readFlag();
		const int v = static_cast<int>(r.readInt(bits - 1));
		return negative ? -v : v;
	};
	for (const int v : {-1, -1, 0, 1, 1, 2})
		REQUIRE(signedInt(16) == v);
	for (const float v : {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 360.0f, 0.0f})
		REQUIRE(r.readF32() == v);
	for (const bool v : {true, false, false, false, false})
		REQUIRE(r.readFlag() == v);
	REQUIRE(signedInt(8) == 90);
	REQUIRE_FALSE(r.readFlag());
	for (const float v : {0.0f, 0.0f, 1.0f})
		REQUIRE(r.readF32() == v);
	REQUIRE_FALSE(r.readFlag());
	REQUIRE(signedInt(8) == 0);
	REQUIRE_FALSE(r.readFlag());
	REQUIRE(rawFieldBits(object.raw) == w.bitPosition());
}

SCENARIO("The world file takes rawobject and rawevent lines")
{
	const auto world = parseWorld(
		"rawobject TerrainBlock f1 str:terrain.ter u32:256\n"
		"rawevent TerrainChunkEvent i4:1 i4:2 u32:7 hex@10:789c0300000000001\n" // odd hex: an error
		"rawevent TerrainChunkEvent i4:1 i4:2 u32:7 hex@10:00\n"
		"rawevent late TerrainChunkEvent f1\n"
		"rawevent late\n");
	REQUIRE(world.objects.size() == 1);
	REQUIRE(world.objects[0].kind == GhostObject::Kind::Raw);
	REQUIRE(world.objects[0].className() == "TerrainBlock");
	// A square size past 64 is refused: a huge one killed a live 5.12.
	REQUIRE(parseWorld("terrain world/terrain 64\n").terrain);
	REQUIRE_FALSE(parseWorld("terrain world/terrain 65\n").terrain);
	REQUIRE_FALSE(parseWorld("terrain world/terrain 2147483647\n").terrain);
	REQUIRE(world.objects[0].raw.size() == 3);
	REQUIRE(world.rawEvents.size() == 2);
	REQUIRE_FALSE(world.rawEvents[0].late);
	REQUIRE(world.rawEvents[0].className == "TerrainChunkEvent");
	REQUIRE(world.rawEvents[1].late);
	REQUIRE(world.errors.size() == 2);
}

SCENARIO("A raw object is ghosted with its fields, and a raw event is posted")
{
	Harness h;
	std::string error;
	GhostObject terrain;
	terrain.kind = GhostObject::Kind::Raw;
	terrain.rawClass = "TerrainBlock";
	terrain.raw = *parseRawFields({"f1", "str:lab.ter", "u32:256"}, error);
	h.conn.setGhost(1, terrain);

	GhostObject unknown = terrain;
	unknown.rawClass = "NoSuchClass";
	h.conn.setGhost(2, unknown);

	REQUIRE(h.conn.postRawEvent("TerrainChunkEvent", *parseRawFields({"i4:5", "i4:6", "u32:99"}, error)));
	REQUIRE_FALSE(h.conn.postRawEvent("NoSuchEvent", {}));

	h.conn.receive(kFirstData, h.now);
	h.tick(60);
	const auto pkt = readServerPacket(h.sent.back()); // the event; the ghost waits for MissionStartPhase1Ack
	h.phase1Ack();
	const auto ghostPkt = readServerPacket(h.sent.back());

	THEN("the ghost carries exactly the fields")
	{
		const auto& pkt = ghostPkt;
		REQUIRE(pkt.ghosts.size() == 1);
		REQUIRE(pkt.ghosts[0].classId == *objectClassId(h.profile, "TerrainBlock"));
		PayloadReader p(pkt.ghosts[0].payload);
		REQUIRE(p.r.readFlag());
		REQUIRE(p.r.readString() == "lab.ter");
		REQUIRE(p.r.readU32() == 256);
		REQUIRE(p.r.bitPosition() == pkt.ghosts[0].bits);
	}

	THEN("the event carries exactly the fields")
	{
		const auto id = *eventClassId(h.profile, "TerrainChunkEvent");
		const ClientEvent* chunk = nullptr;
		for (const auto& ev : pkt.events)
			if (ev.classId == id)
				chunk = &ev;
		REQUIRE(chunk);
		REQUIRE(chunk->bits == 4 + 4 + 32);
		BitReader r(chunk->payload);
		REQUIRE(r.readInt(4) == 5);
		REQUIRE(r.readInt(4) == 6);
		REQUIRE(r.readU32() == 99);
	}
}

SCENARIO("A Camera as the control object: its two extra sections are read")
{
	// A live 5.12 writes the Camera's mode and followed-object flags after its
	// GameObject update (T3D-023, lab 2026-11-25); the events after them must read.
	Harness h;
	GhostObject camera;
	camera.kind = GhostObject::Kind::Camera;
	camera.object.name = "G3DCamera_guest";
	h.conn.setGhost(7, camera);
	h.conn.setControlObject(7);
	h.conn.receive(kFirstData, h.now);
	h.tick(60);
	h.phase1Ack();

	BitWriter echo;
	echo.setPacketStrings(true);
	writeGameObjectUpdate(echo, camera.object, GameObjectBits::Transform);
	echo.writeFlag(false); // mode
	echo.writeFlag(false); // followed object
	ClientControl control{{0.0f, 0.0f, 0.0f}, 0, &echo};
	std::vector<OutEvent> evs;
	evs.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "Hello"}})});
	h.conn.receive(clientDataPacket(h.profile, 3, 2, 3, evs, 1, &control), h.now);

	REQUIRE_FALSE(h.logs.empty());
	REQUIRE(h.logs.back() == "remote command ignored: Hello");
}

SCENARIO("Every event says its exact length")
{
	// 5.12 trusts an event's int14 length: one that claimed a bit more than it
	// carried ran garbled and crashed the client (fuzz tier 1, 2026-11-25).
	Harness h;
	std::string error;

	THEN("an event too long for its int14 length is not sent")
	{
		std::vector<std::string_view> words(520, "u32:7"); // 16640 bits
		REQUIRE_FALSE(h.conn.postRawEvent("TerrainChunkEvent", *parseRawFields(words, error)));
		REQUIRE(h.logs.back().find("longer than an event can say") != std::string::npos);
	}

	WHEN("the mission start command and a raw event go out")
	{
		REQUIRE(h.conn.postRawEvent("TerrainChunkEvent", *parseRawFields({"i4:5", "i4:6", "u32:99"}, error)));
		h.conn.receive(kFirstData, h.now);
		h.tick(60);
		const auto pkt = readServerPacket(h.sent.back());

		THEN("each one's length is what its reader takes")
		{
			REQUIRE(pkt.events.size() >= 2);
			const auto command = *eventClassId(h.profile, "RemoteCommandEvent");
			const auto chunk = *eventClassId(h.profile, "TerrainChunkEvent");
			bool sawCommand = false, sawChunk = false;
			for (const auto& ev : pkt.events)
			{
				PayloadReader p(ev.payload);
				if (ev.classId == command)
				{
					const auto args = p.r.readInt(CommandArgsBits);
					for (uint32_t i = 0; i < args; ++i)
						unpackCommandArg(p.r);
					REQUIRE(p.r.bitPosition() == ev.bits);
					sawCommand = true;
				}
				else if (ev.classId == chunk)
				{
					p.r.readInt(4);
					p.r.readInt(4);
					p.r.readU32();
					REQUIRE(p.r.bitPosition() == ev.bits);
					sawChunk = true;
				}
			}
			REQUIRE(sawCommand);
			REQUIRE(sawChunk);
		}
	}
}

SCENARIO("A TerrainRequestUpdatesEvent from the client is logged with its stamps")
{
	Harness h;
	h.conn.receive(kFirstData, h.now);
	h.tick(60);

	BitWriter payload;
	for (int i = 0; i < 256; ++i)
		payload.writeU32(i == 17 ? 42u : 0u);
	std::vector<OutEvent> events;
	events.push_back(OutEvent{"TerrainRequestUpdatesEvent", std::move(payload)});
	h.conn.receive(clientDataPacket(h.profile, 2, 1, 1, events, 0), h.now);

	bool found = false;
	for (const auto& l : h.logs)
		if (l.find("TerrainRequestUpdatesEvent: 8192 bits, 1 chunks with a stamp (1,1)=42") != std::string::npos)
			found = true;
	REQUIRE(found);
	REQUIRE(h.terrainRequests.size() == 1);
	REQUIRE(h.terrainRequests[0][17] == 42);
	REQUIRE(h.terrainRequests[0][0] == 0);
}

SCENARIO("A terrain line ghosts a TerrainBlock and loads the chunk files")
{
	const auto world = parseWorld("terrain world/terrain 4 repeat\nterrain again\n");
	REQUIRE(world.terrain);
	REQUIRE(world.terrain->folder == "world/terrain");
	REQUIRE(world.terrain->squareSize == 4);
	REQUIRE(world.terrain->repeat);
	REQUIRE(world.errors.size() == 1); // one terrain only
	REQUIRE(world.objects.size() == 1);
	REQUIRE(world.objects[0].className() == "TerrainBlock");

	THEN("the TerrainBlock update is the 5.12 layout")
	{
		BitWriter w;
		writeRawFields(w, world.objects[0].raw);
		const auto bytes = w.toString();
		BitReader r(bytes);
		REQUIRE(r.readFlag());
		REQUIRE(r.readString() == "Terrain");
		REQUIRE(r.readU32() == 0);
		REQUIRE(r.readString().empty());
		REQUIRE(r.readString().empty());
		REQUIRE(r.readU32() == 0x3F800000u);
		REQUIRE(r.readU32() == 0x3C23D70Au);
		REQUIRE(r.readU32() == 8);
		REQUIRE(r.readU32() == 4);
		REQUIRE(r.readInt(8) == 1);
		REQUIRE(r.bitPosition() == rawFieldBits(world.objects[0].raw));
	}

	THEN("each chunk file is kept with Torque's stamp of its inflated bytes")
	{
		Terrain t = *world.terrain;
		const std::string plain = "chunk data";
		std::string packed(64, '\0');
		uLongf size = static_cast<uLongf>(packed.size());
		REQUIRE(compress(reinterpret_cast<Bytef*>(packed.data()), &size, reinterpret_cast<const Bytef*>(plain.data()), plain.size()) == Z_OK);
		packed.resize(size);
		const auto errors = loadTerrain(t, [&](const std::string& path) -> std::optional<std::string> {
			if (path == "world/terrain/heights3-1.dat")
				return packed;
			if (path == "world/terrain/heights0-0.dat")
				return std::string("not zlib");
			if (path == "world/terrain/heights15-15.dat")
				return std::string(1024, 'x');
			return std::nullopt;
		});
		REQUIRE(errors.size() == 2);
		REQUIRE(t.chunks[1 * 16 + 3]);
		REQUIRE(t.chunks[1 * 16 + 3]->packed == packed);
		REQUIRE(t.chunks[1 * 16 + 3]->stamp == (static_cast<uint32_t>(crc32(0, reinterpret_cast<const Bytef*>(plain.data()), plain.size())) ^ 0xFFFFFFFFu));
		REQUIRE_FALSE(t.chunks[0]);
		REQUIRE_FALSE(t.chunks[255]);
	}
}

SCENARIO("An object line's last three numbers are its scale, section 2 of the update")
{
	const auto world = parseWorld("object Big tree.3ds 1 2 3  0 0 1 0  2 2 3\nobject Plain tree.3ds 0 0 0\n");
	REQUIRE(world.errors.empty());
	REQUIRE(world.objects.size() == 2);
	REQUIRE(world.objects[0].object.scale == std::array<float, 3>{2, 2, 3});
	REQUIRE(world.objects[1].object.scale == std::array<float, 3>{1, 1, 1});
	REQUIRE((gameObjectFullMask(world.objects[0].object) & GameObjectBits::Scale) != 0);
	REQUIRE((gameObjectFullMask(world.objects[1].object) & GameObjectBits::Scale) == 0);

	BitWriter w;
	writeGameObjectUpdate(w, world.objects[0].object, gameObjectFullMask(world.objects[0].object));
	const auto bytes = w.toString();
	BitReader r(bytes);
	REQUIRE(r.readFlag());
	(void)r.readString();
	REQUIRE(r.readFlag()); // section 2
	REQUIRE(r.readF32() == 2.0f);
	REQUIRE(r.readF32() == 2.0f);
	REQUIRE(r.readF32() == 3.0f);
}

namespace
{
	std::string chunk3ds(uint16_t id, const std::string& body)
	{
		std::string out;
		const uint32_t length = static_cast<uint32_t>(body.size() + 6);
		out.push_back(static_cast<char>(id & 0xFF));
		out.push_back(static_cast<char>(id >> 8));
		for (int i = 0; i < 4; ++i)
			out.push_back(static_cast<char>((length >> (8 * i)) & 0xFF));
		return out + body;
	}
} // namespace

SCENARIO("A .3ds the real client crashes on is found before it is served")
{
	const auto mesh = chunk3ds(0x4000, std::string("box\0", 4) + chunk3ds(0x4100, chunk3ds(0x4110, std::string(2, '\0'))));
	const auto material = chunk3ds(0xAFFF, chunk3ds(0xA000, std::string("m\0", 2)));
	const auto good = chunk3ds(0x4D4D, chunk3ds(0x3D3D, material + mesh));
	REQUIRE_FALSE(unsafe3ds(good));

	REQUIRE(unsafe3ds(chunk3ds(0x4D4D, chunk3ds(0x3D3D, mesh))) == "it has meshes but no materials");
	REQUIRE(unsafe3ds(good.substr(0, good.size() / 2)));               // cut in half
	std::string longer = good;
	longer[2] = static_cast<char>(longer[2] + 100);                    // main length past the end
	REQUIRE(unsafe3ds(longer));
	std::string inner = good;
	inner[8] = static_cast<char>(inner[8] + 50);                       // the editor chunk past its parent
	REQUIRE(unsafe3ds(inner));
	REQUIRE(unsafe3ds("not a 3ds") == "not a 3D Studio file");
}

SCENARIO("A .3ds mesh whose faces do not fit its lists is found before it is served")
{
	const auto material = chunk3ds(0xAFFF, chunk3ds(0xA000, std::string("m\0", 2)));
	const auto le16 = [](uint16_t v) { return std::string{static_cast<char>(v & 0xFF), static_cast<char>(v >> 8)}; };
	const auto vertices = chunk3ds(0x4110, le16(3) + std::string(3 * 12, '\0'));
	const auto faces = [&](uint16_t count, uint16_t index, size_t present) {
		std::string body = le16(count);
		for (size_t i = 0; i < present; ++i)
			body += le16(0) + le16(1) + le16(index) + le16(0);
		return chunk3ds(0x4120, body);
	};
	const auto file = [&](const std::string& lists) {
		const auto mesh = chunk3ds(0x4000, std::string("box\0", 4) + chunk3ds(0x4100, lists));
		return chunk3ds(0x4D4D, chunk3ds(0x3D3D, material + mesh));
	};
	REQUIRE_FALSE(unsafe3ds(file(vertices + faces(1, 2, 1))));
	REQUIRE(unsafe3ds(file(vertices + faces(5, 2, 1))));                // more faces claimed than present
	REQUIRE(unsafe3ds(file(vertices + faces(1, 3, 1))));                // vertex 3 of 3
	REQUIRE(unsafe3ds(file(chunk3ds(0x4110, le16(9) + std::string(12, '\0')))));  // more vertices claimed
	REQUIRE(unsafe3ds(file(faces(1, 0, 1))));                           // faces with no vertices
}

SCENARIO("A .dts or PNG the real 5.12 crashes on is found before it is served")
{
	const auto le32 = [](uint32_t v) {
		std::string out;
		for (int i = 0; i < 4; ++i)
			out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
		return out;
	};
	const std::string dts = le32(24) + le32(4) + le32(2) + le32(3) + std::string(16, '\0') + std::string(8, 'x');
	REQUIRE_FALSE(unsafeDts(dts));
	REQUIRE(unsafeDts(dts.substr(0, dts.size() * 3 / 10)));
	REQUIRE(unsafeDts(dts.substr(0, 20)));                              // cut inside the buffer
	REQUIRE(unsafeDts(le32(24) + le32(0x40000000) + le32(0) + le32(0) + std::string(16, '\0')));
	REQUIRE(unsafeDts(le32(24) + le32(4) + le32(3) + le32(2) + std::string(16, '\0')));  // parts out of order
	for (const uint32_t version : {16u, 17u, 19u, 22u, 23u, 25u})                // only 24 draws on 5.12
		REQUIRE(unsafeDts(le32(version) + dts.substr(4)));
	for (const uint32_t count : {5u, 0x7FFFFFFFu, 0xFFFFFFFFu})                  // node count past the buffer
		REQUIRE(unsafeDts(dts.substr(0, 16) + le32(count) + dts.substr(20)));
	REQUIRE_FALSE(unsafeDts(dts.substr(0, 16) + le32(4) + dts.substr(20)));
	REQUIRE(unsafeAsset(".DTS", dts.substr(0, 10)));

	const auto be32 = [](uint32_t v) {
		std::string out;
		for (int i = 3; i >= 0; --i)
			out.push_back(static_cast<char>((v >> (8 * i)) & 0xFF));
		return out;
	};
	const auto png = [&](uint32_t w, uint32_t h) { return std::string("\x89PNG\r\n\x1a\n", 8) + be32(13) + "IHDR" + be32(w) + be32(h) + std::string(5, '\0'); };
	REQUIRE_FALSE(unsafeImage(png(256, 256)));
	REQUIRE(unsafeImage(png(30000, 30000)));
	REQUIRE(unsafeImage(png(0, 16)));
	REQUIRE(unsafeImage(png(256, 256).substr(0, 20)));
	REQUIRE_FALSE(unsafeImage("GIF89a"));
	REQUIRE(pngSize(png(256, 128)) == std::pair<uint32_t, uint32_t>{256, 128});
	REQUIRE_FALSE(pngSize("GIF89a"));                               // only PNG is checked
	REQUIRE(unsafeAsset(".png", png(30000, 30000)));
}

SCENARIO("A Sky's textures may be named with their extensions")
{
	REQUIRE(skyTextureNames("sky_fr.png,sky_rt.JPG,sky_bk,clouds.v2.gif") == "sky_fr,sky_rt,sky_bk,clouds.v2");
	REQUIRE(skyTextureNames("plain") == "plain");
	const auto world = parseWorld("sky sky_fr.png,sky_up.png 500 0.6 0.7 0.9\n");
	REQUIRE(world.objects.size() == 1);
	REQUIRE(world.objects[0].sky.materialList == "sky_fr,sky_up");
}

SCENARIO("A water line ghosts a WaterBlock in the 5.12 layout")
{
	const auto world = parseWorld("water -1024 -1024 0.5 2048 2048 20 lake 1\nwater 1 2 3\n");
	REQUIRE(world.objects.size() == 1);
	REQUIRE(world.errors.size() == 1);
	REQUIRE(world.objects[0].className() == "WaterBlock");
	BitWriter w;
	writeRawFields(w, world.objects[0].raw);
	const auto bytes = w.toString();
	BitReader r(bytes);
	REQUIRE(r.readFlag());
	REQUIRE(r.readString() == "Water");
	REQUIRE(r.readF32() == -1024.0f);
	REQUIRE(r.readF32() == -1024.0f);
	REQUIRE(r.readF32() == 0.5f);
	for (int i = 0; i < 3; ++i)
		REQUIRE(r.readF32() == 0.0f);
	REQUIRE_FALSE(r.readFlag());
	REQUIRE(r.readF32() == 2048.0f);
	REQUIRE(r.readF32() == 2048.0f);
	REQUIRE(r.readF32() == 20.0f);
	REQUIRE(r.readString() == "lake");
	for (int i = 0; i < 6; ++i)
		REQUIRE(r.readString().empty());
	REQUIRE(r.readU32() == 1);
	// 5 floats, 2 bytes, 12 floats, 4 colour bytes, a float, a byte.
	REQUIRE(rawFieldBits(world.objects[0].raw) == r.bitPosition() + 5 * 32 + 16 + 12 * 32 + 32 + 32 + 8);
}

SCENARIO("A TerrainChunkEvent carries x, y, the stamp and the zlib bytes")
{
	Harness h;
	const std::string packed = "\x78\x9c\x03\x00\x00\x00\x00\x01";
	REQUIRE(h.conn.postTerrainChunk(5, 14, 0xDEADBEEF, packed));
	REQUIRE_FALSE(h.conn.postTerrainChunk(0, 0, 0, std::string(1024, 'x')));
	h.conn.receive(kFirstData, h.now);
	h.tick(60);
	const auto pkt = readServerPacket(h.sent.back());
	const auto id = *eventClassId(h.profile, "TerrainChunkEvent");
	const ClientEvent* chunk = nullptr;
	for (const auto& ev : pkt.events)
		if (ev.classId == id)
			chunk = &ev;
	REQUIRE(chunk);
	REQUIRE(chunk->bits == 4 + 4 + 32 + 10 + 8 * packed.size());
	BitReader r(chunk->payload);
	REQUIRE(r.readInt(4) == 5);
	REQUIRE(r.readInt(4) == 14);
	REQUIRE(r.readU32() == 0xDEADBEEF);
	REQUIRE(r.readInt(10) == packed.size());
	REQUIRE(r.readBytes(packed.size()) == packed);
}

SCENARIO("The Sharp3D route sends the world as ghost-always events and controls the Camera")
{
	const Profile& profile = *sharp3dProfileForProtocol(24);
	REQUIRE(profile.ghostAlwaysWorld);
	REQUIRE_FALSE(sharp3dProfileForProtocol(21));
	REQUIRE_FALSE(eventClassId(profile, "RemoteCommandEvent"));
	REQUIRE_FALSE(eventClassId(profile, "NetStringEvent"));
	REQUIRE(*objectClassId(profile, "Sun") == 0);
	REQUIRE(*objectClassId(profile, "GameObject") == 1);
	REQUIRE(*objectClassId(profile, "Camera") == 2);
	REQUIRE(*objectClassId(profile, "Sky") == 3);
	REQUIRE(*eventClassId(profile, "GhostAlwaysObjectEvent") == 3);

	uint64_t now = 1000;
	std::vector<std::string> sent;
	std::vector<MissionState> mission;
	std::vector<std::string> logs;
	Connection conn{profile, 0, now, Connection::Callbacks{
										 [&](std::string_view d) { sent.emplace_back(d); },
										 [](const TextMessage&) {},
										 [&](std::string_view l) { logs.emplace_back(l); },
										 [&](MissionState m) { mission.push_back(m); },
										 [](const ClientState&) {},
									 }};
	GhostObject sun;
	sun.kind = GhostObject::Kind::Sun;
	GhostObject ground;
	ground.object.name = "Ground";
	ground.object.shape = "isle.3ds";
	GhostObject camera;
	camera.kind = GhostObject::Kind::Camera;
	camera.object.name = "G3DCamera_lab";
	conn.setGhost(1, sun);
	conn.setGhost(2, ground);
	conn.setGhost(3, camera);
	conn.setCameraObject(3);
	conn.setControlObject(3);

	conn.receive(kFirstData, now);
	now += 60;
	REQUIRE(conn.update(now));
	REQUIRE(mission == std::vector<MissionState>{MissionState::Phase1Sent});
	const auto first = readServerPacket(sent.back(), true);

	THEN("every object is a GhostAlwaysObjectEvent and no command or ghost section goes out")
	{
		REQUIRE(first.ghosts.empty());
		REQUIRE_FALSE(first.control);
		REQUIRE(first.events.size() == 3);
		const std::vector<uint32_t> classes{0, 1, 2};
		for (size_t i = 0; i < 3; ++i)
		{
			const auto& ev = first.events[i];
			REQUIRE(ev.classId == *eventClassId(profile, "GhostAlwaysObjectEvent"));
			PayloadReader p(ev.payload);
			REQUIRE(p.r.readInt(12) == i); // ghost index
			REQUIRE(p.r.readFlag());
			REQUIRE(p.r.readInt(12) == classes[i]);
			const auto bits = p.r.readInt(14);
			REQUIRE(12 + 1 + 12 + 14 + bits == ev.bits);
			if (i == 1)
			{
				GameObjectState state;
				std::string moveType;
				readGameObjectUpdate(p.r, state, moveType);
				REQUIRE(state.shape == "isle.3ds");
			}
		}
	}

	WHEN("the client acks the events")
	{
		sent.clear();
		conn.receive(clientDataPacket(profile, 2, 1, 1, {}, 0), now);
		now += 60;
		REQUIRE(conn.update(now));

		THEN("the mission is done and the Camera is the control object in every packet")
		{
			REQUIRE(mission.back() == MissionState::Done);
			REQUIRE_FALSE(sent.empty());
			const auto pkt = readServerPacket(sent.back(), true);
			REQUIRE(pkt.control);
			REQUIRE(pkt.controlIndex == 2);
			REQUIRE(pkt.camera);
			REQUIRE(pkt.cameraIndex == 2);
			REQUIRE(pkt.ghosts.empty());

			conn.receive(clientDataPacket(profile, 3, 2, 1, {}, 0), now);
			now += 1000;
			REQUIRE(conn.update(now));
			const auto later = readServerPacket(sent.back(), true);
			REQUIRE(later.control);
			REQUIRE(later.controlIndex == 2);
		}

		THEN("a later change goes out in the ghost section")
		{
			ground.object.chat = "hi";
			conn.setGhost(2, ground);
			now += 1000;
			REQUIRE(conn.update(now));
			const auto pkt = readServerPacket(sent.back(), true);
			REQUIRE(pkt.ghosts.size() == 1);
			REQUIRE(pkt.ghosts[0].index == 1);
		}
	}
}

SCENARIO("Lab mutations (graal3dmutate) are parsed, seeded and sent once")
{
	GIVEN("the setting's text")
	{
		THEN("each kind parses, with a seed or 1")
		{
			REQUIRE(parseMutation("ghost-uncreated") == std::pair{Mutation::GhostUncreated, 1u});
			REQUIRE(parseMutation("accept-protocol:42") == std::pair{Mutation::AcceptProtocol, 42u});
			REQUIRE(mutationName(Mutation::ControlDeleted) == "control-deleted");
			REQUIRE_FALSE(parseMutation("nonsense"));
			REQUIRE_FALSE(parseMutation("ghost-uncreated:x"));
		}
	}

	GIVEN("an accept mutation")
	{
		const auto& profile = *profileForProtocol(24);
		THEN("the same seed gives the same accept, and it differs from the real one")
		{
			for (const auto m : {Mutation::AcceptProtocol, Mutation::AcceptClassTable})
			{
				for (uint32_t seed = 1; seed < 8; ++seed)
				{
					std::string what;
					const auto a = buildConnectAccept(0x400, profile, acceptTweak(m, seed, profile, &what));
					REQUIRE_FALSE(what.empty());
					REQUIRE(a == buildConnectAccept(0x400, profile, acceptTweak(m, seed, profile)));
					REQUIRE(a != buildConnectAccept(0x400, profile));
				}
			}
			REQUIRE(buildConnectAccept(0x400, profile, acceptTweak(Mutation::GhostUncreated, 1, profile)) == buildConnectAccept(0x400, profile));
		}
	}

	GIVEN("a packet mutation on a connection whose mission start is done")
	{
		for (const auto m : {Mutation::EventUnknownClass, Mutation::EventBadLength, Mutation::GhostUncreated, Mutation::GhostDoubleDelete,
							 Mutation::GhostIndexWidth, Mutation::ControlWrongClass, Mutation::ControlDeleted})
		{
			Harness h;
			h.conn.setMutation(m, 7);
			const auto& profile = h.profile;
			h.conn.receive(kFirstData, h.now);
			h.tick(60);
			std::vector<OutEvent> evs;
			evs.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "MissionStartPhase1Ack"}})});
			h.conn.receive(clientDataPacket(profile, 2, 1, 1, evs, 0), h.now);
			h.tick(60);
			std::vector<OutEvent> evs2;
			evs2.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "MissionStartPhase2Ack"}})});
			h.conn.receive(clientDataPacket(profile, 3, 2, 1, evs2, 1), h.now);
			REQUIRE(h.mission.back() == MissionState::Done);

			h.tick(60);
			REQUIRE_FALSE(h.conn.mutationSent());
			const auto line = [&] {
				for (const auto& l : h.logs)
				{
					if (l.starts_with("mutation "))
						return l;
				}
				return std::string();
			};
			REQUIRE(line().empty());
			h.tick(2100);
			REQUIRE(h.conn.mutationSent());
			const auto said = line();
			INFO(said);
			REQUIRE(said.starts_with("mutation " + std::string(mutationName(m)) + ":7 in seq "));
			REQUIRE(said.find("; packet ") != std::string::npos);
			// The control mutations need a ghost of another class, which this world
			// does not have; the rest always find something to change.
			if (m != Mutation::ControlWrongClass && m != Mutation::GhostIndexWidth)
				REQUIRE(said.find("nothing to mutate") == std::string::npos);
			// Once only.
			const auto count = std::ranges::count_if(h.logs, [](const std::string& l) { return l.starts_with("mutation "); });
			h.tick(3000);
			REQUIRE(std::ranges::count_if(h.logs, [](const std::string& l) { return l.starts_with("mutation "); }) == count);
		}
	}
}
