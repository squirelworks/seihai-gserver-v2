// Modified by the Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
#ifndef PLAYERLOGIN_H
#define PLAYERLOGIN_H

#include <cstdint>
#include <optional>
#include <string_view>

#include <CSocket.h>

#include <CString.h>

#include <network/IPacketHandler.h>
#include <object/Player.h>
#include <utilities/CommonTypes.h>
#include <utilities/SecureLogin.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

class PlayerLogin : public Player
{
public:
	PlayerLogin(CSocket* pSocket, PlayerID pId);
	~PlayerLogin() override = default;

public:
	bool onRecv() override;
	void onUnregister() override {}

protected:
	std::string_view whoAmI() const noexcept override;
	HandlePacketResult handlePacket(std::optional<uint8_t> id, CString& packet) override;

public:
	HandlePacketResult msgLoginPacket(CString& pPacket);

private:
	// Seihai: SEI00001 (seihai/documentation/secure-login.md). After C1 the connection
	// stays a PlayerLogin and waits for C2, which opens back into the classic login line.
	struct PendingSecureLogin
	{
		std::string prefix;	// Client type and key byte, as the classic line has them.
		std::string inner;	// The build's own version token.
		seclogin::Opener opener;
	};
	std::optional<PendingSecureLogin> m_secureLogin;

	std::optional<HandlePacketResult> msgSecureHello(CString& pPacket, int type);
	HandlePacketResult msgSecureSealed(CString& pPacket);
	HandlePacketResult refuseSecure(std::string_view message);
	HandlePacketResult createPlayer(CString& pPacket, int type, bool secure);
};

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal

#endif // PLAYERLOGIN_H
