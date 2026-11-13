// Modified by the Seihai project, 2026-11-11, 2026-11-12, 2026-11-13: see CHANGES-SEIHAI.md.
// New file: one Graal 3D (Torque) UDP connection, server side. See Graal3DConnection.h.
// The notify protocol follows Torque's ConnectionProtocol::processRawPacket and
// NetConnection's event code (MIT-licensed Torque3D source), with Graal's 12-bit
// class ids, 14-bit event lengths and 10-bit rate fields. Ghosting follows the
// 5.00r7 Graal3DEngine.dll's ghostReadPacket: an int4 index width sent as is, and a
// class id and int14 length on every ghost update.

#include <graal3d/Graal3DConnection.h>
#include <graal3d/Graal3DTge.h>

#include <algorithm>
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
		if (sendsEvents() && m_established)
		{
			const bool work = !m_sendQueue.empty() || m_ackOwed || ghostWorkPending() || controlPending();
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

	void Connection::postEvent(std::string_view className, BitWriter payload)
	{
		const auto id = eventClassId(m_profile, className);
		if (!id)
			return;
		const int32_t seq = m_nextSendEventSeq++;
		m_events.emplace(seq, OutEvent{*id, std::move(payload)});
		m_sendQueue.push_back(seq);
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
		// with the mission sequence. This server has one: the CameraData every
		// Camera ghost uses.
		if (m_datablocksSent)
			return;
		m_datablocksSent = true;
		const auto classId = datablockClassId(m_profile, "CameraData");
		if (classId)
		{
			const auto data = tge::cameraData("", m_packetStrings);
			postEvent("SimDataBlockEvent", tge::simDataBlockEvent(tge::FirstDatablockId, *classId, 0, 1, data));
		}
		postEvent("ConnectionMessageEvent", tge::connectionMessageEvent(m_missionSeq, tge::message::DataBlocksDone, 0));
		log("datablocks sent (1), then DataBlocksDone");
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
		if (m_profile.tgePackets)
		{
			// GameConnection::writePacket, protocol 19: move ack, then the control
			// object with its packet data, no camera, no field of view.
			std::optional<tge::ServerControl> control;
			if (controlPending())
			{
				const auto& g = m_ghosts.at(*m_controlKey);
				control = tge::ServerControl{static_cast<uint32_t>(nowMs), g.index, g.object.tgeCamera};
				note.control = true;
				m_controlInFlight = true;
				log("control object (Camera ghost " + std::to_string(g.index) + ") sent in seq " + std::to_string(note.sequence));
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

		// Events: no unguaranteed ones, then the guaranteed-ordered queue.
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
			w.writeInt(ev.classId, 12);
			w.writeInt(static_cast<uint32_t>(ev.payload.bitPosition()), 14);
			w.writeBits(ev.payload);
			note.events.push_back(seq);
			m_sendQueue.pop_front();
		}
		w.writeFlag(false);

		writeGhostSection(w, note);

		m_notifies.push_back(std::move(note));
		if (m_cb.send)
			m_cb.send(w.toString());
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
				m_events.erase(seq);
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
		// Each era ghosts only its own kinds: protocol 19 the TGE Camera, the
		// GameObject era everything else.
		if (!m_profile.gamePackets() || object.isTge() != m_profile.tgePackets)
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
			m_ghosts.emplace(key, std::move(g));
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
		if (m_cameraKey)
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

	bool Connection::ghostWorkPending() const
	{
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
		if (!w.writeFlag(controlPending()))
			return;

		const auto& g = m_ghosts.at(*m_controlKey);
		w.writeU32(static_cast<uint32_t>(nowMs)); // the client's synchronised clock (timevar3)
		w.writeInt(g.index, 12);
		// The packet data is a full GameObject update with no length prefix. The
		// ghost already carries the object's state, so no section is set.
		writeGameObjectUpdate(w, g.object.object, 0);
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

		// Protocol 19: no ghost before the client has loaded the datablocks they
		// name (an unknown datablock id drops the connection).
		if (m_profile.tgePackets && !m_ghostingAllowed)
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
				g.object.writeUpdate(payload, mask);
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
				w.writeInt(g.classId, 12);
				w.writeInt(static_cast<uint32_t>(payload.bitPosition()), 14);
				w.writeBits(payload);
			}

			note.ghosts.push_back({c.key, mask, create, g.kill});
			if (g.kill)
				g.killInFlight = true;
			else if (create)
				g.createInFlight = true;
			g.pending = 0;
		}
		w.writeFlag(false);
	}

	void Connection::ghostNotify(const Notify& note, bool delivered)
	{
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
		}
		if (r.readFlag())
			r.readInt(8);
		if (r.overrun())
			return false;

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
			// Ack every move the client has sent; the Camera is not simulated here,
			// the client predicts its own control object.
			const uint32_t next = game->firstMove + static_cast<uint32_t>(game->moves.size());
			if (next > m_lastMoveAck)
			{
				m_movesReceived += next - std::max(m_lastMoveAck, game->firstMove);
				m_lastMoveAck = next;
				m_ackOwed = true;
			}
		}
		return true;
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
				return std::pair{std::move(out), true}; // not decoded: nothing to check
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

		log("event ignored: " + std::string(ev.name.empty() ? std::string_view{"(unknown class)"} : ev.name));
	}

} // namespace graal3d
