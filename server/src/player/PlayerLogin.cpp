// Modified by the Seihai project, 2026-10-31, 2026-11-12: see CHANGES-SEIHAI.md.
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

#include <CSocket.h>

#include <CString.h>
#include <IEnums.h>

#include <Server.h>
#include <network/IPacketHandler.h>
#include <npcserver/PlayerNPCServer.h>
#include <object/Player.h>
#include <player/PlayerClient.h>
#include <player/PlayerClientOriginal.h>
#include <player/PlayerLogin.h>
#include <player/PlayerNC.h>
#include <player/PlayerRC.h>
#include <utilities/CommonTypes.h>
#include <utilities/Log.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal
{
///////////////////////////////////////////////////////////////////////////////

PlayerLogin::PlayerLogin(CSocket* pSocket, const PlayerID pId)
	: Player(pSocket, pId)
{
}

///////////////////////////////////////////////////////////////////////////////

bool PlayerLogin::onRecv()
{
	auto self = shared_from_this();
	(void)Player::onRecv();
	return PacketCount == 0;
}

///////////////////////////////////////////////////////////////////////////////

std::string_view PlayerLogin::whoAmI() const noexcept
{
	return m_playerSock != nullptr ? m_playerSock->getRemoteIp() : "(unknown)";
}

///////////////////////////////////////////////////////////////////////////////

HandlePacketResult PlayerLogin::handlePacket(std::optional<uint8_t> id, CString& packet)
{
	// TODO(websocket): Websocket stuff somewhere.
	if (msgLoginPacket(packet) == HandlePacketResult::Failed)
		disconnect();

	return HandlePacketResult::Handled;
}

HandlePacketResult PlayerLogin::msgLoginPacket(CString& pPacket)
{
	// Seihai: the frame after a SEI00001 hello is C2.
	if (m_secureLogin)
		return msgSecureSealed(pPacket);

	const auto type = (1 << pPacket.readGChar());
	if (type & PLTYPE_ANYCLIENT)
	{
		if (auto result = msgSecureHello(pPacket, type); result.has_value())
			return *result;
	}
	return createPlayer(pPacket, type, false);
}

std::optional<HandlePacketResult> PlayerLogin::msgSecureHello(CString& pPacket, const int type)
{
	const std::string_view line = pPacket.toStringView();

	// The token sits where the version token does: after the type, or after the type and
	// the key byte. Only 1.x clients (PLTYPE_CLIENT) send no key byte.
	size_t prefixLength = 2;
	if (type == PLTYPE_CLIENT && line.substr(1).starts_with(seclogin::TOKEN))
		prefixLength = 1;

	bool malformed = false;
	auto hello = seclogin::ClientHello::parse(line, prefixLength, malformed);
	if (!hello)
	{
		if (!malformed)
			return std::nullopt;	// A classic login.
		log::printLine(log::server, "New login from {}: malformed SEI00001 hello, closing.", whoAmI());
		return HandlePacketResult::Failed;
	}
	if (!hello->suffix.empty())
	{
		log::printLine(log::server, "New login from {}: SEI00001 hello with a suffix, closing.", whoAmI());
		return HandlePacketResult::Failed;
	}

	const auto* identity = m_server->getSecureLoginIdentity();
	if (identity == nullptr)
	{
		log::printLine(log::server, "New login from {}: SEI00001 (inner {}) refused, secure login is off.", whoAmI(), hello->inner);
		return refuseSecure("This server does not take secure logins.");
	}

	// The inner token is the client version: check it now, before the client sends its password.
	const int versionId = getVersionID(hello->inner);
	if (versionId == CLVER_UNKNOWN || !m_server->isClientVersionAllowed(versionId))
	{
		log::printLine(log::server, "New login from {}: SEI00001 (inner {}) refused, version not allowed.", whoAmI(), hello->inner);
		return refuseSecure(std::string("Your client version is not allowed on this server.\rAllowed: ") + m_server->getAllowedVersionString());
	}

	auto response = seclogin::respond(*hello, *identity);
	if (!response)
	{
		log::printLine(log::server, "New login from {}: SEI00001 key exchange failed, closing.", whoAmI());
		return HandlePacketResult::Failed;
	}

	// S1, in a plain zlib frame (our output codec is still the classic gen-3 one).
	sendPacket(CString() << response->line);
	m_fileQueue.sendCompress();

	m_secureLogin = PendingSecureLogin{std::string(line.substr(0, prefixLength)), hello->inner, response->opener};

	// Read the next frame as a login frame again, and stay registered for it.
	PacketCount = 0;
	LoginParsed = false;
	LoginLeftover.clear();
	return HandlePacketResult::Handled;
}

HandlePacketResult PlayerLogin::msgSecureSealed(CString& pPacket)
{
	const auto pending = std::move(*m_secureLogin);
	m_secureLogin.reset();

	const auto tail = pending.opener.open(pPacket.toStringView());
	if (!tail)
	{
		// Either an attack or a broken client: close with no answer.
		log::printLine(log::server, "New login from {}: SEI00001 sealed login did not open, closing.", whoAmI());
		return HandlePacketResult::Failed;
	}

	// The classic login line, as the inner build would have sent it.
	CString classic;
	classic << pending.prefix << pending.inner;
	classic.write(tail->data(), static_cast<int>(tail->size()));
	classic.setRead(0);
	const auto type = (1 << classic.readGChar());
	if ((type & PLTYPE_ANYCLIENT) == 0)
		return HandlePacketResult::Failed;
	return createPlayer(classic, type, true);
}

HandlePacketResult PlayerLogin::refuseSecure(const std::string_view message)
{
	// What a refused classic login gets: PLO_DISCMESSAGE in a plain zlib frame, then close.
	sendPacket(CString() >> (char)PLO_DISCMESSAGE << message);
	return HandlePacketResult::Failed;
}

HandlePacketResult PlayerLogin::createPlayer(CString& pPacket, const int type, const bool secure)
{
	// Create our appropriate player.
	std::shared_ptr<Player> player = nullptr;
	if (type & PLTYPE_ANYCLIENT)
	{
		if (type == PLTYPE_CLIENT)
			player = std::make_shared<PlayerClientOriginal>(m_playerSock, m_id);
		else player = std::make_shared<PlayerClient>(m_playerSock, m_id);
	}
	else if (type & PLTYPE_ANYRC)
		player = std::make_shared<PlayerRC>(m_playerSock, m_id);
	else if (type & PLTYPE_ANYNC)
		player = std::make_shared<PlayerNC>(m_playerSock, m_id);
	else if (type & PLTYPE_NPCSERVER)
		player = std::make_shared<PlayerNPCServer>(m_playerSock, m_id);
	else
	{
		log::printLine(log::server, "New login, but unknown player type: {}", type);
		return HandlePacketResult::Failed;
	}

	// Fix the type.
	player->setType(type);
	player->setSecureLogin(secure);

	// Update the new player's current packet state to match ours.
	player->PacketCount = 1;
	player->LoginParsed = true;
	player->setReceivedBuffer(m_recvBuffer);

	// Remove ourselves from the server.
	// We need to null our socket to avoid being passed data by the socket manager.
	const auto self = shared_from_this();
	m_server->swapPlayer(self, player);
	m_playerSock = nullptr;

	// Pass the login to the new player.
	pPacket.setRead(0);
	if (player != nullptr && !player->handleLogin(pPacket))
		player->disconnect();
	else if (player != nullptr && !LoginLeftover.isEmpty())
		player->processLoginLeftover(LoginLeftover);

	return HandlePacketResult::Handled;
}

///////////////////////////////////////////////////////////////////////////////
} // end namespace preagonal
