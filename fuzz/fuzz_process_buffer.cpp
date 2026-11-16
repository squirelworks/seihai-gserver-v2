// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under
// the GNU General Public License version 3 or later (see LICENSE.md).
//
// libFuzzer target (#87): IPacketHandler::processBuffer — framing,
// per-generation decrypt/decompress, and newline packet split — with a
// stub handler so no socket or Server is required. Seeded from fixture
// corpora under fuzz/corpus/process_buffer/.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

using namespace std::string_view_literals;

#include <network/IPacketHandler.h>

using namespace preagonal;

namespace
{
	struct StubHandler final : IPacketHandler
	{
		HandlePacketResult handlePacket(std::optional<uint8_t> /*id*/, CString& /*packet*/) override
		{
			return HandlePacketResult::Handled;
		}
	};
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	if (size < 3 || size > 256 * 1024)
		return 0;

	const uint8_t key = data[0] & 0x7F;
	const uint32_t gen = data[1] % (ENCRYPT_GEN_6 + 1U);
	const Gen5Steps steps =
		(data[2] & 1) != 0 ? Gen5Steps::PerWholeGroup : Gen5Steps::PerStartedGroup;

	StubHandler handler;
	handler.Encryption.setGen(gen);
	handler.Encryption.setGen5Steps(steps);
	handler.Encryption.reset(key);
	handler.LoginParsed = (data[2] & 2) != 0;
	handler.PacketCount = handler.LoginParsed ? 1U : 0U;

	CString buffer;
	buffer.write(reinterpret_cast<const char*>(data + 3), static_cast<int>(size - 3));
	handler.processBuffer(buffer);
	return 0;
}
