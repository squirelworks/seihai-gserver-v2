// Modified by the Seihai project, 2026-11-11, 2026-11-12, 2026-11-13: see CHANGES-SEIHAI.md.
// New file: wire format of the Graal 3D (Torque) UDP connection. See Graal3DProtocol.h.

#include <graal3d/Graal3DProtocol.h>

#include <algorithm>

#include <bzlib.h>
#include <zlib.h>

namespace graal3d
{
	namespace
	{
		// 4.20r6 and 5.x networked classes (Graal_analysis torque/wire-by-build.md).
		const std::vector<std::string_view> kObjects5{
			"Camera", "DynamicGrass", "DynamicGrassReplicator", "DynamicShapeReplicator", "GameObject",
			"Sky", "Sun", "TerrainBlock", "WaterBlock", "fxSunLight",
		};
		const std::vector<std::string_view> kEvents5{
			"BigDataChunkEvent", "BigDataStartEvent", "ConnectionMessageEvent", "GhostAlwaysObjectEvent",
			"NetStringEvent", "RemoteCommandEvent", "RequestReloadEvent", "TerrainChunkEvent",
			"TerrainRequestUpdatesEvent",
		};
		const std::vector<std::string_view> kObjects420{
			"Camera", "DynamicGrass", "DynamicGrassReplicator", "DynamicShapeReplicator", "GameObject",
			"SimpleNetObject", "Sky", "Sun", "TerrainBlock", "WaterBlock", "fxSunLight", "twSurfaceReference",
		};
		const std::vector<std::string_view> kEvents420{
			"BigDataChunkEvent", "BigDataStartEvent", "ConnectionMessageEvent", "GhostAlwaysObjectEvent",
			"NetStringEvent", "RemoteCommandEvent", "RequestReloadEvent", "SimpleMessageEvent",
			"TerrainChunkEvent", "TerrainRequestUpdatesEvent",
		};
		// 3.153 and 4.04r2: the server ghosts one Camera per client (its control
		// object) with one CameraData datablock; see Graal3DTge.h.
		const std::vector<std::string_view> kObjects404{"Camera"};
		const std::vector<std::string_view> kDatablocks404{"CameraData"};
		const std::vector<std::string_view> kEvents404{
			"BigDataChunkEvent", "BigDataStartEvent", "ConnectionMessageEvent", "GhostAlwaysObjectEvent",
			"LightningStrikeEvent", "NetStringEvent", "PathManagerEvent", "RemoteCommandEvent",
			"RequestReloadEvent", "Sim2DAudioEvent", "Sim3DAudioEvent", "SimDataBlockEvent",
			"SimpleMessageEvent", "TerrainChunkEvent", "TerrainRequestUpdatesEvent",
		};

		const std::array<Profile, 3> kProfiles{
			Profile{19, "3.153, 4.04r2", true, false, 3, kObjects404, kEvents404, kDatablocks404, true},
			Profile{21, "4.20r6", false, true, 2, kObjects420, kEvents420},
			Profile{24, "5.x (5.00r7, 5.12)", false, true, 2, kObjects5, kEvents5},
		};

		void writeDigest(BitWriter& w, const Digest& digest)
		{
			for (auto d : digest)
				w.writeU32(d);
		}
	} // namespace

	const Profile* profileForProtocol(uint32_t protocol)
	{
		for (const auto& p : kProfiles)
		{
			if (p.protocol == protocol)
				return &p;
		}
		return nullptr;
	}

	std::optional<uint32_t> eventClassId(const Profile& profile, std::string_view name)
	{
		for (uint32_t i = 0; i < profile.eventClasses.size(); ++i)
		{
			if (profile.eventClasses[i] == name)
				return i;
		}
		return std::nullopt;
	}

	std::optional<uint32_t> objectClassId(const Profile& profile, std::string_view name)
	{
		for (uint32_t i = 0; i < profile.objectClasses.size(); ++i)
		{
			if (profile.objectClasses[i] == name)
				return i;
		}
		return std::nullopt;
	}

	std::optional<uint32_t> datablockClassId(const Profile& profile, std::string_view name)
	{
		for (uint32_t i = 0; i < profile.datablockClasses.size(); ++i)
		{
			if (profile.datablockClasses[i] == name)
				return i;
		}
		return std::nullopt;
	}

	std::string_view eventClassName(const Profile& profile, uint32_t id)
	{
		return id < profile.eventClasses.size() ? profile.eventClasses[id] : std::string_view{};
	}

	std::optional<uint32_t> parseChallengeRequest(std::string_view packet)
	{
		if (packet.size() < 5 || static_cast<uint8_t>(packet[0]) != static_cast<uint8_t>(InfoPacket::ChallengeRequest))
			return std::nullopt;
		BitReader r(packet);
		r.readU8();
		return r.readU32();
	}

	std::optional<ConnectRequest> parseConnectRequest(std::string_view packet)
	{
		if (packet.empty() || static_cast<uint8_t>(packet[0]) != static_cast<uint8_t>(InfoPacket::ConnectRequest))
			return std::nullopt;

		BitReader r(packet);
		r.readU8();
		ConnectRequest req;
		req.sequence = r.readU32();
		for (auto& d : req.digest)
			d = r.readU32();
		req.className = r.readString();
		req.classGroup = r.readU32();
		req.classCrc = r.readU32();
		req.game = r.readString();
		req.currentProtocol = r.readU32();
		req.minimumProtocol = r.readU32();
		req.joinPassword = r.readString();
		const auto argc = r.readU32();
		if (argc > 16)
			return std::nullopt; // CR_INVALID_ARGS
		for (uint32_t i = 0; i < argc && !r.overrun(); ++i)
			req.args.push_back(r.readString());
		if (r.overrun())
			return std::nullopt;
		return req;
	}

	std::optional<uint32_t> parseDisconnect(std::string_view packet, std::string* reason)
	{
		if (packet.size() < 5 || static_cast<uint8_t>(packet[0]) != static_cast<uint8_t>(InfoPacket::Disconnect))
			return std::nullopt;
		BitReader r(packet);
		r.readU8();
		const auto seq = r.readU32();
		if (reason != nullptr)
			*reason = r.readString();
		return seq;
	}

	std::string buildChallengeResponse(uint32_t sequence, const Digest& digest)
	{
		BitWriter w;
		w.writeU8(static_cast<uint8_t>(InfoPacket::ChallengeResponse));
		w.writeU32(sequence);
		writeDigest(w, digest);
		return w.toString();
	}

	std::string buildConnectAccept(uint32_t sequence, const Profile& profile)
	{
		BitWriter w;
		w.writeU8(static_cast<uint8_t>(InfoPacket::ConnectAccept));
		w.writeU32(sequence);

		// Graal's class-name table: flag 1, int12 type, int12 server id, name; flag 0.
		for (uint32_t i = 0; i < profile.objectClasses.size(); ++i)
		{
			w.writeFlag(true);
			w.writeInt(0, 12);
			w.writeInt(i, 12);
			w.writeString(profile.objectClasses[i]);
		}
		for (uint32_t i = 0; i < profile.datablockClasses.size(); ++i)
		{
			w.writeFlag(true);
			w.writeInt(1, 12);
			w.writeInt(i, 12);
			w.writeString(profile.datablockClasses[i]);
		}
		for (uint32_t i = 0; i < profile.eventClasses.size(); ++i)
		{
			w.writeFlag(true);
			w.writeInt(profile.eventType(), 12);
			w.writeInt(i, 12);
			w.writeString(profile.eventClasses[i]);
		}
		w.writeFlag(false);

		// GameConnection::writeConnectAccept: the server's protocol version.
		w.writeU32(profile.protocol);
		return w.toString();
	}

	std::string buildConnectReject(uint32_t sequence, std::string_view reason)
	{
		BitWriter w;
		w.writeU8(static_cast<uint8_t>(InfoPacket::ConnectReject));
		w.writeU32(sequence);
		w.writeString(reason);
		return w.toString();
	}

	std::string buildDisconnect(uint32_t sequence, std::string_view reason)
	{
		BitWriter w;
		w.writeU8(static_cast<uint8_t>(InfoPacket::Disconnect));
		w.writeU32(sequence);
		w.writeString(reason);
		return w.toString();
	}

	std::optional<std::string> checkConnectRequest(const ConnectRequest& request)
	{
		if (request.className != "GameConnection")
			return std::string{"CHR_INVALID"};
		if (request.game != GameString)
			return std::string{"CHR_GAME"};
		if (profileForProtocol(request.currentProtocol) == nullptr || request.minimumProtocol > request.currentProtocol)
			return std::string{"CHR_PROTOCOL"};
		if (request.args.empty() || request.args[0].empty())
			return std::string{"CR_INVALID_ARGS"};
		return std::nullopt;
	}

	std::optional<PacketHeader> readPacketHeader(BitReader& r)
	{
		if (!r.readFlag())
			return std::nullopt; // an info packet
		PacketHeader h;
		h.connectSequenceBit = r.readInt(1);
		h.sequence = r.readInt(9);
		h.highestAck = r.readInt(9);
		const auto type = r.readInt(2);
		h.ackByteCount = r.readInt(3);
		if (type > 2 || h.ackByteCount > 4)
			return std::nullopt;
		h.type = static_cast<PacketType>(type);
		h.ackMask = r.readInt(static_cast<int>(h.ackByteCount * 8));
		if (r.overrun())
			return std::nullopt;
		return h;
	}

	void writePacketHeader(BitWriter& w, const PacketHeader& h)
	{
		w.writeFlag(true);
		w.writeInt(h.connectSequenceBit & 1u, 1);
		w.writeInt(h.sequence & 0x1FFu, 9);
		w.writeInt(h.highestAck & 0x1FFu, 9);
		w.writeInt(static_cast<uint32_t>(h.type) & 3u, 2);
		w.writeInt(h.ackByteCount & 7u, 3);
		const uint32_t mask = h.ackByteCount >= 4 ? 0xFFFFFFFFu : ~(0xFFFFFFFFu << (h.ackByteCount * 8));
		w.writeInt(h.ackMask & mask, static_cast<int>(h.ackByteCount * 8));
	}

	void packCommandArg(BitWriter& w, const CommandArg& arg)
	{
		if (arg.isTag)
		{
			w.writeInt(static_cast<uint32_t>(StringCode::Tag), 2);
			w.writeInt(arg.tag, StringTableBits);
			return;
		}
		if (arg.text.empty())
		{
			w.writeInt(static_cast<uint32_t>(StringCode::Null), 2);
			return;
		}
		w.writeInt(static_cast<uint32_t>(StringCode::CString), 2);
		w.writeString(arg.text);
	}

	CommandArg unpackCommandArg(BitReader& r)
	{
		CommandArg arg;
		switch (static_cast<StringCode>(r.readInt(2)))
		{
			case StringCode::Null:
				break;
			case StringCode::CString:
				arg.text = r.readString();
				break;
			case StringCode::Tag:
				arg.isTag = true;
				arg.tag = r.readInt(StringTableBits);
				break;
			case StringCode::Integer:
			{
				const bool negative = r.readFlag();
				int32_t num;
				if (r.readFlag())
					num = static_cast<int32_t>(r.readInt(7));
				else if (r.readFlag())
					num = static_cast<int32_t>(r.readInt(15));
				else
					num = static_cast<int32_t>(r.readInt(31));
				arg.text = std::to_string(negative ? -num : num);
				break;
			}
		}
		return arg;
	}

	BitWriter netStringEvent(uint32_t index, std::string_view text, bool packetStrings)
	{
		BitWriter w;
		w.setPacketStrings(packetStrings);
		w.writeInt(index, StringTableBits);
		w.writeString(text);
		return w;
	}

	BitWriter remoteCommandEvent(const std::vector<CommandArg>& args, bool packetStrings)
	{
		BitWriter w;
		w.setPacketStrings(packetStrings);
		w.writeInt(static_cast<uint32_t>(args.size()), CommandArgsBits);
		for (const auto& a : args)
			packCommandArg(w, a);
		return w;
	}

	BitWriter bigDataStartEvent(uint32_t id, const TextMessage& message, uint32_t totalLength, BigDataMode mode, bool packetStrings)
	{
		BitWriter w;
		w.setPacketStrings(packetStrings);
		w.writeInt(id, BigDataIdBits);
		w.writeString(message.weapon);
		w.writeString(message.type);
		w.writeString(message.option);
		w.writeInt(totalLength, BigDataLengthBits);
		w.writeInt(static_cast<uint32_t>(mode), BigDataModeBits);
		return w;
	}

	BitWriter bigDataChunkEvent(uint32_t id, std::string_view bytes)
	{
		BitWriter w;
		w.writeInt(id, BigDataIdBits);
		w.writeInt(static_cast<uint32_t>(bytes.size()), 8);
		w.writeBytes(bytes);
		return w;
	}

	std::vector<BitWriter> bigDataEvents(uint32_t id, const TextMessage& message, bool packetStrings)
	{
		std::vector<BitWriter> out;
		std::string_view data = message.data;
		if (data.size() >= (1u << BigDataLengthBits))
			data = data.substr(0, (1u << BigDataLengthBits) - 1);

		out.push_back(bigDataStartEvent(id, message, static_cast<uint32_t>(data.size()), BigDataMode::Raw, packetStrings));
		for (size_t pos = 0; pos < data.size(); pos += BigDataChunkMax)
			out.push_back(bigDataChunkEvent(id, data.substr(pos, BigDataChunkMax)));
		return out;
	}

	void BigDataAssembler::start(BitReader& r)
	{
		const auto id = r.readInt(BigDataIdBits);
		TextMessage header;
		header.weapon = r.readString();
		header.type = r.readString();
		header.option = r.readString();
		const auto total = r.readInt(BigDataLengthBits);
		const auto mode = static_cast<BigDataMode>(r.readInt(BigDataModeBits));
		if (!r.overrun())
			start(id, std::move(header), total, mode);
	}

	void BigDataAssembler::start(uint32_t id, TextMessage header, uint32_t total, BigDataMode mode)
	{
		Transfer t;
		t.message = std::move(header);
		t.total = total;
		t.mode = mode;
		m_transfers[id] = std::move(t);
		if (m_transfers.size() > 64)
			m_transfers.erase(m_transfers.begin());
	}

	std::optional<TextMessage> BigDataAssembler::chunk(BitReader& r)
	{
		const auto id = r.readInt(BigDataIdBits);
		const auto length = r.readInt(8);
		auto bytes = r.readBytes(length);
		if (r.overrun())
			return std::nullopt;
		return chunk(id, bytes);
	}

	std::optional<TextMessage> BigDataAssembler::chunk(uint32_t id, std::string_view bytes)
	{
		const auto it = m_transfers.find(id);
		if (it == m_transfers.end())
			return std::nullopt;
		auto& t = it->second;
		t.bytes += bytes;
		if (t.bytes.size() < t.total)
			return std::nullopt;

		auto data = decodeBigData(t.bytes, t.mode);
		auto message = std::move(t.message);
		m_transfers.erase(it);
		if (!data)
			return std::nullopt;
		message.data = std::move(*data);
		return message;
	}

	std::optional<std::string> decodeBigData(std::string_view bytes, BigDataMode mode)
	{
		constexpr size_t kMax = 4u << 20;
		switch (mode)
		{
			case BigDataMode::Raw:
				return std::string(bytes);

			case BigDataMode::Zlib:
			{
				for (size_t cap = std::max<size_t>(bytes.size() * 4, 256); cap <= kMax; cap *= 2)
				{
					std::string out(cap, '\0');
					uLongf len = static_cast<uLongf>(cap);
					const auto rc = uncompress(reinterpret_cast<Bytef*>(out.data()), &len,
											   reinterpret_cast<const Bytef*>(bytes.data()), static_cast<uLong>(bytes.size()));
					if (rc == Z_OK)
					{
						out.resize(len);
						return out;
					}
					if (rc != Z_BUF_ERROR)
						return std::nullopt;
				}
				return std::nullopt;
			}

			case BigDataMode::Bzip2:
			{
				for (size_t cap = std::max<size_t>(bytes.size() * 4, 256); cap <= kMax; cap *= 2)
				{
					std::string out(cap, '\0');
					auto len = static_cast<unsigned int>(cap);
					const auto rc = BZ2_bzBuffToBuffDecompress(out.data(), &len, const_cast<char*>(bytes.data()),
															   static_cast<unsigned int>(bytes.size()), 0, 0);
					if (rc == BZ_OK)
					{
						out.resize(len);
						return out;
					}
					if (rc != BZ_OUTBUFF_FULL)
						return std::nullopt;
				}
				return std::nullopt;
			}
		}
		return std::nullopt;
	}

} // namespace graal3d
