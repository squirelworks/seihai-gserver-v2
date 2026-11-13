// Modified by the Seihai project, 2026-10-31, 2026-11-12, 2026-11-13: see CHANGES-SEIHAI.md.
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <zlib.h>

#include <BabyDI.h>
#include <CString.h>
#include <IEnums.h>

#include <Server.h>
#include <filesystem/File.h>
#include <filesystem/FileSystemTypes.h>
#include <level/LevelItem.h>
#include <npcserver/NPCServer.h>
#include <object/NPC.h>
#include <object/Player.h>
#include <object/Weapon.h>
#include <scripting/Script.h>
#include <scripting/ScriptClass.h>
#include <scripting/ScriptContainers.h>
#include <scripting/ScriptTypes.h>
#include <utilities/CommonTypes.h>
#include <utilities/Log.h>
#include <utilities/ScriptCacheDes.h>
#include <utilities/StringUtils.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

Weapon::Weapon(const LevelItemType itemType)
	: name(LevelItem::getItemName(itemType)), modTime(clock::now()), m_weaponDefault(itemType)
{
	m_server = BabyDI::Get<Server>();
	assert(m_server != nullptr);
}

Weapon::Weapon(const std::string_view name, const std::string_view image, const std::string_view script)
	: name(name), modTime(clock::now()), m_weaponDefault(LevelItemType::INVALID)
{
	m_server = BabyDI::Get<Server>();
	assert(m_server != nullptr);
	updateWeapon(image, script);
}

//----------------------------

std::shared_ptr<Weapon> Weapon::loadWeapon(const std::filesystem::path& fileName)
{
	// Calculated file name.
	// Non-alphanumeric characters are encoded as %000.

	const auto server = BabyDI::Get<Server>();
	const auto file = server->getFileSystemServer().openi(fs::FileCategory::WEAPON, fileName.string());
	if (file == nullptr || !file->opened())
		return nullptr;

	if (const auto header = file->readLine(); header != "GRAWP001")
		return nullptr;

	const auto weaponName = file->readConfigLine("REALNAME", " "sv);
	auto weaponImage = file->readConfigLine("IMAGE", " "sv);
	const auto weaponScript = file->readConfigSection("SCRIPT", "SCRIPTEND");
	file->close();

	// Valid Weapon Name?
	if (!weaponName.has_value() || weaponName->empty())
		return nullptr;

	// Fix weapon image.
	if (weaponImage == "-"sv)
		weaponImage.reset();

	// Create the weapon.
	auto weapon = std::make_shared<Weapon>(weaponName.value(), weaponImage.value_or(""s), weaponScript.value_or(""s));

	// Check for script limits.
	if (const auto& script = weapon->getScript(); script.getClientByteCode().empty() && script.getClientSide().length() > 0x705F)
		log::printLine(log::server, "WARNING: Clientside script of weapon ({}) exceeds the limit of 28767 bytes.", *weaponName);

	// Set the mod time to the file mod time.
	weapon->modTime = fs::getFileModTime(file->filePath());

	// Check if we need to rename the file.
	const auto expectedFileName = fs::getHTMLEscapedFileName(std::format("weapon{}.txt", weapon->name)).string();
	const auto currentFileName = fs::getANSIFileName(fileName);
	if (expectedFileName != currentFileName)
	{
		if (const auto fileData = server->getFileSystemServer().infoi(fs::FileCategory::WEAPON, currentFileName); fileData != nullptr)
		{
			auto indent = log::server.indent();
			if (server->getFileSystemServer().rename(*fileData, expectedFileName))
				log::printLine(log::server, "Renamed weapon file [{}] to [{}]", currentFileName, expectedFileName);
			else
				log::printLine(log::server, "** Failed to rename weapon file [{}] to [{}]", currentFileName, expectedFileName);
		}
	}

	return weapon;
}

//----------------------------

bool Weapon::saveWeapon()
{
	// Don't save default weapons / empty weapons
	if (this->isDefault() || name.empty())
		return false;

	// If the bytecode filename is set, the weapon is treated as read-only so it can't be saved
	//if (!m_bytecodeFile.empty())
	//	return false;

	const auto fileName = fs::getHTMLEscapedFileName(std::format("weapon{}.txt", name));
	const auto file = m_server->getFileSystemServer().openiForWriting(fs::FileCategory::WEAPON, fileName, true);
	if (!file)
		return false;

	// Write the file.
	file->clear();
	file->writeLine("GRAWP001");
	file->writeConfigLine("REALNAME"sv, name);
	file->writeConfigLine("IMAGE"sv, image);

	// Write the script.
	const auto& originalSource = m_script.getOriginalSource();
	if (!originalSource.empty())
		file->writeConfigSection("SCRIPT", originalSource, "SCRIPTEND");

	file->close();

	calculateHeaderChecksum();

	// Queue the created event.
	scripting.events.addEvent(ScriptEventType::CREATED, source::FromServer());

	return true;
}

Weapon& Weapon::updateWeapon(const std::string_view newImage, const std::string_view newScript)
{
	setJoinedClasses("");
	m_script = Script{name, ScriptType::WEAPON, newScript};
	image = newImage;
	modTime = clock::now();

	calculateHeaderChecksum();

	// Queue the created event.
	scripting.events.addEvent(ScriptEventType::CREATED, source::FromServer());

	return *this;
}

//----------------------------

void Weapon::registerWeaponWithPlayer(const std::shared_ptr<Player>& player) const
{
	if (isDefault())
	{
		player->sendPacket(CString() >> (char)PLO_DEFAULTWEAPON >> (char)m_weaponDefault);
		return;
	}

	CString weaponPacket;
	weaponPacket >> (char)PLO_NPCWEAPONADD >> (char)name.length() << name >> (char)NPCProp::IMAGE >> (char)image.length() << image;

	// Classic weapons.
	if (m_script.getClientByteCode().empty() && player->getVersion() <= CLVER_5_07)
	{
		// Seihai: without an NPC-server nothing turned the weapon file's newlines into \xa7.
		const std::string script = gs1WireText(getClientSideScript());
		weaponPacket >> (char)NPCProp::SCRIPT >> (short)script.length() << script;
		player->sendPacket(weaponPacket);
	}
	// If we have bytecode, send the weapon headers.
	else if (m_server->Generation == ServerGeneration::MODERN)
	{
		if (const auto classes = getJoinedClassesList(); !classes.empty())
			weaponPacket >> (char)NPCProp::CLASS >> (short)classes.length() << classes;

		// Send the weapon.
		player->sendPacket(weaponPacket);

		// Tell the client the load the script.
		// Seihai: only when there is bytecode to load. A GS1-only weapon has none, so the header carried the CRC of
		// nothing (0); 6.03 then skips its cache check, asks for the script with PLI_UPDATESCRIPT, and
		// sendByteCodeToPlayer has nothing to send (#23, lead 1).
		if (!m_headerWithCRC.empty() && !m_script.getClientByteCode().empty())
		{
			if (player->getVersion() >= CLVER_5_07)
				player->sendPacket(CString() >> (char)PLO_LOADSCRIPT << m_headerWithCRC);
			// Seihai (#26): 193 carries the CRC of the plain bytecode, not the GRESCR01 one, and only 4.04r2 and later
			// handle it. Older builds get the bytecode itself (132), as they would after a cache miss.
			else if (player->getVersion() >= CLVER_4_042)
				player->sendPacket(getWeaponHashPacket());
			else sendByteCodeToPlayer(player);
		}
	}
}

void Weapon::sendByteCodeToPlayer(const std::shared_ptr<Player>& player) const
{
	// Send the bytecode.
	if (const auto& bytecode = m_script.getClientByteCode(); !bytecode.empty())
	{
		const auto bytecodePtr = reinterpret_cast<const char*>(bytecode.data());
		const std::string_view bytecodeView(bytecodePtr, bytecode.size());

		if (player->getVersion() >= CLVER_5_07)
			player->sendRawPacket(CString() >> (char)PLO_WEAPONBYTECODE2 >> (short)m_header.length() << m_header << bytecodeView);
		else player->sendRawPacket(CString() >> (char)PLO_WEAPONBYTECODE >> (char)name.length() << name << bytecodeView);
	}
}

//----------------------------

std::string Weapon::getJoinedClassesList() const
{
	bool hasExpired = false;
	std::string result;
	for (const auto& classPtr : m_joinedClasses | std::views::values)
	{
		if (auto scriptClass = classPtr.lock(); scriptClass != nullptr)
		{
			result += scriptClass->name;
			result += ',';
		}
		else hasExpired = true;
	}
	// A weapon with no joined classes has an empty list: don't trim the trailing comma then.
	if (!result.empty())
		result.pop_back();

	// If we have expired, clear them out.
	if (hasExpired)
	{
		std::erase_if(m_joinedClasses, [](const decltype(m_joinedClasses)::value_type& pair)
		{
			return pair.second.expired();
		});
	}

	return result;
}

void Weapon::setJoinedClasses(std::string_view classes)
{
	if (!m_server->hasNPCServer()) return;

	for (const auto& [handle, classPtr] : m_joinedClasses)
	{
		if (auto scriptClass = classPtr.lock(); scriptClass != nullptr)
			scriptClass->onScriptModified.unsubscribe(handle);
	}

	m_joinedClasses.clear();

	while (!classes.empty())
	{
		auto className = string::extractLine(classes, ',');
		if (className.empty())
			continue;

		className = string::trim(className);
		if (const auto scriptClass = m_server->getNPCServer()->getClass(className); scriptClass != nullptr)
		{
			auto handle = scriptClass->onScriptModified.subscribe(std::bind(&Weapon::updateScriptClass, this, std::placeholders::_1));
			m_joinedClasses.emplace_back(handle, scriptClass);
			m_server->updateWeaponForPlayers(this);
		}
	}
}

void Weapon::joinClass(const std::string_view className)
{
	const auto it = std::ranges::find_if(m_joinedClasses, [&className](const decltype(m_joinedClasses)::value_type& kvp)
	{
		return kvp.second.lock()->name == className;
	});
	if (it != m_joinedClasses.end())
		return;

	if (!m_server->hasNPCServer())
		return;

	if (const auto scriptClass = m_server->getNPCServer()->getClass(className); scriptClass != nullptr)
	{
		auto handle = scriptClass->onScriptModified.subscribe(std::bind(&Weapon::updateScriptClass, this, std::placeholders::_1));
		m_joinedClasses.emplace_back(handle, scriptClass);
		m_server->updateWeaponForPlayers(this);
	}
	else
	{
		log::printLine(log::npc, "Error: Weapon '{}' tried to join class '{}', but it does not exist.", name, className);
	}
}

void Weapon::leaveClass(const std::string_view className)
{
	const auto it = std::ranges::find_if(m_joinedClasses, [&className](const decltype(m_joinedClasses)::value_type& kvp)
	{
		return kvp.second.lock()->name == className;
	});
	if (it == m_joinedClasses.end())
		return;

	if (!m_server->hasNPCServer())
		return;

	if (const auto scriptClass = it->second.lock(); scriptClass != nullptr)
		scriptClass->onScriptModified.unsubscribe(it->first);

	m_joinedClasses.erase(it);
	m_server->updateWeaponForPlayers(this);
}

std::string Weapon::gs1WireText(const std::string_view script)
{
	std::string out;
	out.reserve(script.size());
	for (size_t i = 0; i < script.size(); ++i)
	{
		const char c = script[i];
		if (c == '\r')
		{
			// \r\n and a lone \r are one line break each.
			if (i + 1 < script.size() && script[i + 1] == '\n')
				++i;
			out += '\xa7';
		}
		else if (c == '\n')
			out += '\xa7';
		else
			out += c;
	}
	return out;
}

std::string Weapon::getClientSideScript() const
{
	std::string result{m_script.getClientSide()};
	for (const auto& classPtr : m_joinedClasses | std::views::values)
	{
		if (auto scriptClass = classPtr.lock(); scriptClass != nullptr)
		{
			if (const auto& clientSide = scriptClass->getScript().getClientSide(); !clientSide.empty())
			{
				result += '\xa7';
				result += clientSide;
			}
		}
	}
	return result;
}

void Weapon::calculateHeaderChecksum()
{
	const auto& script = m_script.getOriginalSource();
	const auto& bytecode = m_script.getClientByteCode();

	// Set the cryptographic key to be the script's hash.
	// This will become the DES encryption key the client will use the encrypt the bytecode.
	constexpr string::string_hash hash{};
	auto scriptHash = static_cast<uint64_t>(hash(script));

	// Package the key into two GYBTE5's.
	const auto hashBytes = reinterpret_cast<uint32_t*>(&scriptHash);
	const CString key = CString() >> (long long)(hashBytes[0]) >> (long long)(hashBytes[1]);
	m_desKey = key.toString();

	// CRC32 checksum the bytecode as the client stores it.
	// Seihai (#24): 5.x/6.x clients cache the bytecode DES-encrypted with this key (GRESCR01) and compare the
	// CRC32 of that stored body with this field. The CRC of the plain bytecode never matched, so every login
	// asked for every weapon again with PLI_UPDATESCRIPT (real 6.03r7, lab run 2026-11-12).
	std::array<uint8_t, 8> keyBytes{};
	for (size_t i = 0; i < 4; ++i)
	{
		keyBytes[i] = static_cast<uint8_t>(hashBytes[0] >> (8 * i));
		keyBytes[4 + i] = static_cast<uint8_t>(hashBytes[1] >> (8 * i));
	}
	const auto stored = seihai::scriptCacheEncrypt(std::string_view{reinterpret_cast<const char*>(bytecode.data()), bytecode.size()}, keyBytes);
	m_checksum = crc32(0L, Z_NULL, 0);
	m_checksum = crc32(m_checksum, stored.data(), static_cast<uInt>(stored.size()));

	// Seihai (#26): 4.04r2 to 4.20r8 cache a weapon as the raw bytes of its PLO_WEAPONBYTECODE (132), in
	// weblevels/weapons/<name>.code, and 193 compares the zlib CRC32 of that file with its GINT5
	// (Graal_analysis protocol/unknown-ids-2026-11-06.md, PLO 132 and PLO 193).
	m_plainChecksum = crc32(0L, Z_NULL, 0);
	m_plainChecksum = crc32(m_plainChecksum, bytecode.data(), static_cast<uInt>(bytecode.size()));

	// Create the header.
	// [GBYTE2 length_header_and_bytecode]
	// [STRING type,name,[0/1 save_to_disk],[GBYTE[10] desKey]]
	std::vector<std::string> headerParts =
	{
		"weapon",
		name,
		"1",
		m_desKey
	};
	m_header = string::toCSV(headerParts);

	// Header with CRC32.
	// Seihai: the CRC is a GINT5, like the key fields, the class header's CRC (ScriptClass) and PLO_WEAPONHASH.
	// CString's long long constructor writes decimal text ("847531159"), which no GINT5 reader decodes to the CRC.
	const CString crc32 = CString() >> static_cast<long long>(m_checksum);
	headerParts.push_back(crc32.toString());
	m_headerWithCRC = string::toCSV(headerParts);
}

CString Weapon::getWeaponHashPacket() const
{
	return CString() >> (char)PLO_WEAPONHASH >> (long long)m_plainChecksum << name;
}

void Weapon::updateScriptClass(ScriptClass* scriptClass) const
{
	m_server->updateWeaponForPlayers(this);
}

//----------------------------

void Weapon::executeEvents(ScriptEventQueue& events, const ScriptObject& source) const
{
	if (events.queue().empty())
		return;

	m_script.executeEvents(events, source);

	for (auto& scriptClassPtr : m_joinedClasses | std::views::values)
	{
		if (auto scriptClass = scriptClassPtr.lock(); scriptClass != nullptr)
			scriptClass->getScript().executeEvents(events, source);
	}

	events.queue().clear();
}

//----------------------------

ScriptObject source::FromWeapon(const WeaponPtr& weapon)
{
	size_t hash = string::string_hash{}(weapon->name);
	return std::make_pair(hash, ScriptObjectType::WEAPON);
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal
