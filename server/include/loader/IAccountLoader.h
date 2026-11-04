// Modified by the Seihai project, 2026-11-04: see CHANGES-SEIHAI.md.
#ifndef IACCOUNTLOADER_H
#define IACCOUNTLOADER_H

#include <string>
#include <string_view>
#include <vector>

#include <Account.h>
#include <object/Player.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

class IAccountLoader
{
public:
	virtual ~IAccountLoader() = default;

public:
	virtual bool loadAccount(std::string_view accountName, Account& account) = 0;
	virtual bool loadAccount(std::string_view accountName, Player& player) = 0;
	// Seihai: evenIfLoadOnly writes a load-only account too, for an RC that edits it.
	virtual bool saveAccount(const Account& account, bool evenIfLoadOnly = false) = 0;

public:
	[[nodiscard]] virtual bool checkSearchConditions(std::string_view account, const std::vector<std::string>& searches) const = 0;
};

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal

#endif // IACCOUNTLOADER_H
