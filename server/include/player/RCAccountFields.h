// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
// New file: the Edit Account fields an RC sends (PLI_RC_ACCOUNTSET, PLI_RC_ACCOUNTADD)
// and gets back (PLO_RC_ACCOUNTGET), kept apart from PlayerRC so they can be tested.
#ifndef RCACCOUNTFIELDS_H
#define RCACCOUNTFIELDS_H

#include <cstdint>
#include <string>

#include <CString.h>

#include <Account.h>

namespace preagonal
{
	/// @brief What an RC's Edit Account or Add Account dialog sends after the account name.
	/// Layout from a real 2.22 RC (lab capture rc-capture-2026-11-04b, "Capture 3"):
	/// GSTRING password, GSTRING e-mail, GCHAR banned, GCHAR load only, GCHAR admin level,
	/// GSTRING admin worlds, GSTRING ban reason. The RC does not send the ban time.
	struct RCAccountFields
	{
		std::string password;
		std::string email;
		bool banned = false;
		bool loadOnly = false;
		uint8_t adminLevel = 0;
		std::string adminWorlds;
		std::string banReason;
	};

	/// @brief Reads the fields that follow the account name in a PLI_RC_ACCOUNTSET or PLI_RC_ACCOUNTADD.
	inline RCAccountFields readRCAccountFields(CString& packet)
	{
		RCAccountFields fields;
		fields.password = packet.readChars(packet.readGUChar()).toString();
		fields.email = packet.readChars(packet.readGUChar()).toString();
		fields.banned = packet.readGUChar() != 0;
		fields.loadOnly = packet.readGUChar() != 0;
		fields.adminLevel = packet.readGUChar();
		fields.adminWorlds = packet.readChars(packet.readGUChar()).toString();
		fields.banReason = packet.readChars(packet.readGUChar()).toString();
		return fields;
	}

	/// @brief Reads a PLO_RC_ACCOUNTGET payload after the account name, skipping the ban time.
	inline RCAccountFields readRCAccountFieldsWithBanTime(CString& packet)
	{
		RCAccountFields fields;
		fields.password = packet.readChars(packet.readGUChar()).toString();
		fields.email = packet.readChars(packet.readGUChar()).toString();
		fields.banned = packet.readGUChar() != 0;
		fields.loadOnly = packet.readGUChar() != 0;
		fields.adminLevel = packet.readGUChar();
		fields.adminWorlds = packet.readChars(packet.readGUChar()).toString();
		packet.readChars(packet.readGUChar()); // ban time
		fields.banReason = packet.readChars(packet.readGUChar()).toString();
		return fields;
	}

	/// @brief Stores what an RC set on an account. The ban is only taken when the RC may ban.
	inline void applyRCAccountFields(Account& target, const RCAccountFields& fields, bool mayBan)
	{
		// Upstream kept the e-mail and load only here but dropped the admin level and
		// the admin worlds, so a readback always showed 0 and "main".
		target.email = fields.email;
		target.loadOnly = fields.loadOnly;
		target.adminLevel = fields.adminLevel;
		target.adminWorlds = fields.adminWorlds;
		if (mayBan)
		{
			target.banned = fields.banned;
			target.banReason = fields.banReason;
		}
	}

	/// @brief The PLO_RC_ACCOUNTGET payload: the account and the fields above, with the ban
	/// time between the worlds and the reason. The password is never sent.
	inline CString rcAccountGetPayload(const std::string& name, const Account& source)
	{
		// An account with no admin worlds set reads back as "main", as before.
		const std::string worlds = source.adminWorlds.empty() ? std::string{"main"} : source.adminWorlds;
		CString out;
		out >> (char)name.length() << name
			>> (char)0
			>> (char)source.email.size() << source.email
			>> (char)(source.banned ? 1 : 0) >> (char)(source.loadOnly ? 1 : 0) >> (char)source.adminLevel
			>> (char)worlds.size() << worlds
			>> (char)source.banLength.size() << source.banLength
			>> (char)source.banReason.size() << source.banReason;
		return out;
	}
} // namespace preagonal

#endif // RCACCOUNTFIELDS_H
