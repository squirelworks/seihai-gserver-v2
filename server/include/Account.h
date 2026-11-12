// Modified by the Seihai project, 2026-11-04, 2026-11-12: see CHANGES-SEIHAI.md.
#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <string_view>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <object/Character.h>
#include <scripting/ScriptContainers.h>
#include <utilities/Extents.h>
#include <utilities/FilePermissions.h>
#include <utilities/StringUtils.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

class Player;

enum PlayerPermissions
{
	PLPERM_WARPTO = 0x00001,
	PLPERM_WARPTOPLAYER = 0x00002,
	PLPERM_SUMMON = 0x00004,
	PLPERM_UPDATELEVEL = 0x00008,
	PLPERM_DISCONNECT = 0x00010,
	PLPERM_VIEWATTRIBUTES = 0x00020,
	PLPERM_SETATTRIBUTES = 0x00040,
	PLPERM_SETSELFATTRIBUTES = 0x00080,
	PLPERM_RESETATTRIBUTES = 0x00100,
	PLPERM_ADMINMSG = 0x00200,
	PLPERM_SETRIGHTS = 0x00400,
	PLPERM_BAN = 0x00800,
	PLPERM_SETCOMMENTS = 0x01000,
	PLPERM_INVISIBLE = 0x02000,
	PLPERM_MODIFYSTAFFACCOUNT = 0x04000,
	PLPERM_SETSERVERFLAGS = 0x08000,
	PLPERM_SETSERVEROPTIONS = 0x10000,
	PLPERM_SETFOLDEROPTIONS = 0x20000,
	PLPERM_SETFOLDERRIGHTS = 0x40000,
	PLPERM_NPCCONTROL = 0x80000,

	PLPERM_ANYRIGHT = 0xFFFFFF
};

struct SavedChest
{
	int8_t x;
	int8_t y;
	std::string level;
};

struct Account
{
	std::string name;
	std::string communityName;
	std::string level;
	Character character;
	uint8_t maxHitpoints = 3;
	uint8_t status = 20;
	uint16_t apCounter = 0;
	uint32_t onlineSeconds = 0;
	std::string ipAddress;
	std::string platform{"win"};
	uint16_t codePage = 1252;
	std::string language{"English"};
	std::string groupName;
	uint32_t kills = 0;
	uint32_t deaths = 0;
	float eloRating = 1500.0f;
	float eloDeviation = 350.0f;
	std::chrono::system_clock::time_point lastSparTime;
	std::array<std::string, 30> ganiAttributes;
	std::vector<std::string> weapons;
	std::unordered_multimap<std::string, LocalWholeTilePosition> savedChests;
	GameVariableStore variables;
	bool banned = false;
	std::string banReason;
	std::string banLength;
	std::string comments;
	std::string email;
	uint32_t adminRights = 0;
	std::vector<std::string> adminIpRange;
	bool loadOnly = false;
	// Seihai: the RC's Edit Account fields, kept so its readback shows what was set.
	uint8_t adminLevel = 0;
	std::string adminWorlds;
	FilePermissions folderRights;
	std::vector<std::string> folderList;
	std::string lastFolderAccessed;

	[[a::inline]] bool hasRight(uint32_t right) const;

	/// @brief Whether a client may log in with this account name.
	/// Seihai: in localhost mode no listserver vets the name, and it goes straight into accounts/<name>.txt and into
	/// comma-separated lists (staff, RC lists, flags). GwenTea2 lab 2026-11-12: a 1.39 logged in as
	/// "seihaitest,lab420,fresh2x139". Refused: an empty name, a comma, a path separator, "..", and control characters.
	[[nodiscard]] [[a::inline]] static bool isLoginNameAllowed(std::string_view name) noexcept;
	[[a::inline]] bool hasChest(std::string_view levelName, const LocalWholeTilePosition& position) const;
	[[a::inline]] bool hasWeapon(std::string_view weapon) const;
};

inline bool Account::hasRight(const uint32_t right) const
{
	return (adminRights & right);
}

inline bool Account::isLoginNameAllowed(const std::string_view name) noexcept
{
	if (name.empty() || name.find("..") != std::string_view::npos)
		return false;
	return std::ranges::none_of(name, [](const char c)
	{
		return c == ',' || c == '/' || c == '\\' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F;
	});
}

inline bool Account::hasChest(const std::string_view levelName, const LocalWholeTilePosition& position) const
{
	auto [start, end] = savedChests.equal_range(levelName.data());
	for (auto& i = start; i != end; ++i)
	{
		if (i->second == position)
			return true;
	}
	return false;
}

inline bool Account::hasWeapon(const std::string_view weapon) const
{
	return std::ranges::find_if(weapons, [&weapon](const auto& w) { return string::equalsi(w, weapon); }) != std::ranges::end(weapons);
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal

#endif // ACCOUNT_H
