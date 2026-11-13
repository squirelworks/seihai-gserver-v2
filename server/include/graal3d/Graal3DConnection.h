// Modified by the Seihai project, 2026-11-11, 2026-11-12, 2026-11-13: see CHANGES-SEIHAI.md.
// New file: one accepted Graal 3D (Torque) UDP connection, server side. It keeps
// Torque's notify protocol (sequence numbers, acks, pings), resends guaranteed
// events that were lost, drives the mission start phases and carries Graal's
// BigData text channel both ways. It does no I/O itself: datagrams go out through
// a callback and come in through receive(), with the time passed in, so it can be
// tested without sockets.
//
// Ghosting (GameObject era, 4.20r6 and 5.x): the owner names objects by key and
// the connection gives each a ghost index, sends creates, updates and deletes in
// the ghost section of its packets, and resends what a lost packet carried. Once
// the client has a ghost, it can be made the control object; the first control
// object takes a 5.x client to login step 5. The client then reports its own
// position and its control object's changes in every packet (onClientState).
//
// Protocol 19 (3.153, 4.04r2, the stock TGE game layer; see Graal3DTge.h): the
// mission start has three phases. After phase 1's ack the connection sends its one
// datablock (a CameraData) and DataBlocksDone; once the client says the datablocks
// are loaded it sends phase 2, GhostAlwaysStarting and lets ghosts out (the owner
// gives it one TgeCamera); once phase 2 is acked and every ghost is in, it sends
// GhostAlwaysDone and phase 3, and after phase 3's ack the Camera becomes the
// control object. The client then sends moves, which are acknowledged but not
// applied.

#ifndef SEIHAI_GRAAL3D_GRAAL3DCONNECTION_H
#define SEIHAI_GRAAL3D_GRAAL3DCONNECTION_H

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <graal3d/Graal3DObjects.h>
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

	// What a 5.x client says about itself in each packet.
	struct ClientState
	{
		std::array<float, 3> position{}; // its control object's position
		bool controlling = false;        // `object` and `changed` are filled in
		uint32_t changed = 0;            // GameObjectBits the client changed
		GameObjectState object;          // the client's control object, as it now is
	};

	using ObjectKey = uint32_t;

	class Connection
	{
	public:
		struct Callbacks
		{
			std::function<void(std::string_view)> send;
			std::function<void(const TextMessage&)> onText;
			std::function<void(std::string_view)> log;
			std::function<void(MissionState)> onMission;
			std::function<void(const ClientState&)> onClientState;
		};

		// Time-outs in milliseconds.
		static constexpr uint64_t TimeoutMs = 30000;
		static constexpr uint64_t KeepAliveMs = 1000;
		static constexpr uint64_t SendIntervalMs = 50;
		static constexpr size_t MaxPacketBytes = 400; // the clients accept up to 450
		static constexpr uint32_t MaxGhosts = 4096;   // the client's ghost table

		Connection(const Profile& profile, uint32_t connectSequence, uint64_t nowMs, Callbacks callbacks);

		// A datagram whose first bit is 1 (a data, ping or ack packet).
		void receive(std::string_view datagram, uint64_t nowMs);

		// Sends what is due. Returns false once the client has gone quiet for TimeoutMs.
		bool update(uint64_t nowMs);

		// Queues a text message for the client (BigDataStartEvent + chunks).
		// Ignored for profiles whose game packets are not implemented.
		void sendText(const TextMessage& message);

		// Strings inside game packets carry the packet string flag (default on;
		// see TorqueBitStream.h). Set before anything is sent.
		void setPacketStrings(bool on) { m_packetStrings = on; }

		// Ghosting. setGhost adds an object or updates it (only what changed is
		// sent); removeGhost deletes it on the client. Ignored for profiles whose
		// game packets are not implemented.
		void setGhost(ObjectKey key, const GhostObject& object);
		void removeGhost(ObjectKey key);
		// Sends the object's whole update again (once the client has it).
		void resendGhost(ObjectKey key);
		// Gives the client this object as its control object, once its ghost is in.
		void setControlObject(ObjectKey key);
		// Gives the client this Camera ghost as its camera, once its ghost is in.
		// The camera is named in every packet from then on (no name clears it).
		void setCameraObject(ObjectKey key);
		[[nodiscard]] bool cameraNamed() const { return m_cameraNamed; }

		[[nodiscard]] std::optional<uint32_t> ghostIndex(ObjectKey key) const;
		[[nodiscard]] bool ghostDelivered(ObjectKey key) const;
		[[nodiscard]] size_t ghostCount() const { return m_ghosts.size(); }
		[[nodiscard]] bool controlObjectDelivered() const { return m_controlDelivered; }

		// The out-of-band disconnect packet for this connection.
		[[nodiscard]] std::string disconnectPacket(std::string_view reason) const;

		[[nodiscard]] const Profile& profile() const { return m_profile; }
		[[nodiscard]] uint32_t connectSequence() const { return m_connectSequence; }
		[[nodiscard]] MissionState missionState() const { return m_mission; }
		[[nodiscard]] bool established() const { return m_established; }
		[[nodiscard]] bool sendsEvents() const { return m_profile.gamePackets(); }
		// Protocol 19: the last move index acknowledged to the client, and how many
		// moves it has sent.
		[[nodiscard]] uint32_t lastMoveAck() const { return m_lastMoveAck; }
		[[nodiscard]] size_t movesReceived() const { return m_movesReceived; }
		[[nodiscard]] size_t pendingEvents() const { return m_sendQueue.size(); }
		[[nodiscard]] size_t unackedEvents() const { return m_events.size(); }

	private:
		struct OutEvent
		{
			uint32_t classId;
			BitWriter payload;
		};

		// A client event, decoded where it stood in its packet: strings can refer
		// to the string before them in the same packet.
		struct InEvent
		{
			std::string_view name;
			uint32_t id = 0; // NetString index, BigData id, ConnectionMessage type
			std::string text;
			std::vector<CommandArg> args;
			TextMessage header;
			uint32_t total = 0;
			BigDataMode mode = BigDataMode::Raw;
			uint32_t sequence = 0;
			uint32_t count = 0;
		};

		struct Ghost
		{
			uint32_t index = 0;
			uint32_t classId = 0;
			GhostObject object;
			uint32_t pending = 0;        // dirty bits not yet sent
			bool created = false;        // the client acked the create
			bool createInFlight = false;
			bool kill = false;
			bool killInFlight = false;
		};

		struct GhostSent
		{
			ObjectKey key;
			uint32_t mask;
			bool create;
			bool kill;
		};

		struct Notify
		{
			uint32_t sequence;
			std::vector<int32_t> events; // guaranteed event sequence numbers
			std::vector<GhostSent> ghosts;
			bool control = false;
		};

		void postEvent(std::string_view className, BitWriter payload);
		void sendCommand(std::string_view command, const std::vector<std::string>& args = {});
		void startTgeDatablocks();
		void advanceTge();
		void sendPacket(PacketType type, uint64_t nowMs);
		void handleNotify(bool delivered);
		void readDataPacket(BitReader& reader);
		bool readClientGameSection(BitReader& reader);
		bool readTgeClientGameSection(BitReader& reader);
		std::optional<InEvent> decodeEvent(uint32_t classId, const std::string& payload, size_t bits, std::string& stringBuffer);
		void processEvent(const InEvent& event);
		void writeControlSection(BitWriter& writer, Notify& note, uint64_t nowMs);
		void writeCameraSection(BitWriter& writer);
		void writeGhostSection(BitWriter& writer, Notify& note);
		void ghostNotify(const Notify& note, bool delivered);
		[[nodiscard]] bool ghostWorkPending() const;
		[[nodiscard]] bool controlPending() const;
		std::optional<uint32_t> allocateGhostIndex();
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
		bool m_packetStrings = true;
		bool m_warnedStrings = false;

		std::map<ObjectKey, Ghost> m_ghosts;
		std::set<uint32_t> m_usedIndices;
		std::optional<ObjectKey> m_controlKey;
		bool m_controlInFlight = false;
		bool m_controlDelivered = false;
		std::optional<ObjectKey> m_cameraKey;
		bool m_cameraNamed = false;
		// Lab diagnostics: each logged once per connection.
		bool m_warnedWindow = false;
		bool m_warnedAckAhead = false;

		// Protocol 19 mission start and moves.
		uint32_t m_missionSeq = 1;
		bool m_datablocksSent = false;
		bool m_ghostingAllowed = false;
		bool m_phase2Acked = false;
		uint32_t m_lastMoveAck = 0;
		size_t m_movesReceived = 0;

		// The client's own control object, as its echoes describe it.
		GameObjectState m_clientObject;
		std::string m_clientMoveType;
	};

} // namespace graal3d

#endif
