// Modified by the Seihai project, 2026-11-12, 2026-11-16, 2026-11-25: see CHANGES-SEIHAI.md.
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <zlib.h>

#include <CString.h>
#include <IEnums.h>

#include <BabyDI.h>
#include <Server.h>
#include <misc/ScriptPacketGuards.h>
#include <object/Player.h>
#include <scripting/ScriptClass.h>
#include <utilities/ScriptCacheDes.h>
#include <utilities/StringUtils.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

ScriptClass::ScriptClass(const std::string_view className, const std::string_view classScript)
	: name(className), m_server(BabyDI::Get<Server>()), m_plainChecksum(0), m_encryptedChecksum(0)
{
	assert(m_server);
	setScript(classScript);
}

ScriptClass& ScriptClass::setScript(std::string_view classScript)
{
	m_script = {name, ScriptType::CLASS, classScript};
	generateScriptHeader();

	// Set the modification time to now.
	modTime = std::chrono::system_clock::now();

	// Send out events.
	onScriptModified.post(this);

	return *this;
}

void ScriptClass::generateScriptHeader()
{
	// Seihai: a class whose new script has no clientside bytecode keeps no header from its old one.
	m_plainChecksum = 0;
	m_encryptedChecksum = 0;
	m_desKey.clear();
	m_header.clear();
	m_headerWithCRC.clear();

	if (const auto& bytecode = m_script.getClientSideByteCode(); bytecode.empty())
		return;

	// Classic clients cache plain weapon bytecode under PLO_WEAPONHASH; classes use
	// PLO_CLASSBYTECODE on ask. Modern clients (5.24 / 6.00β1 on) cache the DES-encrypted
	// body under PLO_LOADSCRIPT, the same split weapons use.
	std::string desKey;
	m_script.calculateClientByteCodeChecksums(desKey, m_plainChecksum, m_encryptedChecksum);
	m_desKey = desKey;

	// Create the header.
	// [GBYTE2 length_header_and_bytecode]
	// [STRING type,name,[0/1 save_to_disk],[GBYTE[10] desKey]]
	std::vector<std::string> headerParts =
	{
		"class",
		name,
		"1",
		desKey
	};
	m_header = string::toCSV(headerParts);

	// Header with CRC32.
	const CString crc32 = CString() >> (long long)m_encryptedChecksum;
	headerParts.push_back(crc32.toString());
	m_headerWithCRC = string::toCSV(headerParts);
}

void ScriptClass::registerWithPlayer(const std::shared_ptr<Player>& player) const
{
	if (m_server->Generation != ServerGeneration::MODERN)
		return;

	// Matching CRC: modern clients get a LOADSCRIPT refresh. Classic clients
	// already hold the class (161 CRC matched) — stay silent. Never announce
	// a class with PLO_WEAPONHASH (193 is the weapon path only).
	if (m_headerWithCRC.empty())
		return;
	if (scriptPackets::usesModernScriptCache(player->getVersion()))
		player->sendPacket(CString() >> (char)PLO_LOADSCRIPT << m_headerWithCRC);
}

void ScriptClass::sendByteCodeToPlayer(const std::shared_ptr<Player>& player) const
{
	if (const auto& bytecode = m_script.getClientSideByteCode(); !bytecode.empty())
	{
		const auto bytecodePtr = reinterpret_cast<const char*>(bytecode.data());
		const std::string_view bytecodeView(bytecodePtr, bytecode.size());

		if (scriptPackets::usesModernScriptCache(player->getVersion()))
			player->sendRawPacket(CString() >> (char)PLO_HEADEREDBYTECODE >> (short)m_header.length() << m_header << bytecodeView);
		else if (scriptPackets::usesClassicScriptBytecode(player->getVersion()) || player->getVersion() >= CLVER_3_153)
			player->sendRawPacket(CString() >> (char)PLO_CLASSBYTECODE >> (char)name.length() << name << bytecodeView);
	}
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal
