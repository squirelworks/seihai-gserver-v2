// Seihai project, 2026-11-21: see CHANGES-SEIHAI.md.
// Written from the public project contract, RFC 6229 and RFC 8017.
#include <utilities/ConnectorCipher.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <sodium.h>
#define LTM_DESC
#include <tomcrypt.h>
#include <zlib.h>

namespace preagonal::connector
{
namespace
{
    void erase(std::string& s)
    {
        if (!s.empty()) sodium_memzero(s.data(), s.size());
        s.clear();
    }
    size_t length(std::string_view s)
    {
        return (size_t(static_cast<unsigned char>(s[2])) << 16)
             | (size_t(static_cast<unsigned char>(s[3])) << 8)
             | size_t(static_cast<unsigned char>(s[4]));
    }
    std::string header(unsigned char compression, unsigned char counter, size_t n, unsigned char id)
    {
        return {char(compression), char(counter), char(n >> 16), char(n >> 8), char(n), char(id)};
    }
    std::optional<std::string> inflateKey(std::string_view z)
    {
        // Exactly a six-byte header and a 256-byte RSA block. Reject extra
        // inflated data, trailing zlib data, dictionary requests and truncation.
        std::string out(263, '\0');
        z_stream stream{};
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(z.data()));
        stream.avail_in = static_cast<uInt>(z.size());
        stream.next_out = reinterpret_cast<Bytef*>(out.data());
        stream.avail_out = static_cast<uInt>(out.size());
        if (inflateInit(&stream) != Z_OK) return std::nullopt;
        const int result = inflate(&stream, Z_FINISH);
        const bool valid = result == Z_STREAM_END && stream.total_out == 262 && stream.avail_in == 0;
        inflateEnd(&stream);
        if (!valid) return std::nullopt;
        out.resize(262);
        return out;
    }
    void initMath()
    {
        static std::once_flag once;
        std::call_once(once, [] { if (ltc_mp.name == nullptr) ltc_mp = ltm_desc; });
    }
}

Route Selector::inspect(std::string_view initial)
{
    if (m_route != Route::Pending) return m_route;
    const auto n = std::min(initial.size(), Token.size());
    if (initial.substr(0, n) != Token.substr(0, n)) m_route = Route::Classic;
    else if (initial.size() >= Token.size()) m_route = Route::Connector;
    return m_route;
}

Rc4::~Rc4() { sodium_memzero(m_s.data(), m_s.size()); }
void Rc4::reset(const Key& key)
{
    for (size_t i = 0; i < m_s.size(); ++i) m_s[i] = static_cast<unsigned char>(i);
    unsigned char j = 0;
    for (size_t i = 0; i < m_s.size(); ++i)
    {
        j = static_cast<unsigned char>(j + m_s[i] + key[i % key.size()]);
        std::swap(m_s[i], m_s[j]);
    }
    m_i = m_j = 0;
    m_ready = true;
}
void Rc4::apply(std::string& bytes)
{
    if (!m_ready) throw std::logic_error("RC4 key is not set");
    for (char& byte : bytes)
    {
        ++m_i;
        m_j = static_cast<unsigned char>(m_j + m_s[m_i]);
        std::swap(m_s[m_i], m_s[m_j]);
        byte = static_cast<char>(static_cast<unsigned char>(byte) ^ m_s[static_cast<unsigned char>(m_s[m_i] + m_s[m_j])]);
    }
}

struct Rsa2048::Impl { rsa_key key{}; bool imported = false; };
Rsa2048::Rsa2048() : m_impl(std::make_unique<Impl>()) {}
Rsa2048::~Rsa2048() { if (m_impl->imported) rsa_free(&m_impl->key); }
std::shared_ptr<Rsa2048> Rsa2048::import(std::string_view der, bool requirePrivate)
{
    if (der.empty() || der.size() > 8192) return {};
    initMath();
    auto out = std::shared_ptr<Rsa2048>(new Rsa2048);
    auto& key = out->m_impl->key;
    if (rsa_import(reinterpret_cast<const unsigned char*>(der.data()), static_cast<unsigned long>(der.size()), &key) != CRYPT_OK) return {};
    out->m_impl->imported = true;
    if (ltc_mp.unsigned_size(key.N) != 256 || ltc_mp.count_bits(key.N) != 2048
        || ltc_mp.compare_d(key.e, 65537) != LTC_MP_EQ
        || (requirePrivate && key.type != PK_PRIVATE)) return {};
    return out;
}
std::optional<std::string> Rsa2048::wrap(std::string_view plain) const
{
    if (plain.size() > 245 || sodium_init() < 0) return std::nullopt;
    std::string em(256, '\0');
    em[1] = 2;
    const size_t stop = 255 - plain.size();
    for (size_t i = 2; i < stop; ++i)
    {
        unsigned char byte = 0;
        do { randombytes_buf(&byte, 1); } while (byte == 0);
        em[i] = static_cast<char>(byte);
    }
    std::copy(plain.begin(), plain.end(), em.begin() + static_cast<ptrdiff_t>(stop + 1));
    std::string out(256, '\0');
    unsigned long size = 256;
    const int result = rsa_exptmod(reinterpret_cast<const unsigned char*>(em.data()), 256,
        reinterpret_cast<unsigned char*>(out.data()), &size, PK_PUBLIC, &m_impl->key);
    erase(em);
    if (result != CRYPT_OK || size != 256) return std::nullopt;
    return out;
}
std::optional<std::string> Rsa2048::unwrap(std::string_view block) const
{
    if (block.size() != 256 || m_impl->key.type != PK_PRIVATE) return std::nullopt;
    std::string em(256, '\0');
    unsigned long size = 256;
    const int result = rsa_exptmod(reinterpret_cast<const unsigned char*>(block.data()), 256,
        reinterpret_cast<unsigned char*>(em.data()), &size, PK_PRIVATE, &m_impl->key);
    size_t delimiter = 2;
    while (delimiter < em.size() && em[delimiter] != 0) ++delimiter;
    const bool valid = result == CRYPT_OK && size == 256 && em[0] == 0 && em[1] == 2 && delimiter >= 10 && delimiter < 256;
    std::optional<std::string> plain;
    if (valid) plain = em.substr(delimiter + 1);
    erase(em);
    return plain;
}

std::optional<std::string> readPem(const std::string& path, bool privateKey)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) return std::nullopt;
    // Read a bounded file without allowing an arbitrary key file to fill memory.
    std::string pem(16385, '\0');
    file.read(pem.data(), static_cast<std::streamsize>(pem.size()));
    pem.resize(static_cast<size_t>(file.gcount()));
    if (pem.size() > 16384) { erase(pem); return std::nullopt; }
    const std::string label = privateKey ? "RSA PRIVATE KEY" : "PUBLIC KEY";
    const std::string begin = "-----BEGIN " + label + "-----";
    const std::string end = "-----END " + label + "-----";
    if (!pem.starts_with(begin)) { erase(pem); return std::nullopt; }
    const auto stop = pem.find(end, begin.size());
    if (stop == std::string::npos || pem.find_first_not_of("\r\n \t", stop + end.size()) != std::string::npos)
    { erase(pem); return std::nullopt; }
    std::string encoded = pem.substr(begin.size(), stop - begin.size());
    std::string der(8192, '\0');
    size_t size = 0;
    const bool ok = sodium_init() >= 0 && sodium_base642bin(reinterpret_cast<unsigned char*>(der.data()), der.size(),
        encoded.data(), encoded.size(), "\r\n \t", &size, nullptr, sodium_base64_VARIANT_ORIGINAL) == 0;
    erase(pem); erase(encoded);
    if (!ok) { erase(der); return std::nullopt; }
    der.resize(size);
    return der;
}

std::string keyFrame(std::string_view block)
{
    if (block.size() != 256) return {};
    const std::string inner = header(0, 1, 262, 0xfc) + std::string(block);
    std::string z(compressBound(static_cast<uLong>(inner.size())), '\0');
    uLongf size = static_cast<uLongf>(z.size());
    if (compress2(reinterpret_cast<Bytef*>(z.data()), &size,
        reinterpret_cast<const Bytef*>(inner.data()), static_cast<uLong>(inner.size()), Z_DEFAULT_COMPRESSION) != Z_OK) return {};
    z.resize(size);
    return header(1, 2, 6 + z.size(), 0xfd) + z;
}

Session::Session(Transform unwrap, Transform wrap) : m_unwrap(std::move(unwrap)), m_wrap(std::move(wrap)) {}
Session::~Session() { erase(m_pending); erase(m_plain); }
bool Session::fail()
{
    m_failed = true;
    m_ready = false;
    erase(m_pending); erase(m_plain); erase(m_reply);
    m_unwrap = {}; m_wrap = {};
    return false;
}
bool Session::feed(std::string_view wire)
{
    if (m_failed) return false;
    if (wire.size() > MaxFrame || m_pending.size() + wire.size() > MaxFrame || m_plain.size() + wire.size() > MaxFrame) return fail();
    if (m_ready)
    {
        std::string bytes(wire);
        m_in.apply(bytes);
        m_plain += bytes;
        erase(bytes);
        return true;
    }
    m_pending += wire;
    if (m_pending.size() < 6) return true;
    const auto n = length(m_pending);
    if (m_pending[0] != 1 || m_pending[1] != 2 || static_cast<unsigned char>(m_pending[5]) != 0xfd || n < 6 || n > 1024) return fail();
    if (m_pending.size() < n) return true;
    const auto inner = inflateKey(std::string_view(m_pending).substr(6, n - 6));
    if (!inner || (*inner)[0] != 0 || (*inner)[1] != 1 || length(*inner) != 262 || static_cast<unsigned char>((*inner)[5]) != 0xfc) return fail();
    auto client = m_unwrap(std::string_view(*inner).substr(6));
    if (!client || client->size() != 16) { if (client) erase(*client); return fail(); }
    if (sodium_init() < 0) { erase(*client); return fail(); }
    Key in{}, out{};
    std::copy(client->begin(), client->end(), in.begin());
    erase(*client);
    randombytes_buf(out.data(), out.size());
    std::string plain{char(1), char(48)};
    plain.append(reinterpret_cast<const char*>(out.data()), out.size());
    plain += char(32);
    auto wrapped = m_wrap(plain);
    erase(plain);
    if (!wrapped || wrapped->size() != 256)
    {
        sodium_memzero(in.data(), in.size()); sodium_memzero(out.data(), out.size());
        return fail();
    }
    m_reply = keyFrame(*wrapped);
    if (m_reply.empty())
    {
        sodium_memzero(in.data(), in.size()); sodium_memzero(out.data(), out.size());
        return fail();
    }
    m_in.reset(in); m_out.reset(out);
    sodium_memzero(in.data(), in.size()); sodium_memzero(out.data(), out.size());
    m_ready = true;
    m_plain = m_pending.substr(n);
    erase(m_pending);
    m_in.apply(m_plain);
    m_unwrap = {}; m_wrap = {};
    return true;
}
std::string Session::takeReply() { return std::exchange(m_reply, {}); }
std::string Session::takePlain() { return std::exchange(m_plain, {}); }
std::string Session::encrypt(std::string plain)
{
    if (!m_ready || m_failed) return {};
    m_out.apply(plain);
    return plain;
}
}
