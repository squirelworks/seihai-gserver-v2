// Modified by the Seihai project, 2026-11-11: see CHANGES-SEIHAI.md.
// New file: the wire format of the Torque UDP connection that the Graal 3.153 to 5.x
// clients open to a 3D server, on the gserver's port + 1. Out-of-band handshake
// packets, the per-version profiles (protocol 19, 21, 24), the data packet header,
// and the network events the 3D server needs: NetStringEvent, RemoteCommandEvent and
// Graal's BigDataStartEvent / BigDataChunkEvent text channel.
//
// Sources: Graal_analysis torque/network.md, torque/wire-by-build.md,
// graal3d/torque-engine-4x.md, and the 2026-10-30 Wine lab with the real 5.12
// client. Graal's changes to stock Torque: a class-name table in the connect accept,
// 12-bit class ids in the server's numbering, a 14-bit bit length before every event
// payload, and 10-bit rate fields.

#ifndef SEIHAI_GRAAL3D_GRAAL3DPROTOCOL_H
#define SEIHAI_GRAAL3D_GRAAL3DPROTOCOL_H

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <graal3d/TorqueBitStream.h>

namespace graal3d
{
	// Out-of-band packet types (first byte, low bit 0).
	enum class InfoPacket : uint8_t
	{
		ChallengeRequest = 0x1A,
		ChallengeReject = 0x1C,
		ChallengeResponse = 0x1E,
		ConnectRequest = 0x20,
		ConnectReject = 0x22,
		ConnectAccept = 0x24,
		Disconnect = 0x26,
	};

	// The game string every Graal 3D client sends from 3.153 on.
	inline constexpr std::string_view GameString = "Graal3D";

	// What a client build expects. The accept must carry exactly `protocol`.
	struct Profile
	{
		uint32_t protocol;
		std::string_view clients;
		// 3.153 and 4.04r2 number class types object 0, datablock 1, event 2;
		// 4.20r6 and 5.x have no datablocks: object 0, event 1.
		bool hasDatablocks;
		// The GameObject-era game packet (4.20r6, 5.x). The TGE layout of 3.153 and
		// 4.04r2 (moves, datablocks) is not implemented, so those clients get the
		// handshake and keep-alive only.
		bool gameObjectPackets;
		// Mission phases the server drives: 2 in the GameObject era, 3 before.
		int missionPhases;
		std::vector<std::string_view> objectClasses;
		std::vector<std::string_view> eventClasses;

		[[nodiscard]] uint32_t eventType() const { return hasDatablocks ? 2 : 1; }
	};

	const Profile* profileForProtocol(uint32_t protocol);

	// Server class ids: the index in the profile's object or event list.
	std::optional<uint32_t> eventClassId(const Profile& profile, std::string_view name);
	std::string_view eventClassName(const Profile& profile, uint32_t id);

	using Digest = std::array<uint32_t, 4>;

	struct ConnectRequest
	{
		uint32_t sequence = 0;
		Digest digest{};
		std::string className;
		uint32_t classGroup = 0;
		uint32_t classCrc = 0;
		std::string game;
		uint32_t currentProtocol = 0;
		uint32_t minimumProtocol = 0;
		std::string joinPassword;
		std::vector<std::string> args; // 4.04r2 and 5.x: account, nickname
	};

	std::optional<uint32_t> parseChallengeRequest(std::string_view packet);
	std::optional<ConnectRequest> parseConnectRequest(std::string_view packet);
	std::optional<uint32_t> parseDisconnect(std::string_view packet, std::string* reason = nullptr);

	std::string buildChallengeResponse(uint32_t sequence, const Digest& digest);
	std::string buildConnectAccept(uint32_t sequence, const Profile& profile);
	std::string buildConnectReject(uint32_t sequence, std::string_view reason);
	std::string buildDisconnect(uint32_t sequence, std::string_view reason);

	// Why a connect request is refused, or nullopt if it may go on.
	// The texts are the 5.12 client's own (CHR_*), so the player sees a known message.
	std::optional<std::string> checkConnectRequest(const ConnectRequest& request);

	// --- Data packets -------------------------------------------------------

	enum class PacketType : uint8_t
	{
		Data = 0,
		Ping = 1,
		Ack = 2,
	};

	struct PacketHeader
	{
		uint32_t connectSequenceBit = 0;
		uint32_t sequence = 0;   // 9 bits on the wire
		uint32_t highestAck = 0; // 9 bits on the wire
		PacketType type = PacketType::Data;
		uint32_t ackByteCount = 0;
		uint32_t ackMask = 0;
	};

	std::optional<PacketHeader> readPacketHeader(BitReader& reader);
	void writePacketHeader(BitWriter& writer, const PacketHeader& header);

	// --- Events -------------------------------------------------------------

	// NetConnection::packString codes.
	enum class StringCode : uint32_t
	{
		Null = 0,
		CString = 1,
		Tag = 2,
		Integer = 3,
	};

	// The connection string table has 32 entries (5-bit ids) in 5.12.
	inline constexpr int StringTableBits = 5;
	inline constexpr int CommandArgsBits = 5;

	// A RemoteCommandEvent argument as it travels: a tag id or a plain string.
	struct CommandArg
	{
		bool isTag = false;
		uint32_t tag = 0;
		std::string text;
	};

	void packCommandArg(BitWriter& writer, const CommandArg& arg);
	CommandArg unpackCommandArg(BitReader& reader);

	BitWriter netStringEvent(uint32_t index, std::string_view text);
	BitWriter remoteCommandEvent(const std::vector<CommandArg>& args);

	// Graal's text channel: one message is four strings, like a GS2 sendtext.
	struct TextMessage
	{
		std::string weapon; // s1: empty, or the weapon the text is for
		std::string type;   // s2: set, unset, events, addweapon, weaponscript, ...
		std::string option; // s3
		std::string data;
	};

	inline constexpr int BigDataIdBits = 16;
	inline constexpr int BigDataLengthBits = 24;
	inline constexpr int BigDataModeBits = 3;
	inline constexpr size_t BigDataChunkMax = 191;

	enum class BigDataMode : uint32_t
	{
		Raw = 0,
		Zlib = 1,
		Bzip2 = 2,
	};

	BitWriter bigDataStartEvent(uint32_t id, const TextMessage& message, uint32_t totalLength, BigDataMode mode);
	BitWriter bigDataChunkEvent(uint32_t id, std::string_view bytes);

	// Splits a message into a start event and its chunk events. The data is sent
	// uncompressed: mode 0 is valid in every build and avoids depending on the
	// exact zlib and bzip2 framing.
	std::vector<BitWriter> bigDataEvents(uint32_t id, const TextMessage& message);

	// Reassembles BigData transfers that arrive from a client.
	class BigDataAssembler
	{
	public:
		void start(BitReader& payload);
		// Returns the finished message once its last chunk is in.
		std::optional<TextMessage> chunk(BitReader& payload);

	private:
		struct Transfer
		{
			TextMessage message;
			uint32_t total = 0;
			BigDataMode mode = BigDataMode::Raw;
			std::string bytes;
		};
		std::map<uint32_t, Transfer> m_transfers;
	};

	// Inflates a BigData body. Returns nullopt if it cannot be decoded.
	std::optional<std::string> decodeBigData(std::string_view bytes, BigDataMode mode);

} // namespace graal3d

#endif
