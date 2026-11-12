// Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// The SEI00001 secure login, from seihai/documentation/secure-login.md (revision 1).

#include <algorithm>
#include <cstring>
#include <fstream>
#include <sstream>
#include <system_error>

#include <sodium.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <utilities/SecureLogin.h>

///////////////////////////////////////////////////////////////////////////////
namespace preagonal::seclogin
{
///////////////////////////////////////////////////////////////////////////////

namespace
{
	constexpr std::string_view SIGN_CONTEXT{"seihai-login-v1/server-hello\0", 29};
	constexpr std::string_view C2S_INFO = "seihai-login-v1/c2s key";
	constexpr std::string_view B64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::span<const uint8_t> bytes(std::string_view text)
	{
		return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
	}

	void append(std::vector<uint8_t>& out, std::span<const uint8_t> data)
	{
		out.insert(out.end(), data.begin(), data.end());
	}

	template <size_t N>
	std::optional<std::array<uint8_t, N>> decodeFixed(std::string_view text)
	{
		auto data = base64Decode(text);
		if (!data || data->size() != N)
			return std::nullopt;
		std::array<uint8_t, N> out{};
		std::copy(data->begin(), data->end(), out.begin());
		return out;
	}

	struct Derived
	{
		Key32 key;
		Key32 transcript;
	};

	// th = SHA-256(signed || signature); key = HKDF(salt = th, ikm = shared, info = c2s).
	Derived derive(std::span<const uint8_t> signedPart, const Signature& signature, const Key32& shared)
	{
		std::vector<uint8_t> transcript(signedPart.begin(), signedPart.end());
		append(transcript, signature);
		Derived out{};
		out.transcript = sha256(transcript);
		out.key = hkdfSha256(out.transcript, shared, C2S_INFO);
		return out;
	}

	// X25519; false when the result is all zero (a small-order peer key).
	bool x25519(Key32& shared, const Key32& secret, const Key32& peer)
	{
		if (crypto_scalarmult(shared.data(), secret.data(), peer.data()) != 0)
			return false;
		return sodium_is_zero(shared.data(), shared.size()) == 0;
	}

	constexpr Key32 ZERO_NONCE_SOURCE{};
	const uint8_t* zeroNonce()
	{
		static_assert(crypto_aead_chacha20poly1305_ietf_NPUBBYTES <= 32);
		return ZERO_NONCE_SOURCE.data();
	}
} // namespace

bool init()
{
	return sodium_init() >= 0;
}

std::string base64Encode(std::span<const uint8_t> data)
{
	std::string out;
	out.reserve((data.size() * 4 + 2) / 3);
	size_t i = 0;
	for (; i + 3 <= data.size(); i += 3)
	{
		const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8) | data[i + 2];
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
		out += B64[(v >> 6) & 63];
		out += B64[v & 63];
	}
	if (const size_t rest = data.size() - i; rest == 1)
	{
		const uint32_t v = uint32_t(data[i]) << 16;
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
	}
	else if (rest == 2)
	{
		const uint32_t v = (uint32_t(data[i]) << 16) | (uint32_t(data[i + 1]) << 8);
		out += B64[(v >> 18) & 63];
		out += B64[(v >> 12) & 63];
		out += B64[(v >> 6) & 63];
	}
	return out;
}

std::optional<std::vector<uint8_t>> base64Decode(std::string_view text)
{
	if (text.size() % 4 == 1)
		return std::nullopt;

	std::vector<uint8_t> out;
	out.reserve(text.size() * 3 / 4);
	uint32_t acc = 0;
	int bits = 0;
	for (const char c : text)
	{
		const auto pos = B64.find(c);
		if (pos == std::string_view::npos)
			return std::nullopt;
		acc = (acc << 6) | static_cast<uint32_t>(pos);
		bits += 6;
		if (bits >= 8)
		{
			bits -= 8;
			out.push_back(static_cast<uint8_t>((acc >> bits) & 0xFF));
		}
	}
	// The bits left over in the last character must be zero (one encoding per value).
	if (bits > 0 && (acc & ((1u << bits) - 1)) != 0)
		return std::nullopt;
	return out;
}

Key32 sha256(std::span<const uint8_t> data)
{
	Key32 out{};
	crypto_hash_sha256(out.data(), data.data(), data.size());
	return out;
}

Key32 hkdfSha256(const Key32& salt, std::span<const uint8_t> ikm, std::string_view info)
{
	// RFC 5869 with L = 32: PRK = HMAC(salt, ikm); OKM = HMAC(PRK, info || 0x01).
	Key32 prk{};
	crypto_auth_hmacsha256_state state;
	crypto_auth_hmacsha256_init(&state, salt.data(), salt.size());
	crypto_auth_hmacsha256_update(&state, ikm.data(), ikm.size());
	crypto_auth_hmacsha256_final(&state, prk.data());

	Key32 okm{};
	const uint8_t counter = 1;
	crypto_auth_hmacsha256_init(&state, prk.data(), prk.size());
	crypto_auth_hmacsha256_update(&state, reinterpret_cast<const uint8_t*>(info.data()), info.size());
	crypto_auth_hmacsha256_update(&state, &counter, 1);
	crypto_auth_hmacsha256_final(&state, okm.data());
	sodium_memzero(prk.data(), prk.size());
	sodium_memzero(&state, sizeof(state));
	return okm;
}

std::vector<uint8_t> serverHelloSigned(std::string_view c1Line, const Key32& serverX25519, const Key32& serverEd25519)
{
	std::vector<uint8_t> out;
	out.reserve(SIGN_CONTEXT.size() + 2 + c1Line.size() + 64);
	append(out, bytes(SIGN_CONTEXT));
	out.push_back(static_cast<uint8_t>(c1Line.size() & 0xFF));
	out.push_back(static_cast<uint8_t>((c1Line.size() >> 8) & 0xFF));
	append(out, bytes(c1Line));
	append(out, serverX25519);
	append(out, serverEd25519);
	return out;
}

std::string fingerprint(const Key32& publicKey)
{
	return "SHA256:" + base64Encode(sha256(publicKey));
}

///////////////////////////////////////////////////////////////////////////////

std::optional<ServerIdentity> ServerIdentity::fromSeed(const Key32& seed)
{
	if (!init())
		return std::nullopt;
	ServerIdentity id;
	id.m_seed = seed;
	if (crypto_sign_ed25519_seed_keypair(id.m_public.data(), id.m_secret.data(), seed.data()) != 0)
		return std::nullopt;
	return id;
}

std::optional<ServerIdentity> ServerIdentity::generate()
{
	if (!init())
		return std::nullopt;
	Key32 seed{};
	randombytes_buf(seed.data(), seed.size());
	auto id = fromSeed(seed);
	sodium_memzero(seed.data(), seed.size());
	return id;
}

std::optional<ServerIdentity> ServerIdentity::loadOrCreate(const std::filesystem::path& path, bool& created, std::string& error)
{
	created = false;
	std::error_code ec;
	if (std::filesystem::exists(path, ec))
	{
		std::ifstream in(path, std::ios::binary);
		if (!in)
		{
			error = "cannot read " + path.string();
			return std::nullopt;
		}
		std::stringstream buffer;
		buffer << in.rdbuf();
		std::string text = buffer.str();
		while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ' || text.back() == '\t'))
			text.pop_back();
		const auto seed = decodeFixed<32>(text);
		if (!seed)
		{
			error = path.string() + " is not one line of base64 holding a 32-byte seed";
			return std::nullopt;
		}
		auto id = fromSeed(*seed);
		if (!id)
			error = "libsodium did not start";
		return id;
	}

	auto id = generate();
	if (!id)
	{
		error = "libsodium did not start";
		return std::nullopt;
	}
	if (path.has_parent_path())
		std::filesystem::create_directories(path.parent_path(), ec);

	const std::string line = base64Encode(id->seed()) + "\n";
#if defined(_WIN32)
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out || !out.write(line.data(), static_cast<std::streamsize>(line.size())))
	{
		error = "cannot write " + path.string();
		return std::nullopt;
	}
#else
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
	if (fd < 0)
	{
		error = "cannot create " + path.string() + ": " + std::strerror(errno);
		return std::nullopt;
	}
	const bool ok = ::write(fd, line.data(), line.size()) == static_cast<ssize_t>(line.size());
	::close(fd);
	if (!ok)
	{
		error = "cannot write " + path.string();
		return std::nullopt;
	}
#endif
	created = true;
	return id;
}

Signature ServerIdentity::sign(std::span<const uint8_t> message) const
{
	Signature sig{};
	crypto_sign_ed25519_detached(sig.data(), nullptr, message.data(), message.size(), m_secret.data());
	return sig;
}

///////////////////////////////////////////////////////////////////////////////

std::optional<ClientHello> ClientHello::parse(std::string_view line, size_t prefixLength, bool& malformed)
{
	malformed = false;
	if (line.size() < prefixLength + TOKEN.size() || line.substr(prefixLength, TOKEN.size()) != TOKEN)
		return std::nullopt;

	malformed = true;
	const size_t innerAt = prefixLength + TOKEN.size();
	const size_t keyAt = innerAt + 8;
	if (line.size() < keyAt + KEY_B64_LEN || line.size() > 0xFFFF)
		return std::nullopt;

	ClientHello hello;
	hello.line = std::string(line);
	hello.prefixLength = prefixLength;
	hello.inner = std::string(line.substr(innerAt, 8));
	if (!std::ranges::all_of(hello.inner, [](const char c) { return c >= 0x21 && c <= 0x7E; }))
		return std::nullopt;
	const auto key = decodeFixed<32>(line.substr(keyAt, KEY_B64_LEN));
	if (!key)
		return std::nullopt;
	hello.clientPublic = *key;
	hello.suffix = std::string(line.substr(keyAt + KEY_B64_LEN));

	malformed = false;
	return hello;
}

std::optional<ServerHello> respond(const ClientHello& hello, const ServerIdentity& identity)
{
	if (!init())
		return std::nullopt;
	Key32 secret{};
	randombytes_buf(secret.data(), secret.size());
	auto out = respondWith(hello, identity, secret);
	sodium_memzero(secret.data(), secret.size());
	return out;
}

std::optional<ServerHello> respondWith(const ClientHello& hello, const ServerIdentity& identity, const Key32& ephemeralSecret)
{
	Key32 ephemeralPublic{};
	crypto_scalarmult_base(ephemeralPublic.data(), ephemeralSecret.data());

	Key32 shared{};
	if (!x25519(shared, ephemeralSecret, hello.clientPublic))
		return std::nullopt;

	const auto signedPart = serverHelloSigned(hello.line, ephemeralPublic, identity.publicKey());
	const auto signature = identity.sign(signedPart);
	const auto derived = derive(signedPart, signature, shared);
	sodium_memzero(shared.data(), shared.size());

	std::string line(TOKEN);
	line += base64Encode(ephemeralPublic);
	line += identity.publicKeyBase64();
	line += base64Encode(signature);
	return ServerHello{std::move(line), Opener(derived.key, derived.transcript)};
}

std::optional<std::string> Opener::open(std::string_view c2Line) const
{
	if (!c2Line.starts_with(TOKEN))
		return std::nullopt;
	const auto sealed = base64Decode(c2Line.substr(TOKEN.size()));
	constexpr size_t TAG = crypto_aead_chacha20poly1305_ietf_ABYTES;
	constexpr size_t MAX_PLAIN = (2 + MAX_TAIL + PAD_TO - 1) / PAD_TO * PAD_TO;
	if (!sealed || sealed->size() < TAG + PAD_TO || (sealed->size() - TAG) % PAD_TO != 0 || sealed->size() - TAG > MAX_PLAIN)
		return std::nullopt;

	std::vector<uint8_t> plain(sealed->size() - TAG);
	unsigned long long plainLength = 0;
	if (crypto_aead_chacha20poly1305_ietf_decrypt(plain.data(), &plainLength, nullptr,
												  sealed->data(), sealed->size(),
												  m_transcript.data(), m_transcript.size(),
												  zeroNonce(), m_key.data())
		!= 0)
		return std::nullopt;

	const size_t length = plain[0] | (size_t(plain[1]) << 8);
	if (length > MAX_TAIL || 2 + length > plainLength)
		return std::nullopt;
	if (sodium_is_zero(plain.data() + 2 + length, plainLength - 2 - length) != 1)
		return std::nullopt;

	std::string tail(reinterpret_cast<const char*>(plain.data() + 2), length);
	sodium_memzero(plain.data(), plain.size());
	return tail;
}

///////////////////////////////////////////////////////////////////////////////

std::optional<ClientHandshake> ClientHandshake::create(std::string_view prefix, std::string_view inner, std::string_view suffix)
{
	if (!init())
		return std::nullopt;
	Key32 secret{};
	randombytes_buf(secret.data(), secret.size());
	auto hs = createWith(prefix, inner, suffix, secret);
	sodium_memzero(secret.data(), secret.size());
	return hs;
}

std::optional<ClientHandshake> ClientHandshake::createWith(std::string_view prefix, std::string_view inner, std::string_view suffix, const Key32& secret)
{
	if (inner.size() != 8 || !init())
		return std::nullopt;
	ClientHandshake hs;
	hs.m_secret = secret;
	crypto_scalarmult_base(hs.m_public.data(), hs.m_secret.data());
	hs.m_hello = std::string(prefix);
	hs.m_hello += TOKEN;
	hs.m_hello += inner;
	hs.m_hello += base64Encode(hs.m_public);
	hs.m_hello += suffix;
	return hs;
}

std::optional<ClientHandshake::Verified> ClientHandshake::finish(std::string_view s1) const
{
	if (s1.size() != TOKEN.size() + 2 * KEY_B64_LEN + SIGNATURE_B64_LEN || !s1.starts_with(TOKEN))
		return std::nullopt;
	const auto serverX = decodeFixed<32>(s1.substr(TOKEN.size(), KEY_B64_LEN));
	const auto serverEd = decodeFixed<32>(s1.substr(TOKEN.size() + KEY_B64_LEN, KEY_B64_LEN));
	const auto signature = decodeFixed<64>(s1.substr(TOKEN.size() + 2 * KEY_B64_LEN));
	if (!serverX || !serverEd || !signature)
		return std::nullopt;

	const auto signedPart = serverHelloSigned(m_hello, *serverX, *serverEd);
	if (crypto_sign_ed25519_verify_detached(signature->data(), signedPart.data(), signedPart.size(), serverEd->data()) != 0)
		return std::nullopt;

	Key32 shared{};
	if (!x25519(shared, m_secret, *serverX))
		return std::nullopt;
	const auto derived = derive(signedPart, *signature, shared);
	sodium_memzero(shared.data(), shared.size());
	return Verified{*serverEd, derived.key, derived.transcript};
}

std::optional<std::string> ClientHandshake::Verified::seal(std::string_view tail) const
{
	if (tail.size() > MAX_TAIL)
		return std::nullopt;
	const size_t plainLength = (2 + tail.size() + PAD_TO - 1) / PAD_TO * PAD_TO;
	std::vector<uint8_t> plain(plainLength, 0);
	plain[0] = static_cast<uint8_t>(tail.size() & 0xFF);
	plain[1] = static_cast<uint8_t>((tail.size() >> 8) & 0xFF);
	std::memcpy(plain.data() + 2, tail.data(), tail.size());

	std::vector<uint8_t> sealed(plainLength + crypto_aead_chacha20poly1305_ietf_ABYTES);
	unsigned long long sealedLength = 0;
	crypto_aead_chacha20poly1305_ietf_encrypt(sealed.data(), &sealedLength, plain.data(), plain.size(),
											  transcript.data(), transcript.size(), nullptr, zeroNonce(), key.data());
	sodium_memzero(plain.data(), plain.size());
	sealed.resize(sealedLength);
	return std::string(TOKEN) + base64Encode(sealed);
}

///////////////////////////////////////////////////////////////////////////////
} // namespace preagonal::seclogin
