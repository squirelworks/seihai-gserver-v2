// Modified by the Seihai project, 2026-11-16: see CHANGES-SEIHAI.md.
#ifndef SEIHAI_SCRIPT_PACKET_GUARDS_H
#define SEIHAI_SCRIPT_PACKET_GUARDS_H

#include <IUtil.h>

// Per-version script packet guards (Graal_analysis protocol/script-packet-guards.md).
// Gate on the client's version id the same way other Seihai profile cuts do
// (RC / seihai-core plo sets): never send classic bytecode ids to builds that
// dropped them, never send the modern cache path to pure 4.x.
//
// Classic path (193 / 132 / 133): 4.04r2 through 5.12 (incl. Mac 5.12).
// Modern path (197 / 140): from 6.00 beta 1 / 5.24 onward (first builds that
// dropped 132/133/193). Prefer classic while both would be available.
namespace preagonal::scriptPackets
{
	inline bool usesClassicScriptBytecode(const int versionId)
	{
		return versionId >= CLVER_4_042 && versionId < CLVER_6_00_BETA1;
	}

	inline bool usesModernScriptCache(const int versionId)
	{
		return versionId >= CLVER_6_00_BETA1;
	}
}

#endif
