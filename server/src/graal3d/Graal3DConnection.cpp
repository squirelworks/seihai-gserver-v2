// Modified by the Seihai project, 2026-11-11: see CHANGES-SEIHAI.md.
// New file: one Graal 3D (Torque) UDP connection, server side. See Graal3DConnection.h.
// The notify protocol follows Torque's ConnectionProtocol::processRawPacket and
// NetConnection's event code (MIT-licensed Torque3D source), with Graal's 12-bit
// class ids, 14-bit event lengths and 10-bit rate fields.

#include <graal3d/Graal3DConnection.h>

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
			return;

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
		if (!m_profile.gameObjectPackets)
			return;

		// The 5.x client answers these natively and raises onGraal3DLoginStep.
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
		if (sendsEvents() && m_established)
		{
			const bool due = (!m_sendQueue.empty() || m_ackOwed) && sinceSend >= SendIntervalMs;
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

	void Connection::sendCommand(std::string_view command)
	{
		// commandToClient sends the command name as a tagged string: the string
		// first (NetStringEvent), then the command that refers to it.
		const uint32_t tag = m_nextLocalString;
		m_nextLocalString = (m_nextLocalString + 1) % 32;
		postEvent("NetStringEvent", netStringEvent(tag, command));
		postEvent("RemoteCommandEvent", remoteCommandEvent({CommandArg{true, tag, {}}}));
	}

	void Connection::sendText(const TextMessage& message)
	{
		if (!sendsEvents())
			return;
		const uint32_t id = m_nextBigDataId;
		m_nextBigDataId = (m_nextBigDataId + 1) & ((1u << BigDataIdBits) - 1);
		auto events = bigDataEvents(id, message);
		for (size_t i = 0; i < events.size(); ++i)
			postEvent(i == 0 ? "BigDataStartEvent" : "BigDataChunkEvent", std::move(events[i]));
	}

	void Connection::sendPacket(PacketType type, uint64_t nowMs)
	{
		if (type == PacketType::Data && m_lastSendSeq - m_highestAckedSeq >= 30)
			type = PacketType::Ack; // window full: acks only

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

		// GameConnection::writePacket, GameObject era: no control object, no camera,
		// no field of view.
		w.writeFlag(false);
		w.writeFlag(false);
		w.writeFlag(false);

		// Events: no unguaranteed ones, then the guaranteed-ordered queue.
		Notify note{m_lastSendSeq, {}};
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

		// Ghosts: none.
		w.writeFlag(false);

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

	bool Connection::readClientGameSection(BitReader& r)
	{
		// 5.12 GameConnection::writePacket on the client: a flag, the control
		// object's position, its ghost index, its own state, the field of view.
		r.readFlag();
		r.readF32();
		r.readF32();
		r.readF32();
		if (r.readFlag())
			r.readInt(12);
		if (r.readFlag())
		{
			// The client only does this for a control object we gave it. Its
			// layout is not known, so the rest of the packet cannot be read.
			if (!m_warnedControl)
				log("client sent control object state; its events are skipped");
			m_warnedControl = true;
			return false;
		}
		if (r.readFlag())
			r.readInt(8);
		return !r.overrun();
	}

	void Connection::readDataPacket(BitReader& r)
	{
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

		if (!m_profile.gameObjectPackets || !readClientGameSection(r))
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

			if (unguaranteed)
			{
				BitReader payload(copy.bytes().data(), copy.byteSize());
				processEvent(classId, payload);
				continue;
			}

			seq |= (m_nextRecvEventSeq & ~0x7F);
			if (seq < m_nextRecvEventSeq)
				seq += 128;
			m_waiting.emplace(seq, InEvent{classId, copy.toString(), bits});
		}

		while (!m_waiting.empty() && m_waiting.begin()->first == m_nextRecvEventSeq)
		{
			auto ev = std::move(m_waiting.begin()->second);
			m_waiting.erase(m_waiting.begin());
			++m_nextRecvEventSeq;
			BitReader payload(ev.payload);
			processEvent(ev.classId, payload);
		}
	}

	void Connection::processEvent(uint32_t classId, BitReader& r)
	{
		const auto name = eventClassName(m_profile, classId);
		if (name == "NetStringEvent")
		{
			const auto index = r.readInt(StringTableBits);
			m_remoteStrings[index] = r.readString();
			return;
		}

		if (name == "RemoteCommandEvent")
		{
			const auto argc = r.readInt(CommandArgsBits);
			std::vector<std::string> args;
			for (uint32_t i = 0; i < argc && !r.overrun(); ++i)
			{
				auto arg = unpackCommandArg(r);
				args.push_back(arg.isTag ? m_remoteStrings[arg.tag % 32] : std::move(arg.text));
			}
			if (args.empty())
				return;

			const auto& command = args[0];
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

		if (name == "BigDataStartEvent")
		{
			m_bigData.start(r);
			return;
		}

		if (name == "BigDataChunkEvent")
		{
			if (auto message = m_bigData.chunk(r); message && m_cb.onText)
				m_cb.onText(*message);
			return;
		}

		log("event ignored: " + std::string(name.empty() ? std::string_view{"(unknown class)"} : name));
	}

} // namespace graal3d
