// Modified by the Seihai project, 2026-11-11: see CHANGES-SEIHAI.md.
// New file: one accepted Graal 3D (Torque) UDP connection, server side. It keeps
// Torque's notify protocol (sequence numbers, acks, pings), resends guaranteed
// events that were lost, drives the mission start phases and carries Graal's
// BigData text channel both ways. It does no I/O itself: datagrams go out through
// a callback and come in through receive(), with the time passed in, so it can be
// tested without sockets.
//
// Not done here: ghosting (GameObject, terrain, the control object). Without a
// control object a 5.x client stops at login step 4 ("mission lighting done").

#ifndef SEIHAI_GRAAL3D_GRAAL3DCONNECTION_H
#define SEIHAI_GRAAL3D_GRAAL3DCONNECTION_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <graal3d/Graal3DProtocol.h>

namespace graal3d
{
	enum class MissionState
	{
		NotStarted,
		Phase1Sent, // 5.x: "Download Ghost Objects"; TGE builds: datablocks
		Phase2Sent, // 5.x: "Mission Lighting"
		Phase3Sent, // TGE builds only: "Mission Lighting"
		Done,       // every phase acknowledged
	};

	class Connection
	{
	public:
		struct Callbacks
		{
			std::function<void(std::string_view)> send;
			std::function<void(const TextMessage&)> onText;
			std::function<void(std::string_view)> log;
			std::function<void(MissionState)> onMission;
		};

		// Time-outs in milliseconds.
		static constexpr uint64_t TimeoutMs = 30000;
		static constexpr uint64_t KeepAliveMs = 1000;
		static constexpr uint64_t SendIntervalMs = 50;
		static constexpr size_t MaxPacketBytes = 400; // the clients accept up to 450

		Connection(const Profile& profile, uint32_t connectSequence, uint64_t nowMs, Callbacks callbacks);

		// A datagram whose first bit is 1 (a data, ping or ack packet).
		void receive(std::string_view datagram, uint64_t nowMs);

		// Sends what is due. Returns false once the client has gone quiet for TimeoutMs.
		bool update(uint64_t nowMs);

		// Queues a text message for the client (BigDataStartEvent + chunks).
		// Ignored for profiles whose game packets are not implemented.
		void sendText(const TextMessage& message);

		// The out-of-band disconnect packet for this connection.
		[[nodiscard]] std::string disconnectPacket(std::string_view reason) const;

		[[nodiscard]] const Profile& profile() const { return m_profile; }
		[[nodiscard]] uint32_t connectSequence() const { return m_connectSequence; }
		[[nodiscard]] MissionState missionState() const { return m_mission; }
		[[nodiscard]] bool established() const { return m_established; }
		[[nodiscard]] bool sendsEvents() const { return m_profile.gameObjectPackets; }
		[[nodiscard]] size_t pendingEvents() const { return m_sendQueue.size(); }
		[[nodiscard]] size_t unackedEvents() const { return m_events.size(); }

	private:
		struct OutEvent
		{
			uint32_t classId;
			BitWriter payload;
		};

		struct InEvent
		{
			uint32_t classId;
			std::string payload; // the payload bits, byte padded
			size_t bits;
		};

		struct Notify
		{
			uint32_t sequence;
			std::vector<int32_t> events; // guaranteed event sequence numbers
		};

		void postEvent(std::string_view className, BitWriter payload);
		void sendCommand(std::string_view command);
		void sendPacket(PacketType type, uint64_t nowMs);
		void handleNotify(bool delivered);
		void readDataPacket(BitReader& reader);
		bool readClientGameSection(BitReader& reader);
		void processEvent(uint32_t classId, BitReader& payload);
		void onEstablished();
		void log(std::string_view text) const;

		const Profile& m_profile;
		uint32_t m_connectSequence;
		Callbacks m_cb;

		// ConnectionProtocol state, as in Torque.
		uint32_t m_lastSeqRecvd = 0;
		uint32_t m_highestAckedSeq = 0;
		uint32_t m_lastSendSeq = 0;
		uint32_t m_ackMask = 0;
		uint32_t m_lastRecvAckAck = 0;
		std::array<uint32_t, 32> m_lastSeqRecvdAtSend{};
		bool m_established = false;

		uint64_t m_lastReceive;
		uint64_t m_lastSend = 0;
		bool m_ackOwed = false;

		// Guaranteed-ordered events out: every unacked event by sequence, the ones
		// still to (re)send in order, and the events in each unacked packet.
		int32_t m_nextSendEventSeq = 0;
		int32_t m_lastAckedEventSeq = -1;
		std::map<int32_t, OutEvent> m_events;
		std::deque<int32_t> m_sendQueue;
		std::deque<Notify> m_notifies;

		// Guaranteed-ordered events in.
		int32_t m_nextRecvEventSeq = 0;
		std::map<int32_t, InEvent> m_waiting;

		// Strings the client defined with NetStringEvent (5-bit ids).
		std::array<std::string, 32> m_remoteStrings{};
		uint32_t m_nextLocalString = 0;

		BigDataAssembler m_bigData;
		uint32_t m_nextBigDataId = 0;

		MissionState m_mission = MissionState::NotStarted;
		bool m_warnedControl = false;
	};

} // namespace graal3d

#endif
