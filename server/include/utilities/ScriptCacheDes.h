// Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
//
// The DES that 5.x and 6.x clients use for their GRESCR01 script cache (#24).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace seihai
{
	// DES-ECB encrypt `data` the way the client stores a script it was sent: whole 8-byte blocks only (a shorter
	// tail stays as it is), and each key byte taken with its bits reversed compared with standard DES.
	// `key` is the 8 key bytes: the two 32-bit key words of the PLO_LOADSCRIPT / PLO_WEAPONBYTECODE2 header,
	// each little-endian, first word first.
	std::vector<uint8_t> scriptCacheEncrypt(std::string_view data, const std::array<uint8_t, 8>& key);
}
