// Modified by the Seihai project, 2026-11-11, 2026-11-12: see CHANGES-SEIHAI.md.
// New file: the bit stream the Torque-based Graal 3D clients (3.153 to 5.x) use on
// their UDP connection to a 3D server. Bits are packed least significant first.
// Strings use Torque's fixed Huffman tree. The character frequency table is the
// one in the MIT-licensed Torque3D source (core/stream/bitStream.cpp); the Graal
// clients use the same table. Kept apart from the server so it can be tested alone.
//
// Inside game packets the Graal clients, like stock TGE 1.x, give the stream a
// 256-byte string buffer for the length of the packet (GameConnection::readPacket
// and writePacket of the 5.00r7 Graal3DEngine.dll). Every string then starts with
// a flag saying it reuses a prefix of the previous string in that packet. Out-of-
// band packets (the handshake) have no buffer and no flag.

#ifndef SEIHAI_GRAAL3D_TORQUEBITSTREAM_H
#define SEIHAI_GRAAL3D_TORQUEBITSTREAM_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace graal3d
{
	class BitWriter
	{
	public:
		bool writeFlag(bool value);
		void writeInt(uint32_t value, int bits);
		void writeU8(uint8_t value) { writeInt(value, 8); }
		void writeU32(uint32_t value) { writeInt(value, 32); }
		void writeF32(float value);
		void writeBytes(std::span<const uint8_t> bytes);
		void writeBytes(std::string_view bytes);
		void writeBits(const BitWriter& other);

		// A Torque string: flag (1 = Huffman), U8 length, then codes or bytes.
		// At most 255 bytes are written. With packet strings on, a 0 prefix flag
		// comes first (no prefix is ever shared).
		void writeString(std::string_view text);

		// Strings for a game packet (see above). Off by default.
		void setPacketStrings(bool on) { m_packetStrings = on; }
		[[nodiscard]] bool packetStrings() const { return m_packetStrings; }

		// Overwrite `bits` bits at an earlier position (for length prefixes).
		void patchInt(size_t bitPos, uint32_t value, int bits);

		[[nodiscard]] size_t bitPosition() const { return m_bitCount; }
		[[nodiscard]] size_t byteSize() const { return (m_bitCount + 7) / 8; }
		[[nodiscard]] const std::vector<uint8_t>& bytes() const { return m_data; }
		[[nodiscard]] std::string toString() const { return std::string(m_data.begin(), m_data.end()); }

	private:
		void writeBit(bool value);

		std::vector<uint8_t> m_data;
		size_t m_bitCount = 0;
		bool m_packetStrings = false;
	};

	class BitReader
	{
	public:
		BitReader(const uint8_t* data, size_t size)
			: m_data(data), m_bitSize(size * 8) {}
		explicit BitReader(std::string_view data)
			: BitReader(reinterpret_cast<const uint8_t*>(data.data()), data.size()) {}
		explicit BitReader(std::span<const uint8_t> data)
			: BitReader(data.data(), data.size()) {}

		bool readFlag();
		uint32_t readInt(int bits);
		uint8_t readU8() { return static_cast<uint8_t>(readInt(8)); }
		uint32_t readU32() { return readInt(32); }
		float readF32();
		std::string readBytes(size_t count);
		std::string readString();

		// Game packet strings: `buffer` holds the last string read in this packet
		// and is updated by every readString. nullptr (the default) for none.
		void setStringBuffer(std::string* buffer) { m_stringBuffer = buffer; }

		[[nodiscard]] size_t bitPosition() const { return m_bitPos; }
		void setBitPosition(size_t pos) { m_bitPos = pos > m_bitSize ? m_bitSize : pos; }
		[[nodiscard]] size_t bitsLeft() const { return m_bitPos >= m_bitSize ? 0 : m_bitSize - m_bitPos; }

		// Set when a read ran past the end of the data. Reads past the end return 0.
		[[nodiscard]] bool overrun() const { return m_overrun; }

	private:
		std::string readPlainString();

		const uint8_t* m_data;
		size_t m_bitSize;
		size_t m_bitPos = 0;
		bool m_overrun = false;
		std::string* m_stringBuffer = nullptr;
	};

	// The Huffman code length in bits of a string, as Torque counts it.
	size_t huffmanBits(std::string_view text);

} // namespace graal3d

#endif
