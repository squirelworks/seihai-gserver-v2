// Modified by the Seihai project, 2026-11-11: see CHANGES-SEIHAI.md.
// New file: Torque bit stream and Huffman strings for the Graal 3D UDP connection.
// The frequency table and the tree construction follow the MIT-licensed Torque3D
// source (core/stream/bitStream.cpp, HuffmanProcessor), which matches the strings
// the real 5.12 client sent in the 2026-10-30 lab.

#include <graal3d/TorqueBitStream.h>

#include <array>
#include <cstring>

namespace graal3d
{
	namespace
	{
		// Torque's character frequencies (index = byte value).
		constexpr std::array<uint32_t, 256> kFrequencies{
			0, 0, 0, 0, 0, 0, 0, 0, 0, 329, 21, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			2809, 68, 0, 27, 0, 58, 3, 62, 4, 7, 0, 0, 15, 65, 554, 3,
			394, 404, 189, 117, 30, 51, 27, 15, 34, 32, 80, 1, 142, 3, 142, 39,
			0, 144, 125, 44, 122, 275, 70, 135, 61, 127, 8, 12, 113, 246, 122, 36,
			185, 1, 149, 309, 335, 12, 11, 14, 54, 151, 0, 0, 2, 0, 0, 211,
			0, 2090, 344, 736, 993, 2872, 701, 605, 646, 1552, 328, 305, 1240, 735, 1533, 1713,
			562, 3, 1775, 1149, 1469, 979, 407, 553, 59, 279, 31, 0, 0, 0, 68, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
		};

		// A child index: >= 0 is a node, < 0 is the leaf for byte -(index + 1).
		struct Node
		{
			uint32_t pop;
			int32_t index0;
			int32_t index1;
		};

		struct Code
		{
			uint32_t bits = 0; // bit i is the i-th step from the root
			int numBits = 0;
		};

		struct Tree
		{
			std::vector<Node> nodes;
			std::array<Code, 256> codes{};

			Tree()
			{
				struct Wrap
				{
					bool leaf;
					int32_t index;
				};

				std::array<uint32_t, 256> leafPop{};
				for (size_t i = 0; i < 256; ++i)
					leafPop[i] = kFrequencies[i] + 1;

				nodes.push_back(Node{0, 0, 0}); // node 0 becomes the root below
				std::vector<Wrap> wraps;
				for (int32_t i = 0; i < 256; ++i)
					wraps.push_back(Wrap{true, i});

				auto pop = [&](const Wrap& w) { return w.leaf ? leafPop[w.index] : nodes[w.index].pop; };
				auto childIndex = [](const Wrap& w) { return w.leaf ? -(w.index + 1) : w.index; };

				while (wraps.size() != 1)
				{
					uint32_t min1 = 0xfffffffe, min2 = 0xffffffff;
					int32_t i1 = -1, i2 = -1;
					for (int32_t i = 0; i < static_cast<int32_t>(wraps.size()); ++i)
					{
						const auto p = pop(wraps[i]);
						if (p < min1)
						{
							min2 = min1;
							i2 = i1;
							min1 = p;
							i1 = i;
						}
						else if (p < min2)
						{
							min2 = p;
							i2 = i;
						}
					}

					nodes.push_back(Node{pop(wraps[i1]) + pop(wraps[i2]), childIndex(wraps[i1]), childIndex(wraps[i2])});
					const int32_t lo = i1 < i2 ? i1 : i2;
					const int32_t hi = i1 < i2 ? i2 : i1;
					wraps[lo] = Wrap{false, static_cast<int32_t>(nodes.size() - 1)};
					// Torque compares the second index, not the larger one, here.
					if (i2 != static_cast<int32_t>(wraps.size()) - 1)
						wraps[hi] = wraps.back();
					wraps.pop_back();
				}

				nodes[0] = nodes[wraps[0].index];
				generate(0, Code{});
			}

			void generate(int32_t index, Code code)
			{
				if (index < 0)
				{
					codes[-(index + 1)] = code;
					return;
				}
				const Node node = nodes[index];
				Code left = code;
				left.numBits++;
				Code right = code;
				right.bits |= (1u << code.numBits);
				right.numBits++;
				generate(node.index0, left);
				generate(node.index1, right);
			}
		};

		const Tree& tree()
		{
			static const Tree t;
			return t;
		}
	} // namespace

	size_t huffmanBits(std::string_view text)
	{
		size_t bits = 0;
		for (unsigned char c : text)
			bits += static_cast<size_t>(tree().codes[c].numBits);
		return bits;
	}

	void BitWriter::writeBit(bool value)
	{
		const size_t byte = m_bitCount >> 3;
		if (byte >= m_data.size())
			m_data.push_back(0);
		if (value)
			m_data[byte] |= static_cast<uint8_t>(1u << (m_bitCount & 7));
		++m_bitCount;
	}

	bool BitWriter::writeFlag(bool value)
	{
		writeBit(value);
		return value;
	}

	void BitWriter::writeInt(uint32_t value, int bits)
	{
		for (int i = 0; i < bits; ++i)
			writeBit(((value >> i) & 1u) != 0);
	}

	void BitWriter::writeF32(float value)
	{
		uint32_t raw;
		std::memcpy(&raw, &value, sizeof(raw));
		writeInt(raw, 32);
	}

	void BitWriter::writeBytes(std::span<const uint8_t> bytes)
	{
		for (auto b : bytes)
			writeInt(b, 8);
	}

	void BitWriter::writeBytes(std::string_view bytes)
	{
		for (unsigned char b : bytes)
			writeInt(b, 8);
	}

	void BitWriter::writeBits(const BitWriter& other)
	{
		for (size_t i = 0; i < other.m_bitCount; ++i)
			writeBit(((other.m_data[i >> 3] >> (i & 7)) & 1u) != 0);
	}

	void BitWriter::writeString(std::string_view text)
	{
		if (text.size() > 255)
			text = text.substr(0, 255);

		const auto bits = huffmanBits(text);
		if (bits >= text.size() * 8)
		{
			writeFlag(false);
			writeInt(static_cast<uint32_t>(text.size()), 8);
			writeBytes(text);
			return;
		}

		writeFlag(true);
		writeInt(static_cast<uint32_t>(text.size()), 8);
		for (unsigned char c : text)
		{
			const auto& code = tree().codes[c];
			writeInt(code.bits, code.numBits);
		}
	}

	void BitWriter::patchInt(size_t bitPos, uint32_t value, int bits)
	{
		for (int i = 0; i < bits; ++i)
		{
			const size_t pos = bitPos + static_cast<size_t>(i);
			const auto mask = static_cast<uint8_t>(1u << (pos & 7));
			if (((value >> i) & 1u) != 0)
				m_data[pos >> 3] |= mask;
			else
				m_data[pos >> 3] &= static_cast<uint8_t>(~mask);
		}
	}

	bool BitReader::readFlag()
	{
		if (m_bitPos >= m_bitSize)
		{
			m_overrun = true;
			return false;
		}
		const bool bit = ((m_data[m_bitPos >> 3] >> (m_bitPos & 7)) & 1u) != 0;
		++m_bitPos;
		return bit;
	}

	uint32_t BitReader::readInt(int bits)
	{
		uint32_t value = 0;
		for (int i = 0; i < bits; ++i)
		{
			if (readFlag())
				value |= (1u << i);
		}
		return value;
	}

	float BitReader::readF32()
	{
		const uint32_t raw = readInt(32);
		float value;
		std::memcpy(&value, &raw, sizeof(value));
		return value;
	}

	std::string BitReader::readBytes(size_t count)
	{
		std::string out;
		out.reserve(count);
		for (size_t i = 0; i < count && !m_overrun; ++i)
			out.push_back(static_cast<char>(readInt(8)));
		return out;
	}

	std::string BitReader::readString()
	{
		const bool huffman = readFlag();
		const auto length = readInt(8);
		if (!huffman)
			return readBytes(length);

		const auto& nodes = tree().nodes;
		std::string out;
		out.reserve(length);
		for (uint32_t i = 0; i < length && !m_overrun; ++i)
		{
			int32_t index = 0;
			while (index >= 0 && !m_overrun)
				index = readFlag() ? nodes[index].index1 : nodes[index].index0;
			if (index < 0)
				out.push_back(static_cast<char>(-(index + 1)));
		}
		return out;
	}

} // namespace graal3d
