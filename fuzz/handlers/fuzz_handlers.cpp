// Seihai project, 2026-11-19, 2026-11-20: see CHANGES-SEIHAI.md.
//
// This file is part of the Seihai fork of GServer-v2 and is released under the
// GNU General Public License version 3 (see LICENSE.md).
//
// libFuzzer target over the real client packet handlers (#87). Where
// fuzz_process_buffer stops at the framing with a stub handler, this one feeds
// decoded packets to PlayerClient::handlePacket on a Server with no socket, as
// Catch_tests/ShortPackets does for empty bodies. Built from the main project with
// -DFUZZ_HANDLERS=ON (Clang, with SANITIZE=ON); see fuzz/README.md.
//
// RC and NC handlers too: two more picks of the first byte make the player a
// PlayerRC or PlayerNC with every admin right (but no folder rights, so the file
// browser stays inside the scratch world). Game clients stand in a small level.
//
// Input: one byte picking the client type and version, then records of
//   [id byte] [body length byte] [body]
// A record that runs past the end takes what is left. Ids are taken mod 224;
// RAWDATA (it sizes the next packet, which has no meaning here) is skipped.

#include "harness.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	runFuzzInput(data, size);
	return 0;
}
