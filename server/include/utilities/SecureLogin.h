// Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
//
// The SEI00001 secure login (Seihai's own protocol, not part of any Graal build).
// Written from seihai/documentation/secure-login.md, revision 1, with libsodium.
//
//   C1  prefix "SEI00001" inner[8] b64(client_x25519_pub)[43] suffix
//   S1  "SEI00001" b64(server_x25519_pub)[43] b64(server_ed25519_pub)[43] b64(signature)[86]
//   C2  "SEI00001" b64(ChaCha20-Poly1305(key, nonce 0, aad th, u16le(len) || tail || zero padding))
//
// The client half (ClientHandshake) is here for the tests and any later C++ client.

#ifndef SEIHAI_SECURELOGIN_H
#define SEIHAI_SECURELOGIN_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::seclogin
{
///////////////////////////////////////////////////////////////////////////////

constexpr std::string_view TOKEN = "SEI00001";
constexpr size_t KEY_B64_LEN = 43;			// 32 bytes.
constexpr size_t SIGNATURE_B64_LEN = 86;	// 64 bytes.
constexpr size_t MAX_TAIL = 1024;
constexpr size_t PAD_TO = 64;

using Key32 = std::array<uint8_t, 32>;
using Signature = std::array<uint8_t, 64>;

/// Starts libsodium. Safe to call more than once; false if it cannot start.
bool init();

/// Base64, standard alphabet, no padding.
std::string base64Encode(std::span<const uint8_t> data);
/// Strict: no padding, no other characters, and the unused bits of the last character are zero.
std::optional<std::vector<uint8_t>> base64Decode(std::string_view text);

/// "SHA256:" + base64 (no padding) of SHA-256 of the key.
std::string fingerprint(const Key32& publicKey);

/// The server's long-term Ed25519 key.
class ServerIdentity
{
public:
	static std::optional<ServerIdentity> fromSeed(const Key32& seed);
	static std::optional<ServerIdentity> generate();

	/// Reads the seed file (one line of base64, no padding), or creates it (mode 0600 on Unix)
	/// when it does not exist. `created` tells which. nullopt and `error` on failure.
	static std::optional<ServerIdentity> loadOrCreate(const std::filesystem::path& path, bool& created, std::string& error);

	const Key32& seed() const { return m_seed; }
	const Key32& publicKey() const { return m_public; }
	std::string publicKeyBase64() const { return base64Encode(m_public); }
	std::string fingerprint() const { return seclogin::fingerprint(m_public); }

	Signature sign(std::span<const uint8_t> message) const;

private:
	Key32 m_seed{};
	Key32 m_public{};
	std::array<uint8_t, 64> m_secret{};
};

/// A parsed C1. `line` is the whole line without its '\n'.
struct ClientHello
{
	std::string line;
	size_t prefixLength = 0;
	std::string inner;
	Key32 clientPublic{};
	std::string suffix;

	/// nullopt when the line does not carry the token after `prefixLength` bytes.
	/// Sets `malformed` when it does but the rest is not a valid C1.
	static std::optional<ClientHello> parse(std::string_view line, size_t prefixLength, bool& malformed);
};

/// The server's half after S1: opens C2 once.
class Opener
{
public:
	Opener(const Key32& key, const Key32& transcript) : m_key(key), m_transcript(transcript) {}

	/// The tail, or nullopt if C2 does not open (bad token, base64, tag, length or padding).
	std::optional<std::string> open(std::string_view c2Line) const;

	const Key32& key() const { return m_key; }
	const Key32& transcript() const { return m_transcript; }

private:
	Key32 m_key;
	Key32 m_transcript;
};

struct ServerHello
{
	std::string line;	// S1 without '\n'.
	Opener opener;
};

/// Makes S1 for `hello` with a fresh X25519 key. nullopt if the shared secret is all zero.
std::optional<ServerHello> respond(const ClientHello& hello, const ServerIdentity& identity);
/// The same with a given X25519 secret (tests).
std::optional<ServerHello> respondWith(const ClientHello& hello, const ServerIdentity& identity, const Key32& ephemeralSecret);

/// Client half: C1, then check S1 and seal the tail.
class ClientHandshake
{
public:
	/// nullopt if `inner` is not 8 bytes.
	static std::optional<ClientHandshake> create(std::string_view prefix, std::string_view inner, std::string_view suffix = {});
	/// The same with a given X25519 secret (tests).
	static std::optional<ClientHandshake> createWith(std::string_view prefix, std::string_view inner, std::string_view suffix, const Key32& secret);

	/// C1 without '\n'.
	const std::string& hello() const { return m_hello; }

	struct Verified
	{
		Key32 serverKey;	// Ed25519, for pinning.
		Key32 key;
		Key32 transcript;

		/// C2 without '\n'. nullopt if the tail is over MAX_TAIL.
		std::optional<std::string> seal(std::string_view tail) const;
	};

	/// Checks S1 (shape and signature). Pinning is the caller's.
	std::optional<Verified> finish(std::string_view s1) const;

private:
	std::string m_hello;
	Key32 m_secret{};
	Key32 m_public{};
};

/// Low-level pieces, exposed for the tests.
Key32 sha256(std::span<const uint8_t> data);
Key32 hkdfSha256(const Key32& salt, std::span<const uint8_t> ikm, std::string_view info);
std::vector<uint8_t> serverHelloSigned(std::string_view c1Line, const Key32& serverX25519, const Key32& serverEd25519);

///////////////////////////////////////////////////////////////////////////////
} // namespace preagonal::seclogin

#endif // SEIHAI_SECURELOGIN_H
