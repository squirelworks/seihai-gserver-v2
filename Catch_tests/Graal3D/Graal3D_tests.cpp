// Modified by the Seihai project, 2026-11-11, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: tests for the Graal 3D (Torque) UDP side. The client bytes are from the
// 2026-10-30 Wine lab, where the real 5.12 client joined a test 3D server
// (Graal_analysis torque/network.md, "The full handshake, live").
#include "catch2/catch_all.hpp"

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

	ServerPacket readServerPacket(std::string_view datagram)
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
		Connection conn{profile, 0x400, now, Connection::Callbacks{
												  [this](std::string_view d) { sent.emplace_back(d); },
												  [this](const TextMessage& m) { texts.push_back(m); },
												  [this](std::string_view l) { logs.emplace_back(l); },
												  [this](MissionState s) { mission.push_back(s); },
												  [this](const ClientState& c) { states.push_back(c); },
											  }};

		void tick(uint64_t ms)
		{
			now += ms;
			REQUIRE(conn.update(now));
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

SCENARIO("A 3.153 or 4.04r2 connection only keeps alive")
{
	const auto& profile = *profileForProtocol(19);
	std::vector<std::string> sent;
	Connection conn{profile, 0x401, 0, Connection::Callbacks{[&](std::string_view d) { sent.emplace_back(d); }, nullptr, nullptr, nullptr}};

	// A header-only data packet with the odd connect-sequence bit.
	BitWriter w;
	PacketHeader hdr;
	hdr.connectSequenceBit = 1;
	hdr.sequence = 1;
	writePacketHeader(w, hdr);
	conn.receive(w.toString(), 10);
	REQUIRE(conn.update(100));

	THEN("its data packet is acked with an ack packet and no events are sent")
	{
		REQUIRE(sent.size() == 1);
		BitReader r(sent[0]);
		auto h = readPacketHeader(r);
		REQUIRE(h);
		REQUIRE(h->type == PacketType::Ack);
		REQUIRE(h->connectSequenceBit == 1);
		REQUIRE(h->highestAck == 1);
		conn.sendText(TextMessage{"", "set", "", "x"});
		REQUIRE(conn.pendingEvents() == 0);
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

	REQUIRE(h.sent.size() == 1);
	const auto first = readServerPacket(h.sent[0]);

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
		h.conn.receive(clientDataPacket(profile, 2, 1, 1, {}, 0), h.now);
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

	WHEN("the client acks that packet and answers both mission phases")
	{
		std::vector<OutEvent> phase1;
		phase1.push_back({"RemoteCommandEvent", remoteCommandEvent({CommandArg{false, 0, "MissionStartPhase1Ack"}})});
		h.sent.clear();
		h.conn.receive(clientDataPacket(profile, 2, 1, 1, phase1, 0), h.now);
		h.tick(60);

		REQUIRE(h.sent.size() == 1);
		const auto phase2 = readServerPacket(h.sent.back());
		REQUIRE(h.mission.back() == MissionState::Phase2Sent);
		REQUIRE_FALSE(phase2.control); // MissionStartPhase2 would undo login step 5

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
		h.conn.receive(clientDataPacket(profile, 2, 1, 0, {}, 0), h.now);
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
		h.conn.receive(clientDataPacket(profile, 2, 1, 1, {}, 0), h.now);
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
	}
}

SCENARIO("A Sky follows the 5.12 Sky::packUpdate layout")
{
	GIVEN("a world file with a sky and its options")
	{
		const auto world = parseWorld("sky lab.dml 800 0.1 0.2 0.3\nskyoptions 0 1 0 0.5 0.5 1\nskyoptions 1 1 1 0 0 0 0\n");
		REQUIRE(world.errors.size() == 1); // the second skyoptions line has a word too many
		REQUIRE(world.objects.size() == 1);
		const auto& sky = world.objects[0];
		REQUIRE(sky.kind == GhostObject::Kind::Sky);
		REQUIRE(sky.className() == "Sky");
		REQUIRE(sky.sky.materialList == "lab.dml");
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
	REQUIRE(readServerPacket(h.sent.back()).ghosts.size() == 1);

	h.sent.clear();
	h.conn.receive(clientDataPacket(h.profile, 2, 1, 1, {}, 0), h.now);
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
