// Seihai project, 2026-11-21: see CHANGES-SEIHAI.md.
// Offline protocol boundaries. RC4 reference bytes are RFC 6229 section 2,
// key 0102030405060708090a0b0c0d0e0f10 (no initial bytes discarded).
#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>
#include <utilities/ConnectorCipher.h>
#include <utilities/SecureLogin.h>
#include <algorithm>
#include <vector>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <zlib.h>

using namespace preagonal::connector;
namespace
{
    Key clientKey() { Key k{}; for (size_t i=0;i<k.size();++i) k[i]=static_cast<unsigned char>(i+1); return k; }
    std::string hex(std::string_view s)
    {
        const char* digits="0123456789abcdef"; std::string out;
        for (unsigned char c:s) { out+=digits[c>>4]; out+=digits[c&15]; }
        return out;
    }
    const unsigned char vector32[] = {0x9a,0xc7,0xcc,0x9a,0x60,0x9d,0x1e,0xf7,0xb2,0x93,0x28,0x99,0xcd,0xe4,0x1b,0x97,
        0x52,0x48,0xc4,0x95,0x90,0x14,0x12,0x6a,0x6e,0x8a,0x84,0xf1,0x1d,0x1a,0x9e,0x1c};
    std::string encrypted(std::string plain)
    {
        REQUIRE(plain.size()<=sizeof(vector32));
        for(size_t i=0;i<plain.size();++i) plain[i]=static_cast<char>(static_cast<unsigned char>(plain[i])^vector32[i]);
        return plain;
    }
    std::string outer(std::string_view inner)
    {
        std::string z(compressBound(static_cast<uLong>(inner.size())), '\0'); uLongf n=z.size();
        REQUIRE(compress2(reinterpret_cast<Bytef*>(z.data()),&n,reinterpret_cast<const Bytef*>(inner.data()),inner.size(),Z_DEFAULT_COMPRESSION)==Z_OK);
        z.resize(n); size_t total=n+6;
        return std::string{char(1),char(2),char(total>>16),char(total>>8),char(total),char(0xfd)}+z;
    }
    std::string inner(std::string block=std::string(256,'x'))
    { return std::string{char(0),char(1),char(0),char(1),char(6),char(0xfc)}+block; }
    std::string clientFrame() { return outer(inner()); }
    struct Peer
    {
        std::string serverPlain;
        Session session{
            [](std::string_view block)->std::optional<std::string> {
                if(block!=std::string(256,'x'))return std::nullopt;
                auto key=clientKey();return std::string(reinterpret_cast<const char*>(key.data()),key.size());
            },
            [this](std::string_view plain)->std::optional<std::string> {serverPlain=plain;return std::string(256,'y');}};
    };
}

TEST_CASE("Raw selector holds partial token and never changes its selected protocol")
{
    Selector s;
    for(size_t n=0;n<8;++n) CHECK(s.inspect(Token.substr(0,n))==Route::Pending);
    CHECK(s.inspect(std::string(Token)+clientFrame())==Route::Connector);
    CHECK(s.inspect("classic") == Route::Connector);
    for(size_t n=0;n<8;++n)
    {
        Selector mismatch; std::string bytes(Token.substr(0,n)); bytes+='!';
        CHECK(mismatch.inspect(bytes)==Route::Classic);
        CHECK(mismatch.inspect(Token)==Route::Classic);
    }
    // Existing secure-login token is INSIDE a classic frame, not at byte zero.
    Selector classic;
    const std::string framed=std::string("\0\x50",2)+"zlib-body-SEI00001";
    CHECK(classic.inspect(framed)==Route::Classic);
    bool malformed=false;
    CHECK_FALSE(preagonal::seclogin::ClientHello::parse(std::string(Token),2,malformed));
}

TEST_CASE("RC4 matches RFC6229 and reset restarts rather than continues the old key")
{
    Rc4 rc4; rc4.reset(clientKey()); std::string zeros(32,'\0');rc4.apply(zeros);
    CHECK(hex(zeros)=="9ac7cc9a609d1ef7b2932899cde41b975248c4959014126a6e8a84f11d1a9e1c");
    std::string one(1,'\0');rc4.apply(one);
    rc4.reset(clientKey()); std::string reset(32,'\0');rc4.apply(reset);CHECK(reset==zeros);
    Key other{};rc4.reset(other);std::string changed(32,'\0');rc4.apply(changed);CHECK(changed!=zeros);
    rc4.reset(clientKey());std::string split;
    for(size_t n=0;n<32;++n){std::string byte(1,'\0');rc4.apply(byte);split+=byte;}
    CHECK(split==zeros);
}

TEST_CASE("Every key-frame split retains exact clear prefix and decrypts coalesced headers once")
{
    const auto frame=clientFrame();
    const std::string plain=std::string{char(0),char(3),char(0),char(0),char(9),char(7)}+"abc";
    const auto cipher=encrypted(plain);
    for(size_t cut=0;cut<=frame.size();++cut)
    {
        Peer p;
        REQUIRE(p.session.feed(std::string_view(frame).substr(0,cut)));
        CHECK(p.session.ready()==(cut==frame.size()));
        REQUIRE(p.session.feed(frame.substr(cut)+cipher.substr(0,1)));
        REQUIRE(p.session.ready());
        CHECK(p.session.takeReply()==outer(inner(std::string(256,'y'))));
        CHECK(p.session.takeReply().empty());
        REQUIRE(p.session.feed(cipher.substr(1,3)));
        REQUIRE(p.session.feed(cipher.substr(4)));
        CHECK(p.session.takePlain()==plain);
        REQUIRE(p.serverPlain.size()==19);
        CHECK(static_cast<unsigned char>(p.serverPlain[0])==1);
        CHECK(static_cast<unsigned char>(p.serverPlain[1])==48);
        CHECK(static_cast<unsigned char>(p.serverPlain[18])==32);
        auto k=clientKey();CHECK(p.serverPlain.substr(2,16)!=std::string(reinterpret_cast<const char*>(k.data()),16));
        // Output is a separate stream: receiving bytes must not consume its key.
        Rc4 clientIn;Key server{};std::copy_n(p.serverPlain.begin()+2,16,server.begin());clientIn.reset(server);
        auto reply=p.session.encrypt(plain);CHECK(reply!=plain);clientIn.apply(reply);CHECK(reply==plain);
    }
}

TEST_CASE("Malformed clear frames fail permanently without invoking another protocol")
{
    std::vector<std::string> bad;
    auto good=clientFrame();
    for(size_t offset:{size_t(0),size_t(1),size_t(5)}){auto x=good;x[offset]^=1;bad.push_back(x);}
    bad.push_back(std::string{char(1),char(2),char(0),char(0),char(5),char(0xfd)});
    bad.push_back(std::string{char(1),char(2),char(0),char(4),char(1),char(0xfd)});
    for(size_t offset:{size_t(0),size_t(1),size_t(4),size_t(5)}){auto x=inner();x[offset]^=1;bad.push_back(outer(x));}
    bad.push_back(outer(inner()+"extra"));
    bad.push_back(outer(std::string(4096,'x')));
    // Append a byte INSIDE the outer declared zlib body.
    auto trailing=good;trailing+='x';size_t n=trailing.size();trailing[2]=char(n>>16);trailing[3]=char(n>>8);trailing[4]=char(n);bad.push_back(trailing);
    for(const auto& bytes:bad)
    {
        Peer p;CHECK_FALSE(p.session.feed(bytes));CHECK(p.session.failed());CHECK_FALSE(p.session.ready());
        CHECK(p.session.takeReply().empty());CHECK(p.session.takePlain().empty());
        CHECK_FALSE(p.session.feed(good));CHECK(p.session.encrypt("hello").empty());
    }
    Peer incomplete;REQUIRE(incomplete.session.feed(std::string_view(good).substr(0,good.size()-1)));
    CHECK_FALSE(incomplete.session.ready());CHECK(incomplete.session.takeReply().empty());
    CHECK_FALSE(incomplete.session.feed(std::string(MaxFrame+1,'x')));
}

TEST_CASE("Wrong RSA result length and wrapping failures never enable streams")
{
    for(size_t n:{size_t(0),size_t(15),size_t(17),size_t(256)})
    {
        bool wrapped=false;
        Session s([n](std::string_view)->std::optional<std::string>{return std::string(n,'k');},
            [&wrapped](std::string_view)->std::optional<std::string>{wrapped=true;return std::string(256,'x');});
        CHECK_FALSE(s.feed(clientFrame()));CHECK_FALSE(wrapped);CHECK_FALSE(s.ready());CHECK(s.takeReply().empty());
    }
    Session s([](std::string_view)->std::optional<std::string>{return std::string(16,'k');},
        [](std::string_view)->std::optional<std::string>{return std::nullopt;});
    CHECK_FALSE(s.feed(clientFrame()));CHECK_FALSE(s.ready());CHECK(s.takeReply().empty());
}

TEST_CASE("Ephemeral OpenSSL RSA fixtures interoperate and malformed padding is rejected")
{
    const char* directory=std::getenv("CONNECTOR_RSA_FIXTURES");
    REQUIRE(directory!=nullptr); // CI creates these in RUNNER_TEMP, never in source.
    const std::filesystem::path root(directory);
    auto secret=readPem((root/"server.pem").string(),true);
    auto pub=readPem((root/"server.pub.pem").string(),false);
    REQUIRE(secret);REQUIRE(pub);
    auto privateKey=Rsa2048::import(*secret,true);
    auto publicKey=Rsa2048::import(*pub,false);
    REQUIRE(privateKey);REQUIRE(publicKey);
    CHECK_FALSE(Rsa2048::import(*pub,true));
    CHECK_FALSE(Rsa2048::import("bad DER",true));
    auto read=[&](const char* name){std::ifstream f(root/name,std::ios::binary);REQUIRE(f.good());return std::string(std::istreambuf_iterator<char>(f),{});};
    const auto key=clientKey();
    const std::string plain(reinterpret_cast<const char*>(key.data()),key.size());
    const auto opened=privateKey->unwrap(read("client-key.rsa"));
    REQUIRE(opened);CHECK(*opened==plain);
    for(const char* name:{"wrong-type.rsa","short-padding.rsa","missing-delimiter.rsa"}) CHECK_FALSE(privateKey->unwrap(read(name)));
    CHECK_FALSE(privateKey->unwrap(std::string(255,'x')));
    CHECK_FALSE(privateKey->unwrap(std::string(256,char(255))));
    CHECK_FALSE(publicKey->unwrap(read("client-key.rsa")));
    CHECK_FALSE(publicKey->wrap(std::string(246,'x')));
    auto wrapped=publicKey->wrap(plain);REQUIRE(wrapped);REQUIRE(wrapped->size()==256);
    auto roundtrip=privateKey->unwrap(*wrapped);REQUIRE(roundtrip);CHECK(*roundtrip==plain);
    // Preserve the output for an INDEPENDENT OpenSSL decrypt after this test.
    std::ofstream output(root/"server-output.rsa",std::ios::binary);output.write(wrapped->data(),wrapped->size());REQUIRE(output.good());
}
