// Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
// SEI00001 secure login (seihai/documentation/secure-login.md, revision 1).
//
// The "fixed vector" was made independently from the document with Python's
// pyca/cryptography (Ed25519 seed 00..1f, server X25519 secret 40..5f, client
// X25519 secret 80..9f, gserver prefix "%a" = PLTYPE_CLIENT3 + key 0x41, inner
// GNW28015, tail GSTRING("test1") GSTRING("labpw")), so it checks this code
// against a second implementation, not against itself.

#define CATCH_CONFIG_MAIN
#include "catch2/catch_all.hpp"

#include <filesystem>
#include <fstream>
#include <string>

#include <sodium.h>

#if !defined(_WIN32)
#include <sys/stat.h>
#endif

#include <utilities/SecureLogin.h>

using namespace preagonal;
using namespace preagonal::seclogin;

namespace
{
	std::string hex(std::span<const uint8_t> data)
	{
		static constexpr char digits[] = "0123456789abcdef";
		std::string out;
		for (const auto b : data)
		{
			out += digits[b >> 4];
			out += digits[b & 15];
		}
		return out;
	}

	Key32 fromHex(std::string_view text)
	{
		Key32 out{};
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = static_cast<uint8_t>(std::stoi(std::string(text.substr(i * 2, 2)), nullptr, 16));
		return out;
	}

	Key32 counting(uint8_t start)
	{
		Key32 out{};
		for (size_t i = 0; i < out.size(); ++i)
			out[i] = static_cast<uint8_t>(start + i);
		return out;
	}

	constexpr std::string_view VEC_C1 = "%aSEI00001GNW28015ST6C/HRGSlkmiBdiPSBTxeuOLMSpiLT+4XnsawENUx0";
	constexpr std::string_view VEC_S1 = "SEI00001eaYx7t4b+cmPEgMs3q3Q56B5OY/HhriMyEbsia+FpRoA6EHv/POEL4dcN0Y50vAmWfk1jCbpQ1fHdyGZBJVMbg7zJvV7qQwbY7KXGIQCk96lPo4Yz5k+hwUWN7yoAwyQvBbQYKvXjC95DPSXczo38vlSw7jgbWAVpcA8syQbicBw";
	constexpr std::string_view VEC_C2 = "SEI00001D9yUpUUI0R6VgG1QcRbQ5LtnZZDUOCJGnpXvFnAuE22eNVszliBIRzULlkn3lIeZLgFQLYiFyNuE5907zEQReQOFOsIkzAljPbqnfiME1Ok";
	constexpr std::string_view VEC_TH = "2ab182d2d5986dc23684e3a4c11570436a5140da2210d2bd9104dded2491d573";
	constexpr std::string_view VEC_KEY = "2cbff7e7d157bc0b66de58ff1059d7075bab507f2a7c4d61b9f8917ab95cb98d";
	constexpr std::string_view VEC_EDPUB = "03a107bff3ce10be1d70dd18e74bc09967e4d6309ba50d5f1ddc8664125531b8";
	constexpr std::string_view VEC_FP = "SHA256:Vkdap1RjR0wChd9dvyvKtz2mUTWIOem3dIGy6rEHcIw";
	constexpr std::string_view VEC_TAIL = "%test1%labpw";

	ClientHello parseOrFail(std::string_view line, size_t prefixLength)
	{
		bool malformed = true;
		auto hello = ClientHello::parse(line, prefixLength, malformed);
		REQUIRE(hello.has_value());
		REQUIRE_FALSE(malformed);
		return *hello;
	}
} // namespace

TEST_CASE("SEI00001 base64 is standard, unpadded and strict", "[seclogin]")
{
	REQUIRE(init());
	const std::string foobar = "foobar";
	const std::span<const uint8_t> bytes(reinterpret_cast<const uint8_t*>(foobar.data()), foobar.size());
	CHECK(base64Encode(bytes.first(0)) == "");
	CHECK(base64Encode(bytes.first(1)) == "Zg");
	CHECK(base64Encode(bytes.first(2)) == "Zm8");
	CHECK(base64Encode(bytes.first(3)) == "Zm9v");
	CHECK(base64Encode(bytes) == "Zm9vYmFy");

	const auto decoded = base64Decode("Zm9vYmE");
	REQUIRE(decoded.has_value());
	CHECK(std::string(decoded->begin(), decoded->end()) == "fooba");

	CHECK_FALSE(base64Decode("Zm8=").has_value());	 // padding
	CHECK_FALSE(base64Decode("Zm9").has_value());	 // unused bits not zero
	CHECK_FALSE(base64Decode("Zm9vY").has_value());	 // impossible length
	CHECK_FALSE(base64Decode("Zm9v\n").has_value()); // other characters
	CHECK_FALSE(base64Decode("Zm-_").has_value());	 // URL alphabet
}

TEST_CASE("SEI00001 Ed25519 matches RFC 8032 test 1", "[seclogin]")
{
	const auto id = ServerIdentity::fromSeed(fromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60"));
	REQUIRE(id.has_value());
	CHECK(hex(id->publicKey()) == "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
	CHECK(hex(id->sign({})) == "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
}

TEST_CASE("SEI00001 fixed vector, server side", "[seclogin]")
{
	const auto id = ServerIdentity::fromSeed(counting(0x00));
	REQUIRE(id.has_value());
	CHECK(hex(id->publicKey()) == VEC_EDPUB);
	CHECK(id->fingerprint() == VEC_FP);

	const auto hello = parseOrFail(VEC_C1, 2);
	CHECK(hello.inner == "GNW28015");
	CHECK(hello.suffix.empty());

	const auto response = respondWith(hello, *id, counting(0x40));
	REQUIRE(response.has_value());
	CHECK(response->line == VEC_S1);
	CHECK(hex(response->opener.transcript()) == VEC_TH);
	CHECK(hex(response->opener.key()) == VEC_KEY);

	const auto tail = response->opener.open(VEC_C2);
	REQUIRE(tail.has_value());
	CHECK(*tail == VEC_TAIL);
}

TEST_CASE("SEI00001 fixed vector, client side", "[seclogin]")
{
	const auto hs = ClientHandshake::createWith("%a", "GNW28015", "", counting(0x80));
	REQUIRE(hs.has_value());
	CHECK(hs->hello() == VEC_C1);

	const auto verified = hs->finish(VEC_S1);
	REQUIRE(verified.has_value());
	CHECK(hex(verified->serverKey) == VEC_EDPUB);
	CHECK(hex(verified->key) == VEC_KEY);
	CHECK(hex(verified->transcript) == VEC_TH);

	const auto c2 = verified->seal(VEC_TAIL);
	REQUIRE(c2.has_value());
	CHECK(*c2 == VEC_C2);
}

TEST_CASE("SEI00001 round trip with fresh keys", "[seclogin]")
{
	const auto id = ServerIdentity::generate();
	REQUIRE(id.has_value());

	// gen 2 (1.x): type only; listserver style suffix is refused by the gserver but parses here.
	for (const auto& [prefix, inner] : {std::pair<std::string, std::string>{" ", "GNW20020"}, {"%a", "G3D0311C"}, {"%\x7f", "GNW28015"}})
	{
		const auto hs = ClientHandshake::create(prefix, inner);
		REQUIRE(hs.has_value());
		const auto hello = parseOrFail(hs->hello(), prefix.size());
		CHECK(hello.inner == inner);

		const auto response = respond(hello, *id);
		REQUIRE(response.has_value());
		const auto verified = hs->finish(response->line);
		REQUIRE(verified.has_value());
		CHECK(verified->serverKey == id->publicKey());
		CHECK(verified->key == response->opener.key());

		std::string tail = "\x25test1\x25labpw";
		tail += "win,\"\",02e2465a2bf38f8a115f6208e9938ac8,ff144a9abb9eaff4b606f0336d6d8bc5,\"6.2 9200 \"";
		const auto c2 = verified->seal(tail);
		REQUIRE(c2.has_value());
		const auto opened = response->opener.open(*c2);
		REQUIRE(opened.has_value());
		CHECK(*opened == tail);
	}
}

TEST_CASE("SEI00001 pads to 64 bytes and caps the tail at 1024", "[seclogin]")
{
	const auto id = ServerIdentity::generate();
	const auto hs = ClientHandshake::create("%a", "GNW28015");
	const auto response = respond(parseOrFail(hs->hello(), 2), *id);
	const auto verified = hs->finish(response->line);
	REQUIRE(verified.has_value());

	const auto sealedSize = [&](size_t tailLength)
	{
		const auto c2 = verified->seal(std::string(tailLength, 'x'));
		REQUIRE(c2.has_value());
		return base64Decode(std::string_view(*c2).substr(TOKEN.size()))->size();
	};
	CHECK(sealedSize(0) == 64 + 16);
	CHECK(sealedSize(62) == 64 + 16);
	CHECK(sealedSize(63) == 128 + 16);
	CHECK(sealedSize(1024) == 1088 + 16);
	CHECK_FALSE(verified->seal(std::string(1025, 'x')).has_value());

	// The server opens the longest tail too.
	const auto longest = verified->seal(std::string(1024, 'y'));
	CHECK(response->opener.open(*longest) == std::string(1024, 'y'));
}

TEST_CASE("SEI00001 refuses tampering", "[seclogin]")
{
	const auto id = ServerIdentity::fromSeed(counting(0x00));
	const auto hello = parseOrFail(VEC_C1, 2);
	const auto response = respondWith(hello, *id, counting(0x40));
	REQUIRE(response.has_value());
	const auto hs = ClientHandshake::createWith("%a", "GNW28015", "", counting(0x80));

	SECTION("a changed S1 does not verify")
	{
		for (size_t at : {size_t{8}, size_t{8 + 43}, size_t{8 + 86}, VEC_S1.size() - 2})
		{
			std::string s1(VEC_S1);
			s1[at] = s1[at] == 'A' ? 'B' : 'A';
			CHECK_FALSE(hs->finish(s1).has_value());
		}
		CHECK_FALSE(hs->finish(std::string(VEC_S1) + "A").has_value());
		CHECK_FALSE(hs->finish(VEC_S1.substr(0, VEC_S1.size() - 1)).has_value());
	}

	SECTION("a downgraded C1 (other inner token or key byte) gets a signature the client refuses")
	{
		std::string c1(VEC_C1);
		c1[1] = 'b';
		const auto other = respondWith(parseOrFail(c1, 2), *id, counting(0x40));
		REQUIRE(other.has_value());
		CHECK_FALSE(hs->finish(other->line).has_value());
	}

	SECTION("a changed C2 does not open")
	{
		for (size_t at : {size_t{8}, size_t{40}, VEC_C2.size() - 3})
		{
			std::string c2(VEC_C2);
			c2[at] = c2[at] == 'A' ? 'B' : 'A';
			CHECK_FALSE(response->opener.open(c2).has_value());
		}
		CHECK_FALSE(response->opener.open(VEC_C2.substr(8)).has_value());
		CHECK_FALSE(response->opener.open(std::string("SEI00002") + std::string(VEC_C2.substr(8))).has_value());
	}

	SECTION("a C2 for another handshake does not open")
	{
		const auto fresh = respond(hello, *id);
		REQUIRE(fresh.has_value());
		CHECK_FALSE(fresh->opener.open(VEC_C2).has_value());
	}

	SECTION("padding must be zero and the length must fit")
	{
		const auto seal = [&](std::vector<uint8_t> plain)
		{
			std::vector<uint8_t> sealed(plain.size() + crypto_aead_chacha20poly1305_ietf_ABYTES);
			unsigned long long length = 0;
			const std::array<uint8_t, 12> nonce{};
			crypto_aead_chacha20poly1305_ietf_encrypt(sealed.data(), &length, plain.data(), plain.size(),
													  response->opener.transcript().data(), 32, nullptr, nonce.data(), response->opener.key().data());
			return std::string(TOKEN) + base64Encode(sealed);
		};
		std::vector<uint8_t> plain(64, 0);
		plain[0] = 3;
		plain[2] = 'a';
		plain[3] = 'b';
		plain[4] = 'c';
		CHECK(response->opener.open(seal(plain)) == "abc");

		auto dirty = plain;
		dirty[63] = 1;
		CHECK_FALSE(response->opener.open(seal(dirty)).has_value());

		auto tooLong = plain;
		tooLong[0] = 63;
		CHECK_FALSE(response->opener.open(seal(tooLong)).has_value());

		CHECK_FALSE(response->opener.open(seal(std::vector<uint8_t>(65, 0))).has_value()); // not a multiple of 64
	}
}

TEST_CASE("SEI00001 hello parsing", "[seclogin]")
{
	bool malformed = false;
	CHECK_FALSE(ClientHello::parse("%aGNW28015%test1%labpw", 2, malformed).has_value());
	CHECK_FALSE(malformed); // A classic login.

	CHECK_FALSE(ClientHello::parse("%aSEI00001GNW28015", 2, malformed).has_value());
	CHECK(malformed); // No key.

	std::string badKey(VEC_C1);
	badKey[20] = '=';
	CHECK_FALSE(ClientHello::parse(badKey, 2, malformed).has_value());
	CHECK(malformed);

	std::string badInner(VEC_C1);
	badInner[12] = '\x05';
	CHECK_FALSE(ClientHello::parse(badInner, 2, malformed).has_value());
	CHECK(malformed);

	const auto withSuffix = ClientHello::parse(std::string(VEC_C1) + "newmain", 2, malformed);
	REQUIRE(withSuffix.has_value());
	CHECK(withSuffix->suffix == "newmain");

	// A small-order client key (all zero) gives an all-zero shared secret: no S1.
	std::string zeroKey = "%aSEI00001GNW28015" + base64Encode(Key32{});
	const auto zero = ClientHello::parse(zeroKey, 2, malformed);
	REQUIRE(zero.has_value());
	CHECK_FALSE(respond(*zero, *ServerIdentity::generate()).has_value());
}

TEST_CASE("SEI00001 server key file", "[seclogin]")
{
	const auto dir = std::filesystem::temp_directory_path() / ("seclogin-test-" + std::to_string(randombytes_random()));
	std::filesystem::remove_all(dir);
	const auto path = dir / "config" / "seihai-login.key";

	bool created = false;
	std::string error;
	const auto first = ServerIdentity::loadOrCreate(path, created, error);
	REQUIRE(first.has_value());
	CHECK(created);

	std::ifstream in(path);
	std::string line;
	std::getline(in, line);
	CHECK(line.size() == 43);
	CHECK(base64Decode(line) == std::vector<uint8_t>(first->seed().begin(), first->seed().end()));
#if !defined(_WIN32)
	struct stat st{};
	REQUIRE(::stat(path.c_str(), &st) == 0);
	CHECK((st.st_mode & 0777) == 0600);
#endif

	const auto second = ServerIdentity::loadOrCreate(path, created, error);
	REQUIRE(second.has_value());
	CHECK_FALSE(created);
	CHECK(second->publicKey() == first->publicKey());

	std::ofstream(path, std::ios::trunc) << "not a key\n";
	CHECK_FALSE(ServerIdentity::loadOrCreate(path, created, error).has_value());
	CHECK_FALSE(error.empty());

	std::filesystem::remove_all(dir);
}
