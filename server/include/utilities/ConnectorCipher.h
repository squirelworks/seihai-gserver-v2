// Seihai project, 2026-11-21: see CHANGES-SEIHAI.md.
// Project connector contract: graal-client-repro #429 and connector/CIPHER.md.
// This raw-token transport is separate from the framed SEI00001 secure login.
#ifndef SEIHAI_CONNECTORCIPHER_H
#define SEIHAI_CONNECTORCIPHER_H

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace preagonal::connector
{
constexpr std::string_view Token = "SEI00001";
constexpr size_t MaxFrame = 1024 * 1024;
using Key = std::array<unsigned char, 16>;

enum class Route { Pending, Classic, Connector };
// Inspect only the initial bytes. Once selected, a route never changes.
class Selector
{
public:
    Route inspect(std::string_view initial);
    Route route() const { return m_route; }
private:
    Route m_route = Route::Pending;
};

class Rc4
{
public:
    ~Rc4();
    void reset(const Key& key);
    void apply(std::string& bytes);
private:
    std::array<unsigned char, 256> m_s{};
    unsigned char m_i = 0, m_j = 0;
    bool m_ready = false;
};

// PKCS#1 DER private key or DER public key (PKCS#1/SPKI). No operator keys
// are built in; the caller supplies runtime lab key files outside versioned source.
class Rsa2048
{
public:
    static std::shared_ptr<Rsa2048> import(std::string_view der, bool requirePrivate);
    ~Rsa2048();
    Rsa2048(const Rsa2048&) = delete;
    Rsa2048& operator=(const Rsa2048&) = delete;
    std::optional<std::string> wrap(std::string_view plain) const;
    std::optional<std::string> unwrap(std::string_view block) const;
private:
    Rsa2048();
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

std::optional<std::string> readPem(const std::string& path, bool privateKey);
std::string keyFrame(std::string_view rsaBlock);

// Single-use handshake. A successful feed consumes the clear key frame,
// produces the clear server reply, and decrypts ONLY the subsequent bytes.
// Rejected sessions cannot be retried or used as another protocol.
class Session
{
public:
    using Transform = std::function<std::optional<std::string>(std::string_view)>;
    Session(Transform unwrap, Transform wrap);
    ~Session();
    bool feed(std::string_view wire);
    bool ready() const { return m_ready; }
    bool failed() const { return m_failed; }
    std::string takeReply();
    // For the later account/game integration. Headers are already decrypted.
    std::string takePlain();
    std::string encrypt(std::string plain);
private:
    bool fail();
    Transform m_unwrap, m_wrap;
    Rc4 m_in, m_out;
    bool m_ready = false, m_failed = false;
    std::string m_pending, m_reply, m_plain;
};
}
#endif
