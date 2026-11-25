// Modified by the Seihai project, 2026-11-11, 2026-11-12, 2026-11-13, 2026-11-19, 2026-11-25: see CHANGES-SEIHAI.md.
// New file: one Graal 3D (Torque) UDP connection, server side. See Graal3DConnection.h.
// The notify protocol follows Torque's ConnectionProtocol::processRawPacket and
// NetConnection's event code (MIT-licensed Torque3D source), with Graal's 12-bit
// class ids, 14-bit event lengths and 10-bit rate fields. Ghosting follows the
// 5.00r7 Graal3DEngine.dll's ghostReadPacket: an int4 index width sent as is, and a
// class id and int14 length on every ghost update.

#include <graal3d/Graal3DConnection.h>
#include <graal3d/Graal3DTge.h>

#include <algorithm>
#include <cassert>
#include <format>
#include <random>
#include <string>

namespace graal3d
{
	Connection::Connection(const Profile& profile, uint32_t connectSequence, uint64_t nowMs, Callbacks callbacks)
		: m_profile(profile), m_connectSequence(connectSequence), m_cb(std::move(callbacks)), m_lastReceive(nowMs), m_lastSend(nowMs)
	{
	}

	void Connection::log(std::string_view text) const
	{
		if (m_cb.log)
			m_cb.log(text);
	}

	std::string Connection::disconnectPacket(std::string_view reason) const
	{
		return buildDisconnect(m_connectSequence, reason);
	}

	void Connection::receive(std::string_view datagram, uint64_t nowMs)
	{
		BitReader r(datagram);
		const auto header = readPacketHeader(r);
		if (!header || header->connectSequenceBit != (m_connectSequence & 1u))
			return;

		uint32_t seq = header->sequence | (m_lastSeqRecvd & 0xFFFFFE00u);
		if (seq < m_lastSeqRecvd)
			seq += 0x200;
		if (seq > m_lastSeqRecvd + 31)
			return;

		uint32_t highestAck = header->highestAck | (m_highestAckedSeq & 0xFFFFFE00u);
		if (highestAck < m_highestAckedSeq)
			highestAck += 0x200;
		if (highestAck > m_lastSendSeq)
		{
			if (!m_warnedAckAhead)
			{
				m_warnedAckAhead = true;
				log("dropped a packet that acks seq " + std::to_string(highestAck) + ", past the last one sent (" +
					std::to_string(m_lastSendSeq) + ")");
			}
			return;
		}

		m_ackMask <<= (seq - m_lastSeqRecvd);
		if (header->type == PacketType::Data)
			m_ackMask |= 1u;

		for (uint32_t i = m_highestAckedSeq + 1; i <= highestAck; ++i)
		{
			const bool delivered = (header->ackMask & (1u << (highestAck - i))) != 0;
			handleNotify(delivered);
			if (delivered)
				m_lastRecvAckAck = m_lastSeqRecvdAtSend[i & 0x1F];
		}
		if (seq - m_lastRecvAckAck > 32)
			m_lastRecvAckAck = seq - 32;
		m_highestAckedSeq = highestAck;

		m_lastReceive = nowMs;
		if (header->type == PacketType::Ping)
			sendPacket(PacketType::Ack, nowMs);

		if (m_lastSeqRecvd != seq)
		{
			m_lastSeqRecvd = seq;
			if (header->type == PacketType::Data)
			{
				m_ackOwed = true;
				if (!m_established)
					onEstablished();
				readDataPacket(r);
			}
		}
	}

	void Connection::onEstablished()
	{
		m_established = true;
		if (!m_profile.gamePackets())
			return;

		if (m_profile.ghostAlwaysWorld)
		{
			// Sharp3D route: no mission-start commands. The world's ghosts go out as
			// GhostAlwaysObjectEvents (posted by setGhost), and the mission counts as
			// started once the client has acked all of them; then the control
			// object follows. Phase1Sent is still reported, since the owner sends
			// the flags and weapons over the text channel then.
			m_mission = MissionState::Phase1Sent;
			if (m_cb.onMission)
				m_cb.onMission(m_mission);
			finishGhostAlways();
			return;
		}

		// The 5.x and 4.04r2 clients answer these natively and raise
		// onGraal3DLoginStep. 3.153 runs the stock TorqueScript, which takes
		// (sequence, mission name) and sends the sequence back in its ack.
		if (m_profile.tgePackets)
			sendCommand("MissionStartPhase1", {std::to_string(m_missionSeq), "Graal3D"});
		else
			sendCommand("MissionStartPhase1");
		m_mission = MissionState::Phase1Sent;
		if (m_cb.onMission)
			m_cb.onMission(m_mission);
	}

	bool Connection::update(uint64_t nowMs)
	{
		if (nowMs - m_lastReceive > TimeoutMs)
			return false;

		const auto sinceSend = nowMs - m_lastSend;
		if (m_profile.tgePackets && m_established)
			advanceTge();
		if (m_mutation != Mutation::None && !isAcceptMutation(m_mutation) && !m_mutationDone && m_mutationAt == 0 && m_mission == MissionState::Done)
			m_mutationAt = nowMs + 2000;
		if (sendsEvents() && m_established)
		{
			const bool work = !m_sendQueue.empty() || m_ackOwed || ghostWorkPending() || controlPending() || (m_controlDelivered && m_controlMoved) ||
							  mutationDue(nowMs);
			const bool due = work && sinceSend >= SendIntervalMs;
			if (due || sinceSend >= KeepAliveMs)
				sendPacket(PacketType::Data, nowMs);
		}
		else if (m_ackOwed)
		{
			sendPacket(PacketType::Ack, nowMs);
		}
		return true;
	}

	bool Connection::mutationDue(uint64_t nowMs) const
	{
		return m_mutation != Mutation::None && !isAcceptMutation(m_mutation) && !m_mutationDone && m_mutationAt != 0 && nowMs >= m_mutationAt;
	}

	void Connection::writeSized(BitWriter& w, uint32_t classId, const BitWriter& body)
	{
		// Class id, int14 length, body: the length must be the body's exact bit
		// count, never less (see postEvent). Callers keep bodies under 16384 bits.
		const size_t bits = body.bitPosition();
		assert(bits <= MaxEventBits);
		[[maybe_unused]] const size_t start = w.bitPosition();
		w.writeInt(classId, 12);
		w.writeInt(static_cast<uint32_t>(bits), 14);
		w.writeBits(body);
		assert(w.bitPosition() - start == 12 + 14 + bits);
	}

	bool Connection::postEvent(std::string_view className, BitWriter payload)
	{
		const auto id = eventClassId(m_profile, className);
		if (!id)
			return false;
		// An event's length goes out as int14, so a payload of 16384 bits or more
		// would be under-counted, and 5.12 runs what an under-counted event leaves
		// and then crashes (fuzz tier 1, Graal_analysis
		// torque/3d-fuzz-net-tier1-2026-11-25.md). Such an event is not sent.
		if (payload.bitPosition() > MaxEventBits)
		{
			log(std::format("{} of {} bits is longer than an event can say (at most {}), not sent", className, payload.bitPosition(), MaxEventBits));
			return false;
		}
		const int32_t seq = m_nextSendEventSeq++;
		m_events.emplace(seq, OutEvent{*id, std::move(payload)});
		m_sendQueue.push_back(seq);
		return true;
	}

	namespace
	{
		// The first `limit` bytes as hex, for the lab logs.
		std::string hexPreview(std::string_view bytes, size_t limit)
		{
			static constexpr char digits[] = "0123456789abcdef";
			std::string out;
			for (size_t i = 0; i < bytes.size() && i < limit; ++i)
			{
				const auto b = static_cast<uint8_t>(bytes[i]);
				out.push_back(digits[b >> 4]);
				out.push_back(digits[b & 15]);
			}
			if (bytes.size() > limit)
				out += "...";
			return out;
		}
	} // namespace

	bool Connection::postRawEvent(std::string_view className, const std::vector<RawField>& fields)
	{
		if (!eventClassId(m_profile, className))
			return false;
		BitWriter payload;
		payload.setPacketStrings(m_packetStrings);
		writeRawFields(payload, fields);
		return postEvent(className, std::move(payload));
	}

	bool Connection::postTerrainChunk(uint32_t x, uint32_t y, uint32_t stamp, std::string_view packed)
	{
		if (!eventClassId(m_profile, "TerrainChunkEvent") || packed.size() > 1023)
			return false;
		BitWriter payload;
		payload.writeInt(x & 15, 4);
		payload.writeInt(y & 15, 4);
		payload.writeU32(stamp);
		payload.writeInt(static_cast<uint32_t>(packed.size()), 10);
		payload.writeBytes(packed);
		postEvent("TerrainChunkEvent", std::move(payload));
		return true;
	}

	void Connection::sendCommand(std::string_view command, const std::vector<std::string>& args)
	{
		// commandToClient sends the command name as a tagged string: the string
		// first (NetStringEvent), then the command that refers to it. Other
		// arguments go as plain strings.
		const uint32_t tag = m_nextLocalString;
		m_nextLocalString = (m_nextLocalString + 1) % 32;
		postEvent("NetStringEvent", netStringEvent(tag, command, m_packetStrings));
		std::vector<CommandArg> packed{CommandArg{true, tag, {}}};
		for (const auto& a : args)
			packed.push_back(CommandArg{false, 0, a});
		postEvent("RemoteCommandEvent", remoteCommandEvent(packed, m_packetStrings));
	}

	void Connection::startTgeDatablocks()
	{
		// Stock TGE: the datablocks follow the phase 1 ack, then DataBlocksDone
		// with the mission sequence. This server has two, and sends the ones its
		// ghosts use: the CameraData of a TgeCamera, the PlayerData of a TgePlayer.
		if (m_datablocksSent)
			return;
		m_datablocksSent = true;
		bool camera = false;
		const GhostObject* player = nullptr;
		for (const auto& [key, g] : m_ghosts)
		{
			camera = camera || g.object.kind == GhostObject::Kind::TgeCamera;
			if (g.object.kind == GhostObject::Kind::TgePlayer)
				player = &g.object;
		}
		struct Block
		{
			std::string_view className;
			uint32_t id;
			BitWriter data;
		};
		std::vector<Block> blocks;
		if (camera)
			blocks.push_back({"CameraData", tge::CameraDatablockId, tge::cameraData("", m_packetStrings)});
		if (player)
			blocks.push_back({"PlayerData", tge::PlayerDatablockId, tge::playerData("", m_packetStrings, player->tgeShape)});
		for (size_t i = 0; i < blocks.size(); ++i)
		{
			if (const auto classId = datablockClassId(m_profile, blocks[i].className))
				postEvent("SimDataBlockEvent", tge::simDataBlockEvent(blocks[i].id, *classId, static_cast<uint32_t>(i),
																	  static_cast<uint32_t>(blocks.size()), blocks[i].data));
		}
		postEvent("ConnectionMessageEvent", tge::connectionMessageEvent(m_missionSeq, tge::message::DataBlocksDone, 0));
		log("datablocks sent (" + std::to_string(blocks.size()) + "), then DataBlocksDone");
	}

	void Connection::advanceTge()
	{
		// Phase 3 once phase 2 is acked and every ghost is in: GhostAlwaysDone
		// first (the client answers ReadyForNormalGhosts), then the command.
		if (m_mission != MissionState::Phase2Sent || !m_phase2Acked)
			return;
		for (const auto& [key, g] : m_ghosts)
		{
			if (!g.created && !g.kill)
				return;
		}
		postEvent("ConnectionMessageEvent", tge::connectionMessageEvent(m_missionSeq, tge::message::GhostAlwaysDone, 0));
		sendCommand("MissionStartPhase3", {std::to_string(m_missionSeq), "Graal3D"});
		m_mission = MissionState::Phase3Sent;
		if (m_cb.onMission)
			m_cb.onMission(m_mission);
	}

	void Connection::sendText(const TextMessage& message)
	{
		if (!sendsEvents())
			return;
		const uint32_t id = m_nextBigDataId;
		m_nextBigDataId = (m_nextBigDataId + 1) & ((1u << BigDataIdBits) - 1);
		auto events = bigDataEvents(id, message, m_packetStrings);
		for (size_t i = 0; i < events.size(); ++i)
			postEvent(i == 0 ? "BigDataStartEvent" : "BigDataChunkEvent", std::move(events[i]));
	}

	void Connection::sendPacket(PacketType type, uint64_t nowMs)
	{
		if (type == PacketType::Data && m_lastSendSeq - m_highestAckedSeq >= 30)
		{
			type = PacketType::Ack; // window full: acks only
			if (!m_warnedWindow)
			{
				m_warnedWindow = true;
				log("send window full: the client has acked nothing past seq " + std::to_string(m_highestAckedSeq));
			}
		}

		BitWriter w;
		PacketHeader h;
		h.connectSequenceBit = m_connectSequence & 1u;
		if (type == PacketType::Data)
			++m_lastSendSeq;
		h.sequence = m_lastSendSeq;
		h.highestAck = m_lastSeqRecvd;
		h.type = type;
		h.ackByteCount = std::min<uint32_t>(4, (m_lastSeqRecvd - m_lastRecvAckAck + 7) >> 3);
		h.ackMask = m_ackMask;
		writePacketHeader(w, h);
		m_ackOwed = false;
		m_lastSend = nowMs;

		if (type != PacketType::Data)
		{
			if (m_cb.send)
				m_cb.send(w.toString());
			return;
		}

		m_lastSeqRecvdAtSend[m_lastSendSeq & 0x1F] = m_lastSeqRecvd;

		// Rate fields: no change.
		w.writeFlag(false);
		w.writeFlag(false);

		Notify note{m_lastSendSeq, {}, {}, false};
		const bool mutate = mutationDue(nowMs);
		std::mt19937 rng(m_mutationSeed);
		m_mutationNote.clear();
		const auto bitsFor = [](uint32_t index) {
			uint32_t bits = 1;
			while ((index >> bits) != 0)
				++bits;
			return bits;
		};
		const auto freeIndex = [this]() {
			uint32_t index = 0;
			while (m_usedIndices.contains(index))
				++index;
			return index;
		};
		// The ghost a control mutation names: one of another class, or none at all.
		std::optional<uint32_t> badControl;
		if (mutate && (m_mutation == Mutation::ControlWrongClass || m_mutation == Mutation::ControlDeleted))
		{
			if (m_mutation == Mutation::ControlDeleted)
			{
				badControl = freeIndex() + rng() % 8;
				m_mutationNote = "control object: ghost index " + std::to_string(*badControl) + ", which holds no ghost";
			}
			else
			{
				const std::string_view controlClass =
					m_controlKey && m_ghosts.contains(*m_controlKey) ? m_ghosts.at(*m_controlKey).object.className() : std::string_view{};
				std::vector<const Ghost*> others;
				for (const auto& [key, g] : m_ghosts)
				{
					if (g.created && !g.kill && g.object.className() != controlClass)
						others.push_back(&g);
				}
				if (!others.empty())
				{
					const auto* g = others[rng() % others.size()];
					badControl = g->index;
					m_mutationNote = "control object: ghost " + std::to_string(g->index) + ", a " + std::string(g->object.className());
				}
			}
		}
		if (m_profile.tgePackets && badControl)
		{
			tge::writeServerGame(w, m_lastMoveAck, tge::ServerControl{static_cast<uint32_t>(nowMs), *badControl, {}, std::nullopt});
		}
		else if (!m_profile.tgePackets && badControl)
		{
			w.writeFlag(true);
			w.writeFlag(true);
			w.writeU32(static_cast<uint32_t>(nowMs));
			w.writeInt(*badControl, 12);
			writeGameObjectUpdate(w, GameObjectState{}, 0);
			writeCameraSection(w);
			w.writeFlag(false);
		}
		else if (m_profile.tgePackets)
		{
			// GameConnection::writePacket, protocol 19: move ack, then the control
			// object with its packet data, no camera, no field of view.
			// A Player that moved since its packet data last went out sends it again.
			std::optional<tge::ServerControl> control;
			const bool first = controlPending();
			const bool moved = !first && m_controlDelivered && m_controlMoved && m_controlKey && m_ghosts.contains(*m_controlKey);
			if (first || moved)
			{
				const auto& g = m_ghosts.at(*m_controlKey);
				control = tge::ServerControl{static_cast<uint32_t>(nowMs), g.index, g.object.tgeCamera, std::nullopt};
				if (g.object.kind == GhostObject::Kind::TgePlayer)
					control->player = g.object.tgePlayer;
				m_controlMoved = false;
				if (first)
				{
					note.control = true;
					m_controlInFlight = true;
					log("control object (" + std::string(g.object.className()) + " ghost " + std::to_string(g.index) + ") sent in seq " +
						std::to_string(note.sequence));
				}
				else
					note.controlUpdate = true;
			}
			tge::writeServerGame(w, m_lastMoveAck, control);
		}
		else
		{
			// GameConnection::writePacket, GameObject era: the control object, no
			// camera, no field of view.
			writeControlSection(w, note, nowMs);
			writeCameraSection(w);
			w.writeFlag(false);
		}

		// Events: no unguaranteed ones (but a mutation's), then the guaranteed-ordered queue.
		if (mutate && (m_mutation == Mutation::EventUnknownClass || m_mutation == Mutation::EventBadLength))
		{
			const auto classes = static_cast<uint32_t>(m_profile.eventClasses.size());
			const uint32_t bits = rng() % 64;
			uint32_t classId = 0;
			uint32_t length = bits;
			if (m_mutation == Mutation::EventUnknownClass)
			{
				classId = classes + rng() % (4096 - classes);
				m_mutationNote = "an unguaranteed event of class id " + std::to_string(classId) + " (the table has " + std::to_string(classes) +
								 "), " + std::to_string(bits) + " bits";
			}
			else
			{
				classId = classes ? rng() % classes : 0;
				const std::array<int64_t, 4> deltas{1, 8, -1, 16383};
				const auto delta = deltas[rng() % deltas.size()];
				length = static_cast<uint32_t>(std::clamp<int64_t>(static_cast<int64_t>(bits) + delta, 0, 16383));
				m_mutationNote = "an unguaranteed " + std::string(eventClassName(m_profile, classId)) + " claiming " + std::to_string(length) +
								 " bits with " + std::to_string(bits);
			}
			w.writeFlag(true);
			w.writeInt(classId, 12);
			w.writeInt(length, 14);
			for (uint32_t i = 0; i < bits; ++i)
				w.writeFlag((rng() & 1u) != 0);
		}
		w.writeFlag(false);
		int32_t prevSeq = -2;
		const int32_t lastAcked = m_events.empty() ? m_nextSendEventSeq - 1 : m_events.begin()->first - 1;
		while (!m_sendQueue.empty())
		{
			const int32_t seq = m_sendQueue.front();
			if (seq > lastAcked + 126)
				break;
			const auto& ev = m_events.at(seq);
			const size_t eventBits = 1 + 1 + 7 + 12 + 14 + ev.payload.bitPosition();
			if (!note.events.empty() && (w.bitPosition() + eventBits + 16) / 8 > MaxPacketBytes)
				break;

			w.writeFlag(true);
			if (!w.writeFlag(seq == prevSeq + 1))
				w.writeInt(static_cast<uint32_t>(seq) & 0x7Fu, 7);
			prevSeq = seq;
			writeSized(w, ev.classId, ev.payload);
			note.events.push_back(seq);
			m_sendQueue.pop_front();
		}
		w.writeFlag(false);

		const bool ghostMutation =
			mutate && (m_mutation == Mutation::GhostUncreated || m_mutation == Mutation::GhostDoubleDelete || m_mutation == Mutation::GhostIndexWidth);
		if (ghostMutation)
		{
			uint32_t maxIndex = 0;
			const Ghost* existing = nullptr;
			for (const auto& [key, g] : m_ghosts)
			{
				maxIndex = std::max(maxIndex, g.index);
				if (g.created && !g.kill && (!m_controlKey || key != *m_controlKey) && existing == nullptr)
					existing = &g;
			}
			const uint32_t spare = freeIndex() + rng() % 8;
			if (m_mutation == Mutation::GhostIndexWidth && existing != nullptr)
			{
				const uint32_t real = bitsFor(maxIndex);
				uint32_t wrong = (real + 1 + rng() % 14) % 16;
				if (wrong == real)
					wrong = (wrong + 1) % 16;
				BitWriter payload;
				payload.setPacketStrings(m_packetStrings);
				existing->object.writeUpdate(payload, existing->object.fullMask(), false);
				w.writeFlag(true);
				w.writeInt(wrong, 4);
				w.writeFlag(true);
				w.writeInt(existing->index, static_cast<int>(wrong));
				w.writeFlag(false);
				w.writeInt(existing->classId, 12);
				w.writeInt(static_cast<uint32_t>(payload.bitPosition()), 14);
				w.writeBits(payload);
				w.writeFlag(false);
				m_mutationNote = "ghost section with index width " + std::to_string(wrong) + " (needs " + std::to_string(real) + "): a full update of ghost " +
								 std::to_string(existing->index) + ", a " + std::string(existing->object.className());
			}
			else
			{
				const uint32_t idBits = bitsFor(std::max(maxIndex, spare));
				w.writeFlag(true);
				w.writeInt(idBits, 4);
				if (m_mutation == Mutation::GhostDoubleDelete)
				{
					for (int i = 0; i < 2; ++i)
					{
						w.writeFlag(true);
						w.writeInt(spare, static_cast<int>(idBits));
						w.writeFlag(true);
					}
					m_mutationNote = "two deletes of ghost index " + std::to_string(spare) + ", which holds no ghost";
				}
				else
				{
					const uint32_t classId = existing != nullptr ? existing->classId : 0;
					const uint32_t bits = rng() % 64;
					w.writeFlag(true);
					w.writeInt(spare, static_cast<int>(idBits));
					w.writeFlag(false);
					w.writeInt(classId, 12);
					w.writeInt(bits, 14);
					for (uint32_t i = 0; i < bits; ++i)
						w.writeFlag((rng() & 1u) != 0);
					m_mutationNote = "an update of ghost index " + std::to_string(spare) + ", never created, class id " + std::to_string(classId) + ", " +
									 std::to_string(bits) + " random bits";
				}
				w.writeFlag(false);
			}
		}
		else
			writeGhostSection(w, note);

		m_notifies.push_back(std::move(note));
		const auto packet = w.toString();
		if (mutate)
		{
			m_mutationDone = true;
			const auto line = "mutation " + std::string(mutationName(m_mutation)) + ":" + std::to_string(m_mutationSeed) + " in seq " +
							  std::to_string(m_lastSendSeq) + ": " + (m_mutationNote.empty() ? "nothing to mutate (no fitting ghost)" : m_mutationNote) +
							  "; packet " + hexPreview(packet, packet.size());
			log(line);
			if (m_cb.trace)
				m_cb.trace(line);
		}
		if (m_cb.send)
			m_cb.send(packet);
	}

	void Connection::handleNotify(bool delivered)
	{
		if (m_notifies.empty())
			return;
		Notify note = std::move(m_notifies.front());
		m_notifies.pop_front();
		ghostNotify(note, delivered);

		if (delivered)
		{
			for (auto seq : note.events)
			{
				m_events.erase(seq);
				if (auto ga = m_ghostAlwaysEvents.find(seq); ga != m_ghostAlwaysEvents.end())
				{
					if (auto it = m_ghosts.find(ga->second); it != m_ghosts.end())
					{
						it->second.createInFlight = false;
						it->second.created = true;
					}
					m_ghostAlwaysEvents.erase(ga);
				}
			}
			finishGhostAlways();
			m_lastAckedEventSeq = m_events.empty() ? m_nextSendEventSeq - 1 : m_events.begin()->first - 1;
			return;
		}

		// Lost: put its events back in sequence order.
		for (auto seq : note.events)
		{
			auto pos = std::lower_bound(m_sendQueue.begin(), m_sendQueue.end(), seq);
			if (pos == m_sendQueue.end() || *pos != seq)
				m_sendQueue.insert(pos, seq);
		}
	}


	// --- Ghosting ---------------------------------------------------------------

	std::optional<uint32_t> Connection::allocateGhostIndex()
	{
		uint32_t index = 0;
		for (auto used : m_usedIndices)
		{
			if (used != index)
				break;
			++index;
		}
		if (index >= MaxGhosts)
			return std::nullopt;
		m_usedIndices.insert(index);
		return index;
	}

	void Connection::resendGhost(ObjectKey key)
	{
		auto it = m_ghosts.find(key);
		if (it == m_ghosts.end() || it->second.kill)
			return;
		// Not created yet: its create carries the whole update anyway.
		if (it->second.created)
			it->second.pending |= it->second.object.fullMask();
	}

	void Connection::setGhost(ObjectKey key, const GhostObject& object)
	{
		// Each era ghosts only its own kinds: protocol 19 the TGE Camera and
		// Player, the GameObject era everything else; a Sky goes to both.
		if (!m_profile.gamePackets() || !object.sentTo(m_profile.tgePackets))
			return;

		auto it = m_ghosts.find(key);
		if (it == m_ghosts.end())
		{
			const auto classId = objectClassId(m_profile, object.className());
			const auto index = classId ? allocateGhostIndex() : std::nullopt;
			if (!index)
			{
				log("cannot ghost " + std::string(object.className()) + ": no class id or ghost slot");
				return;
			}
			Ghost g;
			g.index = *index;
			g.classId = *classId;
			g.object = object;
			auto& placed = m_ghosts.emplace(key, std::move(g)).first->second;
			if (m_profile.ghostAlwaysWorld)
				postGhostAlways(key, placed);
			return;
		}

		auto& g = it->second;
		if (g.kill)
		{
			// Brought back before its delete was acked: send it whole again.
			g.kill = false;
			g.pending |= object.fullMask();
		}
		g.pending |= object.changesFrom(g.object);
		g.object = object;
	}

	void Connection::removeGhost(ObjectKey key)
	{
		auto it = m_ghosts.find(key);
		if (it == m_ghosts.end())
			return;
		if (m_controlKey == key)
		{
			m_controlKey.reset();
			m_controlInFlight = false;
			m_controlDelivered = false;
		}
		if (m_cameraKey == key)
			m_cameraKey.reset(); // stop naming it before its delete goes out

		auto& g = it->second;
		if (!g.created && !g.createInFlight && !g.killInFlight)
		{
			// The client never heard of it.
			m_usedIndices.erase(g.index);
			m_ghosts.erase(it);
			return;
		}
		g.kill = true;
		g.pending = 0;
	}

	void Connection::setControlObject(ObjectKey key)
	{
		if (m_controlKey == key)
			return;
		m_controlKey = key;
		m_controlInFlight = false;
		m_controlDelivered = false;
	}

	void Connection::setCameraObject(ObjectKey key)
	{
		m_cameraKey = key;
	}

	void Connection::writeCameraSection(BitWriter& w)
	{
		// The client looks the index up as a Camera and, if that fails, drops the
		// rest of the packet. So name it only once its create has been acked.
		m_cameraNamed = false;
		if (m_cameraKey && ghostsMayGo())
		{
			const auto it = m_ghosts.find(*m_cameraKey);
			m_cameraNamed = it != m_ghosts.end() && it->second.created && !it->second.kill &&
							it->second.object.kind == GhostObject::Kind::Camera;
			if (m_cameraNamed)
			{
				w.writeFlag(true);
				w.writeInt(it->second.index, 12);
				w.writeFlag(false); // no camera packet data: its ghost carries the state
				return;
			}
		}
		w.writeFlag(false);
	}

	std::optional<uint32_t> Connection::ghostIndex(ObjectKey key) const
	{
		const auto it = m_ghosts.find(key);
		if (it == m_ghosts.end())
			return std::nullopt;
		return it->second.index;
	}

	bool Connection::ghostDelivered(ObjectKey key) const
	{
		const auto it = m_ghosts.find(key);
		return it != m_ghosts.end() && it->second.created;
	}

	bool Connection::controlPending() const
	{
		// The 5.x client sets login step 5 when it first gets a control object, and
		// MissionStartPhase2 sets the step back to 3. Step 6 only follows step 5, so
		// a control object that comes before the mission start is over leaves the
		// client stuck at step 4 (seen with a 5.12 client).
		if (!m_controlKey || m_controlDelivered || m_controlInFlight || m_mission != MissionState::Done)
			return false;
		const auto it = m_ghosts.find(*m_controlKey);
		return it != m_ghosts.end() && it->second.created && !it->second.kill;
	}

	bool Connection::ghostsMayGo() const
	{
		// Protocol 19: not before the client has loaded the datablocks they name
		// (an unknown datablock id drops the connection). Protocols 21 and 24: not
		// before MissionStartPhase1Ack. A 5.12 on a return visit dropped the ghosts
		// it got before that ack, then stalled in phase 1 on "Received unknown
		// camera object" when the server named its Camera (a world of 0 or 1
		// objects, lab 2026-11-25; with 6 objects the ghosts went after the ack and
		// it worked). The Sharp3D route ghosts through events from the start.
		return m_ghostingAllowed || m_profile.ghostAlwaysWorld;
	}

	bool Connection::ghostWorkPending() const
	{
		if (!ghostsMayGo())
			return false;
		for (const auto& [key, g] : m_ghosts)
		{
			if (g.createInFlight || g.killInFlight)
				continue;
			if (g.kill || !g.created || g.pending != 0)
				return true;
		}
		return false;
	}

	void Connection::writeControlSection(BitWriter& w, Notify& note, uint64_t nowMs)
	{
		// The client reads flag A and flag B; only both set carry a control object,
		// and either one clear leaves the control object as it is. The packet data
		// may only name a ghost the client already has, so the control object goes
		// out once its create has been acked and the mission start is done.
		w.writeFlag(true);
		// The Sharp3D route names its Camera control object in every packet once
		// the world is in, as the server the 5.12 client joined did.
		const bool again = m_profile.ghostAlwaysWorld && m_controlDelivered && m_controlKey && m_ghosts.contains(*m_controlKey) &&
						   m_ghosts.at(*m_controlKey).created && !m_ghosts.at(*m_controlKey).kill;
		if (!w.writeFlag(controlPending() || again))
			return;

		const auto& g = m_ghosts.at(*m_controlKey);
		if (again)
		{
			w.writeU32(static_cast<uint32_t>(nowMs));
			w.writeInt(g.index, 12);
			g.object.writeUpdate(w, 0);
			return;
		}
		w.writeU32(static_cast<uint32_t>(nowMs)); // the client's synchronised clock (timevar3)
		w.writeInt(g.index, 12);
		// The packet data is a full GameObject update with no length prefix. The
		// ghost already carries the object's state, so no section is set.
		// A Camera's carries its two Camera flags too (both clear).
		g.object.writeUpdate(w, 0);
		note.control = true;
		m_controlInFlight = true;
		log("control object (ghost " + std::to_string(g.index) + ") sent in seq " + std::to_string(note.sequence));
	}

	void Connection::writeGhostSection(BitWriter& w, Notify& note)
	{
		struct Candidate
		{
			ObjectKey key;
			Ghost* ghost;
			int order; // 0 delete, 1 create, 2 update
		};

		if (!ghostsMayGo())
		{
			w.writeFlag(false);
			return;
		}

		std::vector<Candidate> todo;
		uint32_t maxIndex = 0;
		for (auto& [key, g] : m_ghosts)
		{
			if (g.createInFlight || g.killInFlight)
				continue;
			int order = -1;
			if (g.kill)
				order = 0;
			else if (!g.created)
				order = 1;
			else if (g.pending != 0)
				order = 2;
			if (order < 0)
				continue;
			todo.push_back({key, &g, order});
			maxIndex = std::max(maxIndex, g.index);
		}

		if (!w.writeFlag(!todo.empty()))
			return;

		std::stable_sort(todo.begin(), todo.end(), [](const Candidate& a, const Candidate& b) { return a.order < b.order; });

		uint32_t idBits = 1;
		while ((maxIndex >> idBits) != 0)
			++idBits;
		w.writeInt(idBits, 4);

		const size_t limitBits = MaxPacketBytes * 8;
		for (const auto& c : todo)
		{
			auto& g = *c.ghost;
			const bool create = c.order == 1;
			const uint32_t mask = create ? g.object.fullMask() : g.pending;

			BitWriter payload;
			payload.setPacketStrings(m_packetStrings);
			if (!g.kill)
				g.object.writeUpdate(payload, mask, m_controlKey == c.key);
			if (payload.bitPosition() >= (1u << 14))
			{
				log("ghost update too large, dropped: " + g.object.object.name);
				g.pending = 0;
				continue;
			}

			const size_t entryBits = 2 + idBits + (g.kill ? 0 : 12 + 14 + payload.bitPosition());
			if (!note.ghosts.empty() && w.bitPosition() + entryBits + 2 > limitBits)
				break;

			w.writeFlag(true);
			w.writeInt(g.index, static_cast<int>(idBits));
			if (!w.writeFlag(g.kill))
			{
				writeSized(w, g.classId, payload);
			}

			note.ghosts.push_back({c.key, mask, create, g.kill});
			if (m_cb.trace)
				m_cb.trace(std::format("ghost-out {} index={} {} {} mask=0x{:x} bits={}", g.kill ? "kill" : create ? "create" : "update", g.index,
									   g.object.className(), g.object.object.name, mask, payload.bitPosition()));
			if (g.kill)
				g.killInFlight = true;
			else if (create)
				g.createInFlight = true;
			g.pending = 0;
		}
		w.writeFlag(false);
	}

	void Connection::postGhostAlways(ObjectKey key, Ghost& g)
	{
		// GhostAlwaysObjectEvent: ghost index (12 bits), a flag (an object
		// follows), its class (12), the update's length (14), the update.
		BitWriter update;
		update.setPacketStrings(m_packetStrings);
		g.object.writeUpdate(update, g.object.fullMask(), m_controlKey == key);
		if (update.bitPosition() >= (1u << 14))
		{
			log("ghost-always object too large, not sent: " + std::string(g.object.className()));
			return;
		}
		BitWriter payload;
		payload.setPacketStrings(m_packetStrings);
		payload.writeInt(g.index, 12);
		payload.writeFlag(true);
		writeSized(payload, g.classId, update);
		const int32_t seq = m_nextSendEventSeq;
		postEvent("GhostAlwaysObjectEvent", std::move(payload));
		if (m_nextSendEventSeq == seq)
			return; // no such event class
		m_ghostAlwaysEvents[seq] = key;
		g.createInFlight = true; // the ghost section leaves it alone; the event's ack creates it
		g.pending = 0;
	}

	void Connection::finishGhostAlways()
	{
		if (!m_profile.ghostAlwaysWorld || !m_established || m_mission == MissionState::Done || !m_ghostAlwaysEvents.empty())
			return;
		m_mission = MissionState::Done;
		log("every ghost-always object is on the client");
		if (m_cb.onMission)
			m_cb.onMission(m_mission);
	}

	void Connection::ghostNotify(const Notify& note, bool delivered)
	{
		if (note.controlUpdate && !delivered)
			m_controlMoved = true; // the Player's latest state goes out again
		if (note.control)
		{
			m_controlInFlight = false;
			log(std::string("control object ") + (delivered ? "delivered" : "lost, sending it again") + " (seq " +
				std::to_string(note.sequence) + ")");
			if (delivered)
			{
				m_controlDelivered = true;
				if (m_controlKey)
				{
					if (auto it = m_ghosts.find(*m_controlKey); it != m_ghosts.end())
						m_clientObject = it->second.object.object;
				}
			}
		}

		for (const auto& sent : note.ghosts)
		{
			auto it = m_ghosts.find(sent.key);
			if (it == m_ghosts.end())
				continue;
			auto& g = it->second;

			if (sent.kill)
			{
				g.killInFlight = false;
				if (!delivered)
					continue; // still marked kill unless brought back: sent again
				if (g.kill)
				{
					m_usedIndices.erase(g.index);
					m_ghosts.erase(it);
				}
				else
				{
					// Brought back while the delete was on its way: create it again.
					g.created = false;
					g.pending = 0;
				}
				continue;
			}

			if (sent.create)
			{
				g.createInFlight = false;
				log("ghost " + std::to_string(g.index) + " (" + std::string(g.object.className()) + " " +
					g.object.object.name + ") " + (delivered ? "created on the client" : "create lost, sending it again") +
					" (seq " + std::to_string(note.sequence) + ")");
				if (delivered)
					g.created = true;
				continue; // a lost create is sent whole again
			}

			if (!delivered)
				g.pending |= sent.mask;
		}
	}

	// --- Reading the client's packets -------------------------------------------

	bool Connection::readClientGameSection(BitReader& r)
	{
		// 5.x GameConnection::writePacket on the client: a flag, its control
		// object's position, that object's ghost index, the object's own update
		// when this connection controls it, the field of view.
		ClientState state;
		r.readFlag();
		for (auto& v : state.position)
			v = r.readF32();
		if (r.readFlag())
			r.readInt(12);
		if (r.readFlag())
		{
			state.controlling = true;
			state.changed = readGameObjectUpdate(r, m_clientObject, m_clientMoveType);
			state.object = m_clientObject;
			const auto control = m_controlKey ? m_ghosts.find(*m_controlKey) : m_ghosts.end();
			const bool cameraControl = control != m_ghosts.end() && control->second.object.kind == GhostObject::Kind::Camera;
			if (m_profile.ghostAlwaysWorld || cameraControl)
			{
				// A Camera as the control object (always on the Sharp3D route): its
				// update has two more sections, mode and followed object. A live 5.12
				// writes both flags (2 bits, nothing else parses with events in the
				// packet; T3D-023, lab 2026-11-25). Without them this route lost every
				// client event while a Camera was in control.
				if (r.readFlag())
					r.readInt(1);
				if (r.readFlag() && r.readFlag())
					r.readInt(12);
			}
		}
		if (r.readFlag())
			r.readInt(8);
		if (r.overrun())
			return false;

		traceState(state);
		if (m_controlDelivered && m_cb.onClientState)
			m_cb.onClientState(state);
		return true;
	}

	bool Connection::readTgeClientGameSection(BitReader& r)
	{
		const auto game = tge::readClientGame(r);
		if (!game)
		{
			log("could not read the client's game section (protocol 19)");
			return false;
		}
		if (!game->sendsState && !game->moves.empty())
		{
			// Ack every move the client has sent. A Player control object takes
			// each new move once (a move can come again in later packets until the
			// client sees it acked); a Camera is not simulated here, the client
			// predicts it.
			const uint32_t next = game->firstMove + static_cast<uint32_t>(game->moves.size());
			if (next > m_lastMoveAck)
			{
				Ghost* player = nullptr;
				if (m_controlDelivered && m_controlKey)
				{
					if (auto it = m_ghosts.find(*m_controlKey); it != m_ghosts.end() && it->second.object.kind == GhostObject::Kind::TgePlayer)
						player = &it->second;
				}
				for (uint32_t i = std::max(m_lastMoveAck, game->firstMove); i < next && player; ++i)
					tge::applyMove(player->object.tgePlayer, game->moves[i - game->firstMove]);
				if (player)
					m_controlMoved = true;
				m_movesReceived += next - std::max(m_lastMoveAck, game->firstMove);
				m_lastMoveAck = next;
				m_ackOwed = true;
			}
		}
		return true;
	}

	std::optional<tge::PlayerState> Connection::controlPlayer() const
	{
		if (!m_controlKey)
			return std::nullopt;
		const auto it = m_ghosts.find(*m_controlKey);
		if (it == m_ghosts.end() || it->second.object.kind != GhostObject::Kind::TgePlayer)
			return std::nullopt;
		return it->second.object.tgePlayer;
	}

	void Connection::readDataPacket(BitReader& r)
	{
		// The client's string buffer lives for the length of one packet.
		std::string stringBuffer;
		if (m_packetStrings)
			r.setStringBuffer(&stringBuffer);

		// Rate fields (10 bits each in the Graal builds).
		if (r.readFlag())
		{
			r.readInt(10);
			r.readInt(10);
		}
		if (r.readFlag())
		{
			r.readInt(10);
			r.readInt(10);
		}

		if (m_profile.tgePackets)
		{
			if (!readTgeClientGameSection(r))
				return;
		}
		else if (!m_profile.gameObjectPackets || !readClientGameSection(r))
			return;

		bool unguaranteed = true;
		int32_t prevSeq = -2;
		while (true)
		{
			bool bit = r.readFlag();
			if (unguaranteed && !bit)
			{
				unguaranteed = false;
				bit = r.readFlag();
			}
			if (r.overrun() || (!unguaranteed && !bit))
				break;

			int32_t seq = -1;
			if (!unguaranteed)
			{
				if (r.readFlag())
					seq = (prevSeq + 1) & 0x7F;
				else
					seq = static_cast<int32_t>(r.readInt(7));
				prevSeq = seq;
			}

			const auto classId = r.readInt(12);
			const auto bits = r.readInt(14);
			if (r.overrun() || bits > r.bitsLeft())
				break;
			const size_t start = r.bitPosition();

			BitWriter copy;
			for (uint32_t i = 0; i < bits; ++i)
				copy.writeFlag(r.readFlag());
			r.setBitPosition(start + bits);

			auto event = decodeEvent(classId, copy.toString(), bits, stringBuffer);
			if (event)
				traceEvent(*event, !unguaranteed);
			else if (m_cb.trace)
				m_cb.trace("event " + std::string(eventClassName(m_profile, classId)) + " (class " + std::to_string(classId) + ") not decoded, bits=" + std::to_string(bits));
			if (unguaranteed)
			{
				if (event)
					processEvent(*event);
				continue;
			}

			seq |= (m_nextRecvEventSeq & ~0x7F);
			if (seq < m_nextRecvEventSeq)
				seq += 128;
			// An event that could not be decoded still takes its place in the order.
			m_waiting.emplace(seq, event.value_or(InEvent{}));
		}

		while (!m_waiting.empty() && m_waiting.begin()->first == m_nextRecvEventSeq)
		{
			auto ev = std::move(m_waiting.begin()->second);
			m_waiting.erase(m_waiting.begin());
			++m_nextRecvEventSeq;
			processEvent(ev);
		}
	}

	std::optional<Connection::InEvent> Connection::decodeEvent(uint32_t classId, const std::string& payload, size_t bits, std::string& stringBuffer)
	{
		InEvent ev;
		ev.name = eventClassName(m_profile, classId);

		const auto decode = [&](bool packetStrings, std::string& buffer) {
			BitReader r(payload);
			if (packetStrings)
				r.setStringBuffer(&buffer);
			InEvent out;
			out.name = ev.name;
			if (out.name == "NetStringEvent")
			{
				out.id = r.readInt(StringTableBits);
				out.text = r.readString();
			}
			else if (out.name == "RemoteCommandEvent")
			{
				const auto argc = r.readInt(CommandArgsBits);
				for (uint32_t i = 0; i < argc && !r.overrun(); ++i)
					out.args.push_back(unpackCommandArg(r));
			}
			else if (out.name == "BigDataStartEvent")
			{
				out.id = r.readInt(BigDataIdBits);
				out.header.weapon = r.readString();
				out.header.type = r.readString();
				out.header.option = r.readString();
				out.total = r.readInt(BigDataLengthBits);
				out.mode = static_cast<BigDataMode>(r.readInt(BigDataModeBits));
			}
			else if (out.name == "BigDataChunkEvent")
			{
				out.id = r.readInt(BigDataIdBits);
				out.text = r.readBytes(r.readInt(8));
			}
			else if (out.name == "ConnectionMessageEvent")
			{
				out.sequence = r.readU32();
				out.id = r.readInt(3);
				out.count = r.readInt(11);
			}
			else
			{
				// Not decoded: kept whole for the log (TerrainRequestUpdatesEvent and
				// the other events whose layouts are not read yet).
				out.payload = payload;
				out.payloadBits = bits;
				return std::pair{std::move(out), true};
			}
			return std::pair{std::move(out), !r.overrun() && r.bitPosition() == bits};
		};

		std::string tried = stringBuffer;
		auto [event, fits] = decode(m_packetStrings, tried);
		if (!fits)
		{
			// Try the other string layout, so a lab run shows which one the client uses.
			std::string other = stringBuffer;
			auto [alt, altFits] = decode(!m_packetStrings, other);
			if (!m_warnedStrings)
			{
				log(std::string("event ") + std::string(ev.name) + " does not fill its length with packet strings " +
					(m_packetStrings ? "on" : "off") + (altFits ? "; it does with them " : "; nor with them ") +
					(m_packetStrings ? "off (graal3dstringprefix = false)" : "on (graal3dstringprefix = true)"));
				m_warnedStrings = true;
			}
			if (!altFits)
				return std::nullopt;
			event = std::move(alt);
			tried = std::move(other);
		}
		stringBuffer = std::move(tried);
		return std::move(event);
	}

	void Connection::processEvent(const InEvent& ev)
	{
		if (ev.name == "NetStringEvent")
		{
			m_remoteStrings[ev.id % 32] = ev.text;
			return;
		}

		if (ev.name == "RemoteCommandEvent")
		{
			std::vector<std::string> args;
			for (const auto& arg : ev.args)
				args.push_back(arg.isTag ? m_remoteStrings[arg.tag % 32] : arg.text);
			if (args.empty())
				return;

			const auto& command = args[0];
			if (m_profile.tgePackets)
			{
				// 3.153 echoes the sequence as the second argument; 4.04r2 sends none.
				if (args.size() > 1 && args[1] != std::to_string(m_missionSeq))
				{
					log("remote command " + command + " for mission sequence " + args[1] + " ignored");
					return;
				}
				if (command == "MissionStartPhase1Ack" && m_mission == MissionState::Phase1Sent)
					startTgeDatablocks();
				else if (command == "MissionStartPhase2Ack" && m_mission == MissionState::Phase2Sent)
				{
					m_phase2Acked = true;
					advanceTge();
				}
				else if (command == "MissionStartPhase3Ack" && m_mission == MissionState::Phase3Sent)
				{
					m_mission = MissionState::Done;
					if (m_cb.onMission)
						m_cb.onMission(m_mission);
				}
				else
					log("remote command ignored: " + command);
				return;
			}

			if (command == "MissionStartPhase1Ack" && m_mission == MissionState::Phase1Sent)
			{
				m_ghostingAllowed = true; // see ghostsMayGo
				sendCommand("MissionStartPhase2");
				m_mission = MissionState::Phase2Sent;
			}
			else if (command == "MissionStartPhase2Ack" && m_mission == MissionState::Phase2Sent)
			{
				if (m_profile.missionPhases > 2)
				{
					sendCommand("MissionStartPhase3");
					m_mission = MissionState::Phase3Sent;
				}
				else
					m_mission = MissionState::Done;
			}
			else if (command == "MissionStartPhase3Ack" && m_mission == MissionState::Phase3Sent)
				m_mission = MissionState::Done;
			else
			{
				log("remote command ignored: " + command);
				return;
			}

			if (m_cb.onMission)
				m_cb.onMission(m_mission);
			return;
		}

		if (ev.name == "BigDataStartEvent")
		{
			m_bigData.start(ev.id, ev.header, ev.total, ev.mode);
			return;
		}

		if (ev.name == "BigDataChunkEvent")
		{
			if (auto message = m_bigData.chunk(ev.id, ev.text); message && m_cb.onText)
				m_cb.onText(*message);
			return;
		}

		if (ev.name == "ConnectionMessageEvent")
		{
			if (!m_profile.tgePackets)
				return; // ReadyForNormalGhosts and the like: this server ghosts from the start

			if (ev.id == tge::message::DataBlocksDownloadDone && ev.sequence == m_missionSeq && m_datablocksSent &&
				m_mission == MissionState::Phase1Sent)
			{
				// The datablocks are loaded: phase 2, and the ghosts may go out.
				log("datablocks loaded on the client");
				sendCommand("MissionStartPhase2", {std::to_string(m_missionSeq), "Graal3D"});
				postEvent("ConnectionMessageEvent", tge::connectionMessageEvent(m_missionSeq, tge::message::GhostAlwaysStarting,
																			   static_cast<uint32_t>(m_ghosts.size())));
				m_ghostingAllowed = true;
				m_mission = MissionState::Phase2Sent;
				if (m_cb.onMission)
					m_cb.onMission(m_mission);
			}
			else if (ev.id == tge::message::ReadyForNormalGhosts)
				log("client ready for normal ghosts");
			else
				log("connection message " + std::to_string(ev.id) + " (sequence " + std::to_string(ev.sequence) + ") ignored");
			return;
		}

		if (ev.name == "TerrainRequestUpdatesEvent")
		{
			// Graal_analysis torque/terrain-512.md: 256 x u32, one stamp per
			// 16 x 16 terrain chunk, what the client already holds.
			BitReader r(ev.payload);
			std::string held;
			size_t nonZero = 0;
			std::array<uint32_t, 256> stamps{};
			for (int i = 0; i < 256 && r.bitsLeft() >= 32; ++i)
			{
				const auto stamp = r.readU32();
				stamps[static_cast<size_t>(i)] = stamp;
				if (stamp == 0)
					continue;
				if (++nonZero <= 8)
					held += " (" + std::to_string(i % 16) + "," + std::to_string(i / 16) + ")=" + std::to_string(stamp);
			}
			log("TerrainRequestUpdatesEvent: " + std::to_string(ev.payloadBits) + " bits, " + std::to_string(nonZero) +
				" chunks with a stamp" + held + (nonZero > 8 ? " ..." : ""));
			log("  payload " + hexPreview(ev.payload, 1024));
			if (m_cb.onTerrainRequest)
				m_cb.onTerrainRequest(stamps);
			return;
		}

		std::string what = "event ignored: " + std::string(ev.name.empty() ? std::string_view{"(unknown class)"} : ev.name);
		if (ev.payloadBits > 0)
			what += ", " + std::to_string(ev.payloadBits) + " bits: " + hexPreview(ev.payload, 64);
		log(what);
	}

	void Connection::traceEvent(const InEvent& ev, bool guaranteed) const
	{
		if (!m_cb.trace)
			return;
		std::string line = std::string(guaranteed ? "event " : "event-unguaranteed ") + std::string(ev.name.empty() ? "?" : ev.name);
		if (ev.name == "NetStringEvent")
			line += " id=" + std::to_string(ev.id) + " text=" + ev.text;
		else if (ev.name == "RemoteCommandEvent")
		{
			for (const auto& a : ev.args)
				line += a.isTag ? " tag:" + std::to_string(a.tag) : " \"" + a.text + "\"";
		}
		else if (!ev.header.type.empty() || !ev.header.data.empty())
			line += " weapon=" + ev.header.weapon + " type=" + ev.header.type + " option=" + ev.header.option + " data=" + ev.header.data.substr(0, 200);
		if (ev.total)
			line += " id=" + std::to_string(ev.id) + " total=" + std::to_string(ev.total) + " seq=" + std::to_string(ev.sequence);
		if (ev.payloadBits)
			line += " bits=" + std::to_string(ev.payloadBits) + " hex=" + hexPreview(ev.payload, 48);
		m_cb.trace(line);
	}

	void Connection::traceState(const ClientState& state) const
	{
		if (!m_cb.trace)
			return;
		std::string line = std::format("state pos={:.3f},{:.3f},{:.3f}", state.position[0], state.position[1], state.position[2]);
		if (state.controlling)
		{
			const auto& t = state.object.transform;
			line += std::format(" changed=0x{:x} at={:.3f},{:.3f},{:.3f} axis={:.3f},{:.3f},{:.3f} angle={:.3f} scale={:.3f},{:.3f},{:.3f}", state.changed,
								t.position[0], t.position[1], t.position[2], t.axis[0], t.axis[1], t.axis[2], t.angle, state.object.scale[0],
								state.object.scale[1], state.object.scale[2]);
			if (state.changed & GameObjectBits::Chat)
				line += " chat=" + state.object.chat;
		}
		if (line == m_lastStateTrace)
			return;
		m_lastStateTrace = line;
		m_cb.trace(line);
	}
}
