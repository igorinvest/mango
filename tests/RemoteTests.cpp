// Remote streaming, M1 (src/remote/): crypto, identity, pairing proofs,
// signed SDPs, hello, the peer store, and two PeerLinks connecting over
// WebRTC in this process. With MANGO_LIVE_SIGNALING=<wss://…/v1>, also two
// RemoteStreaming instances pairing and connecting through that signaling
// service.

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>
#include <thread>

#include <glaze/glaze.hpp>

#include "FakeHttpServer.hpp"
#include "TestCheck.hpp"
#include "remote/identity/Crypto.hpp"
#include "remote/identity/DeviceIdentity.hpp"
#include "remote/forwarding/ForwardingAdapter.hpp"
#include "remote/forwarding/ForwardingClient.hpp"
#include "remote/link/HelloMessage.hpp"
#include "remote/identity/KeyStore.hpp"
#include "remote/pairing/PairingProof.hpp"
#include "remote/link/PeerLink.hpp"
#include "remote/pairing/PeerStore.hpp"
#include "remote/RemoteCore.hpp"
#include "remote/streaming/RemoteEntryId.hpp"
#include "remote/streaming/RemoteHost.hpp"
#include "remote/RemoteStreaming.hpp"
#include "remote/signaling/SdpEnvelope.hpp"
#include "remote/signaling/SignalingMessages.hpp"
#include "remote/signaling/SignalingRouter.hpp"

namespace fs = std::filesystem;
using namespace mango::remote;

namespace
{

Crypto::Bytes FromHex(std::string_view hex)
{
    Crypto::Bytes bytes;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        bytes.push_back(static_cast<std::uint8_t>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return bytes;
}

fs::path FreshTempFolder(const std::string& name)
{
    const auto folder = fs::temp_directory_path() / ("mango-remote-test-" + name);
    fs::remove_all(folder);
    fs::create_directories(folder);
    return folder;
}

// Polls condition every 50 ms until it holds or timeout passes.
bool WaitFor(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return condition();
}

// ---- Crypto ----------------------------------------------------------------

void HashVectors()
{
    Check(Crypto::ToHex(Crypto::Sha256(Crypto::AsBytes("abc"))) ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 of abc");

    // RFC 4231, test case 2.
    Check(Crypto::ToHex(Crypto::HmacSha256(Crypto::AsBytes("Jefe"), Crypto::AsBytes("what do ya want for nothing?"))) ==
              "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843",
        "HMAC-SHA256 RFC 4231 case 2");
}

void Base64Url()
{
    Check(Crypto::Base64UrlEncode(Crypto::AsBytes("")).empty(), "empty encodes to empty");
    Check(Crypto::Base64UrlEncode(Crypto::AsBytes("f")) == "Zg", "f");
    Check(Crypto::Base64UrlEncode(Crypto::AsBytes("fo")) == "Zm8", "fo");
    Check(Crypto::Base64UrlEncode(Crypto::AsBytes("foo")) == "Zm9v", "foo");
    Check(Crypto::Base64UrlEncode(FromHex("fbff")) == "-_8", "url alphabet");

    const auto random = Crypto::RandomBytes(33);
    const auto decoded = Crypto::Base64UrlDecode(Crypto::Base64UrlEncode(random));
    Check(decoded && *decoded == random, "round trip");
    Check(Crypto::Base64UrlDecode("Zm8=").value_or(Crypto::Bytes{}) == Crypto::Bytes{'f', 'o'}, "padding accepted");
    Check(!Crypto::Base64UrlDecode("Zm+v"), "standard alphabet rejected");
    Check(!Crypto::Base64UrlDecode("Z"), "impossible length rejected");
}

// RFC 8032, section 7.1, test 1.
void Ed25519Vector()
{
    const auto privateKey = FromHex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
    const auto expectedPublic = FromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
    const auto expectedSignature = FromHex(
        "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f05"
        "95bbe24655141438e7a100b");

    Check(Crypto::Ed25519PublicKey(privateKey) == expectedPublic, "public key from seed");
    const auto signature = Crypto::Ed25519Sign(privateKey, {});
    Check(signature == expectedSignature, "signature of the empty message");
    Check(Crypto::Ed25519Verify(expectedPublic, {}, signature), "verifies");

    auto tampered = signature;
    tampered[0] ^= 1;
    Check(!Crypto::Ed25519Verify(expectedPublic, {}, tampered), "tampered signature fails");
    Check(!Crypto::Ed25519Verify(expectedPublic, Crypto::AsBytes("x"), signature), "other message fails");
}

// ---- DeviceIdentity --------------------------------------------------------

void IdentityBasics()
{
    const auto identity = DeviceIdentity::CreateInMemory();
    Check(identity.has_value(), "key made");
    if (!identity)
    {
        return;
    }

    const auto publicKey = Crypto::Base64UrlDecode(identity->PublicKey());
    Check(publicKey && publicKey->size() == 32, "public key is 32 raw bytes");
    Check(identity->DeviceId() == Crypto::ToHex(Crypto::Sha256(*publicKey)), "deviceId is SHA-256 of the key");
    Check(DeviceIdentity::DeviceIdOf(identity->PublicKey()) == identity->DeviceId(), "DeviceIdOf");

    const auto signature = identity->Sign("mango-signal-v1 auth:nonce");
    Check(DeviceIdentity::Verify(identity->PublicKey(), "mango-signal-v1 auth:nonce", signature), "verifies");
    Check(!DeviceIdentity::Verify(identity->PublicKey(), "mango-signal-v1 auth:other", signature), "other text fails");
}

void IdentityPersists()
{
    const auto folder = FreshTempFolder("identity");
    const auto path = folder / "remote" / "identity.json";

    const auto first = DeviceIdentity::LoadOrCreate(path);
    const auto second = DeviceIdentity::LoadOrCreate(path);
    Check(first && second && first->DeviceId() == second->DeviceId(), "the same key after a reload");

#ifndef _WIN32
    const auto perms = fs::status(path).permissions();
    Check((perms & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none, "only the owner reads it");
#endif
    fs::remove_all(folder);
}

// ---- KeyStore --------------------------------------------------------------

// Stands in for Keychain/Credential Manager.
struct FakeKeyStore final : KeyStore
{
    std::optional<Crypto::Bytes> key;
    bool readable = true;
    bool writable = true;

    LoadResult Load() override
    {
        return readable ? LoadResult(key) : std::unexpected(std::string("locked"));
    }

    bool Save(const Crypto::Bytes& privateKey) override
    {
        if (writable)
        {
            key = privateKey;
        }
        return writable;
    }
};

// FallbackKeyStore over a fake primary, owned by the store but kept
// reachable for the checks.
struct FallbackFixture
{
    fs::path folder;
    fs::path path;
    FakeKeyStore* primary = nullptr;
    std::unique_ptr<FallbackKeyStore> store;

    explicit FallbackFixture(const std::string& name)
        : folder(FreshTempFolder(name)), path(folder / "remote" / "identity.json")
    {
        auto fake = std::make_unique<FakeKeyStore>();
        primary = fake.get();
        store = std::make_unique<FallbackKeyStore>(std::move(fake), FileKeyStore(path));
    }

    ~FallbackFixture() { fs::remove_all(folder); }
};

void KeyStoreNewKeyGoesToPrimary()
{
    FallbackFixture fixture("keystore-new");
    const auto identity = DeviceIdentity::LoadOrCreate(*fixture.store);
    Check(identity && fixture.primary->key.has_value(), "the new key is in the system store");
    Check(!fs::exists(fixture.path), "and not in the file");

    const auto again = DeviceIdentity::LoadOrCreate(*fixture.store);
    Check(identity && again && identity->DeviceId() == again->DeviceId(), "the same key after a reload");
}

void KeyStoreMigratesFileKey()
{
    FallbackFixture fixture("keystore-migrate");
    const auto old = DeviceIdentity::LoadOrCreate(fixture.path);
    const auto migrated = DeviceIdentity::LoadOrCreate(*fixture.store);
    Check(old && migrated && old->DeviceId() == migrated->DeviceId(), "the file's key is kept");
    Check(fixture.primary->key.has_value() && !fs::exists(fixture.path), "and moved to the system store");
}

void KeyStoreKeepsFileWhenPrimaryCantSave()
{
    FallbackFixture fixture("keystore-nosave");
    fixture.primary->writable = false;
    const auto first = DeviceIdentity::LoadOrCreate(*fixture.store);
    Check(first && fs::exists(fixture.path), "a key the system store refuses goes to the file");

    const auto second = DeviceIdentity::LoadOrCreate(*fixture.store);
    Check(first && second && first->DeviceId() == second->DeviceId(), "and is read back from it");
    Check(fs::exists(fixture.path), "the file stays while the system store refuses it");
}

void KeyStoreUnreadablePrimary()
{
    FallbackFixture fixture("keystore-locked");
    fixture.primary->readable = false;
    Check(!DeviceIdentity::LoadOrCreate(*fixture.store), "no new key while the system store is locked");
    Check(!fixture.primary->key && !fs::exists(fixture.path), "and nothing saved");

    const auto old = DeviceIdentity::LoadOrCreate(fixture.path);
    const auto fromFile = DeviceIdentity::LoadOrCreate(*fixture.store);
    Check(old && fromFile && old->DeviceId() == fromFile->DeviceId(), "a key in the file still works");
}

void KeyStoreRemovesLeftoverFile()
{
    FallbackFixture fixture("keystore-leftover");
    const auto inPrimary = DeviceIdentity::LoadOrCreate(*fixture.store);
    const auto inFile = DeviceIdentity::LoadOrCreate(fixture.path);
    const auto loaded = DeviceIdentity::LoadOrCreate(*fixture.store);
    Check(inPrimary && inFile && loaded && loaded->DeviceId() == inPrimary->DeviceId(), "the system store wins");
    Check(!fs::exists(fixture.path), "and the leftover file goes");
}

// ---- Pairing ---------------------------------------------------------------

void InviteLinks()
{
    const PairingProof::InviteLink link{PairingProof::NewInviteSecret(), std::string(64, 'a')};
    Check(link.secret.size() >= 16, "secret long enough");

    const auto text = PairingProof::FormatLink(link);
    const auto parsed = PairingProof::ParseLink(text);
    Check(parsed && parsed->secret == link.secret && parsed->deviceId == link.deviceId, "round trip");

    const auto reordered = PairingProof::ParseLink("mango://pair?d=" + link.deviceId + "&s=" + link.secret);
    Check(reordered && reordered->secret == link.secret, "parameters in any order");

    Check(!PairingProof::ParseLink("https://pair?s=" + link.secret + "&d=" + link.deviceId), "other scheme");
    Check(!PairingProof::ParseLink("mango://pair?s=short&d=" + link.deviceId), "short secret");
    Check(!PairingProof::ParseLink("mango://pair?s=" + link.secret + "&d=ABC"), "bad deviceId");
    Check(!PairingProof::ParseLink("mango://pair?s=" + link.secret), "missing deviceId");

    Check(PairingProof::InviteHash("secret") == "2bb80d537b1da3e38bd30361aa855686bde0eacd7162fef6a25fe97bf527a25b",
        "invite hash is hex SHA-256");
}

void PairingProofs()
{
    const auto secret = PairingProof::NewInviteSecret();
    using PairingProof::Side;

    const auto fromJoiner = PairingProof::Compute(secret, Side::Joiner, "keyA", "keyB");
    Check(PairingProof::Verify(fromJoiner, secret, Side::Joiner, "keyA", "keyB"), "verifies");
    Check(!PairingProof::Verify(fromJoiner, secret, Side::Inviter, "keyA", "keyB"), "can't be sent back");
    Check(!PairingProof::Verify(fromJoiner, "another-secret-xx", Side::Joiner, "keyA", "keyB"), "wrong secret");
    Check(!PairingProof::Verify(fromJoiner, secret, Side::Joiner, "keyA", "keyC"), "other key");
    Check(PairingProof::Compute(secret, Side::Inviter, "keyA", "keyB") != fromJoiner, "sides differ");
}

// ---- Signed SDP ------------------------------------------------------------

void SignedSdp()
{
    const auto a = DeviceIdentity::CreateInMemory();
    const auto b = DeviceIdentity::CreateInMemory();
    if (!a || !b)
    {
        Check(false, "keys made");
        return;
    }

    const std::int64_t now = 1'800'000'000;
    const auto offer = SdpEnvelope::Make(*b, "offer", "v=0\r\na=fingerprint:sha-256 AB\r\n", a->DeviceId(), "s1", now);
    Check(SdpEnvelope::Verify(offer, b->PublicKey(), a->DeviceId(), now + 5), "verifies");

    auto tampered = offer;
    tampered.sdp += "a=extra\r\n";
    Check(!SdpEnvelope::Verify(tampered, b->PublicKey(), a->DeviceId(), now), "changed SDP fails");

    Check(!SdpEnvelope::Verify(offer, b->PublicKey(), b->DeviceId(), now), "redirected to another device fails");
    Check(!SdpEnvelope::Verify(offer, a->PublicKey(), a->DeviceId(), now), "someone else's key fails");
    Check(!SdpEnvelope::Verify(offer, b->PublicKey(), a->DeviceId(), now + 600), "an old one fails");

    auto otherSession = offer;
    otherSession.sessionId = "s2";
    Check(!SdpEnvelope::Verify(otherSession, b->PublicKey(), a->DeviceId(), now), "session ID is signed");
}

// ---- Hello -----------------------------------------------------------------

void HelloRoundTrip()
{
    const auto text = Hello::Make("1.4.0");
    const auto hello = Hello::Read(text);
    Check(hello && hello->protocol == kProtocolVersion && hello->appVersion == "1.4.0", "reads back");
    Check(!Hello::Read(R"({"type":"ping"})"), "not a hello");

    // A newer peer's extra fields don't stop it being read.
    const auto newer = Hello::Read(R"({"type":"hello","protocol":1,"appVersion":"9","future":true,"initiateExample":{}})");
    Check(newer && newer->appVersion == "9", "unknown fields ignored");

    Check(Hello::Check(kProtocolVersion) == Compatibility::Compatible, "same protocol");
    Check(Hello::Check(kProtocolVersion - 1) == Compatibility::PeerNeedsUpdate, "older peer");
    Check(Hello::Check(kProtocolVersion + 1) == Compatibility::UpdateRequired, "newer peer");
}

void HelloExampleUsesBuiltInDefaults()
{
    const auto example = Hello::BuiltInInitiateExample();
    Check(example.target.playlist == "dash" && example.target.preset == "1080p" && example.target.mode == "automatic",
        "target defaults");
    Check(!example.target.videoCapabilities.empty() && example.target.videoCapabilities[0].codec == "h264",
        "built-in DASH video capabilities");
    Check(example.target.encoding.videoBitrate == "6000k" && example.target.encoding.audioCodec == "aac",
        "built-in 1080p encoding");
    Check(example.accepts.preset.size() == 3 && example.accepts.mode.size() == 3, "accepted presets and modes");
}

// ---- Wire format -----------------------------------------------------------

// The exact JSON two Mango versions exchange. A failure here means a key
// changed: older peers would stop understanding it. Add a field instead.
void WireFormatIsPinned()
{
    const SdpPayload sdp{.kind = "offer", .sdp = "v=0", .from = "a", .to = "b", .sessionId = "s", .ts = 7,
        .signature = "sig"};
    Check(signaling::Write(sdp) ==
              R"({"kind":"offer","sdp":"v=0","from":"a","to":"b","sessionId":"s","ts":7,"signature":"sig"})",
        "SDP envelope");

    Check(signaling::Write(PairPayload{.proof = "p", .name = "Office", .side = "inviter"}) ==
              R"({"kind":"pair","proof":"p","name":"Office","side":"inviter"})",
        "pairing proof");

    const signaling::SignalOut signal{.to = "b", .payload = {R"({"kind":"pair"})"}, .requestId = "m1"};
    Check(signaling::Write(signal) == R"({"type":"signal","to":"b","payload":{"kind":"pair"},"requestId":"m1"})",
        "signal");
    Check(signaling::Write(signaling::Auth{.publicKey = "k", .signature = "s"}) ==
              R"({"type":"auth","publicKey":"k","signature":"s"})",
        "auth");
    Check(signaling::Write(signaling::InviteOpen{.inviteHash = "h", .ttl = 600, .requestId = "m2"}) ==
              R"({"type":"invite.open","inviteHash":"h","ttl":600,"requestId":"m2"})",
        "invite.open");
    Check(signaling::Write(signaling::InviteJoin{.inviteSecret = "x", .requestId = std::nullopt}) ==
              R"({"type":"invite.join","inviteSecret":"x"})",
        "invite.join without a request ID");

    const auto error = signaling::Read<signaling::Error>(R"({"code":"offline","requestId":"m3"})");
    const auto legacyError = signaling::Read<signaling::Error>(R"({"code":"offline","id":"m4"})");
    Check(error && signaling::RequestIdOf(*error) == "m3", "error's requestId");
    Check(legacyError && signaling::RequestIdOf(*legacyError) == "m4", "an older service's error \"id\"");

    const HelloMessage hello{.appVersion = "1.4.0",
        .initiateExample = {.target = {.playlist = "dash",
                                .preset = "1080p",
                                .mode = "automatic",
                                .videoCapabilities = {{.codec = "h264", .maxWidth = 1920, .maxHeight = 1080,
                                    .maxFps = 60, .hdrSupported = false}},
                                .audioCapabilities = {{.codec = "aac", .maxChannels = 6, .maxSampleRate = 48000}},
                                .rotationSupported = false,
                                .encoding = {.videoBitrate = "6000k", .audioBitrate = "256k",
                                    .videoPixFmt = "yuv420p", .audioCodec = "aac"}},
            .accepts = {.playlist = {"dash"}, .preset = {"1080p"}, .mode = {"automatic"}, .videoCodecs = {"h264"},
                .audioCodecs = {"aac"}, .videoPixFmt = {"yuv420p"}, .audioCodec = {"aac"}}}};
    Check(signaling::Write(hello) ==
              R"({"type":"hello","protocol":1,"appVersion":"1.4.0","initiateExample":{"target":{"playlist":"dash",)"
              R"("preset":"1080p","mode":"automatic","videoCapabilities":[{"codec":"h264","maxWidth":1920,)"
              R"("maxHeight":1080,"maxFps":60,"hdrSupported":false}],"audioCapabilities":[{"codec":"aac",)"
              R"("maxChannels":6,"maxSampleRate":48000}],"rotationSupported":false,"encoding":{"videoBitrate":)"
              R"("6000k","audioBitrate":"256k","videoPixFmt":"yuv420p","audioCodec":"aac"}},"accepts":{)"
              R"("playlist":["dash"],"preset":["1080p"],"mode":["automatic"],"videoCodecs":["h264"],)"
              R"("audioCodecs":["aac"],"videoPixFmt":["yuv420p"],"audioCodec":["aac"]}}})",
        "hello");

    const PairedPeer peer{.deviceId = "d", .publicKey = "k", .name = "n", .pairedAtUnixSeconds = 1};
    Check(signaling::Write(peer) == R"({"deviceId":"d","publicKey":"k","name":"n","pairedAtUnixSeconds":1})",
        "clients.json / sources.json entry");
}

// ---- SignalingRouter -------------------------------------------------------

// Records what the router hands a module.
struct RecordingModule final : SignalingModule
{
    void OnSignalingReady() override { calls.push_back("ready"); }
    void OnSignalingLost() override { calls.push_back("lost"); }
    void HandleServiceMessage(const std::string& type, const std::string&) override { calls.push_back(type); }
    void HandleSdpSignal(const std::string& from, const std::string&) override { calls.push_back("sdp:" + from); }
    void HandlePairSignal(const std::string& from, const std::string&) override { calls.push_back("pair:" + from); }

    std::vector<std::string> calls;
};

std::string SignalFrom(const std::string& from, const std::string& payload)
{
    return R"({"type":"signal","from":")" + from + R"(","payload":)" + payload + "}";
}

void RouterRoutesByKind()
{
    RecordingModule host;
    RecordingModule client;
    SignalingRouter router(host, client);

    router.OnReady();
    router.OnMessage("signal", SignalFrom("d1", R"({"kind":"offer","sdp":"x"})"));
    router.OnMessage("signal", SignalFrom("d2", R"({"kind":"answer","sdp":"x"})"));
    router.OnMessage("signal", SignalFrom("d3", R"({"kind":"pair","proof":"p","name":"n","side":"joiner"})"));
    router.OnMessage("signal", SignalFrom("d4", R"({"kind":"pair","proof":"p","name":"n","side":"inviter"})"));
    router.OnMessage("signal", SignalFrom("d5", R"({"kind":"pair","proof":"p","name":"n"})"));
    router.OnMessage("signal", SignalFrom("d6", R"({"kind":"other"})"));
    router.OnMessage("invite.joined", R"({"type":"invite.joined"})");
    router.OnMessage("error", R"({"type":"error","code":"offline","requestId":"7"})");
    router.OnLost();

    const std::vector<std::string> hostWanted{"ready", "sdp:d1", "pair:d3", "invite.joined", "error", "lost"};
    const std::vector<std::string> clientWanted{"ready", "sdp:d2", "pair:d4", "invite.joined", "error", "lost"};
    Check(host.calls == hostWanted, "the host gets offers, joiners' proofs and everything shared");
    Check(client.calls == clientWanted, "the client gets answers, inviters' proofs and everything shared");

    const auto error = ReadSignalError(R"({"type":"error","code":"offline","requestId":"7"})");
    Check(error && error->requestId == "7" && error->code == "offline", "an error with its request ID");
    Check(!ReadSignalError(R"({"type":"error","code":"bad"})"), "an error without one");
}

// ---- PeerStore -------------------------------------------------------------

void PeerStorePersists()
{
    const auto folder = FreshTempFolder("peers");
    const auto path = folder / "sources.json";
    {
        PeerStore store(path);
        store.Load();
        store.Add({.deviceId = "a", .publicKey = "ka", .name = "Office", .pairedAtUnixSeconds = 1});
        store.Add({.deviceId = "b", .publicKey = "kb", .name = "Home", .pairedAtUnixSeconds = 2});
        store.Add({.deviceId = "a", .publicKey = "ka", .name = "Office Mac", .pairedAtUnixSeconds = 3});
    }

    PeerStore reloaded(path);
    reloaded.Load();
    Check(reloaded.All().size() == 2, "two peers after a re-pair");
    const auto* office = reloaded.Find("a");
    Check(office && office->name == "Office Mac", "a re-pair replaces the peer");
    Check(reloaded.NameOf("a") == "Office Mac" && reloaded.NameOf("unknown-device") == "unknown-", "names");
    Check(reloaded.Remove("b") && !reloaded.Find("b") && !reloaded.Remove("b"), "remove");
    fs::remove_all(folder);
}

// ---- PeerLink over WebRTC in this process ---------------------------------

// Two PeerLinks whose SDPs are handed to each other directly instead of
// through the signaling service. Host candidates only: no network needed.
class LinkPair
{
   public:
    LinkPair() : m_a(*DeviceIdentity::CreateInMemory()), m_b(*DeviceIdentity::CreateInMemory())
    {
        m_contextA = MakeContext(m_a, [this](const SdpPayload& p) { Deliver(p, m_b, m_a, m_linkB); });
        m_contextB = MakeContext(m_b, [this](const SdpPayload& p) { Deliver(p, m_a, m_b, m_linkA); });
    }

    ~LinkPair()
    {
        RunOnThread([this] {
            m_linkA.reset();
            m_linkB.reset();
        });
        m_thread.Stop();
    }

    void Connect()
    {
        RunOnThread([this] {
            m_linkB = PeerLink::Create(*m_contextB, Peer(m_a, "source"), true, "session-1");
            m_linkA = PeerLink::Create(*m_contextA, Peer(m_b, "relay"), false, "session-1");
            m_linkA->Start();
            m_linkB->Start();
        });
    }

    PeerLink::State StateA() { return StateOf(m_linkA); }
    PeerLink::State StateB() { return StateOf(m_linkB); }

    void PingFromB()
    {
        RunOnThread([this] { m_linkB->Ping(); });
    }

    std::optional<std::chrono::milliseconds> PingB()
    {
        std::optional<std::chrono::milliseconds> ping;
        RunOnThread([&] { ping = m_linkB->LastPing(); });
        return ping;
    }

    std::string PeerVersionSeenByA()
    {
        std::string version;
        RunOnThread([&] {
            if (const auto& hello = m_linkA->PeerHelloMessage())
            {
                version = hello->appVersion;
            }
        });
        return version;
    }

    void CloseA()
    {
        RunOnThread([this] { m_linkA->Close(); });
    }

    std::shared_ptr<LinkTransport> TransportA()
    {
        std::shared_ptr<LinkTransport> transport;
        RunOnThread([&] { transport = m_linkA->Transport(); });
        return transport;
    }

    std::shared_ptr<LinkTransport> TransportB()
    {
        std::shared_ptr<LinkTransport> transport;
        RunOnThread([&] { transport = m_linkB->Transport(); });
        return transport;
    }

    // Where each side's binary frames go, called on the links' thread.
    void SetBinaryHandlers(std::function<void(std::string)> toA, std::function<void(std::string)> toB)
    {
        RunOnThread([&] {
            m_binaryA = std::move(toA);
            m_binaryB = std::move(toB);
        });
    }

   private:
    std::unique_ptr<PeerLinkContext> MakeContext(
        const DeviceIdentity& identity, std::function<void(const SdpPayload&)> send)
    {
        return std::make_unique<PeerLinkContext>(PeerLinkContext{.thread = m_thread,
            .identity = identity,
            .appVersion = identity.DeviceId() == m_a.DeviceId() ? "a-version" : "b-version",
            .iceServers = {},
            .sendSdp = std::move(send),
            .onChanged = [](const std::string&) {},
            .onMessage = [](const std::string&, const std::string&) {},
            .onBinary = [this, isA = identity.DeviceId() == m_a.DeviceId()](const std::string&, std::string bytes) {
                auto& handler = isA ? m_binaryA : m_binaryB;
                if (handler)
                {
                    handler(std::move(bytes));
                }
            }});
    }

    static PairedPeer Peer(const DeviceIdentity& identity, const std::string& name)
    {
        return {.deviceId = identity.DeviceId(), .publicKey = identity.PublicKey(), .name = name, .pairedAtUnixSeconds = 0};
    }

    // What PeerConnections does with a received SDP: checks the signature,
    // then hands it to the link.
    void Deliver(const SdpPayload& payload, const DeviceIdentity& recipient, const DeviceIdentity& sender,
        std::shared_ptr<PeerLink>& link)
    {
        m_thread.Post([&, payload] {
            m_sdpVerified = m_sdpVerified &&
                            SdpEnvelope::Verify(payload, sender.PublicKey(), recipient.DeviceId(),
                                SdpEnvelope::NowUnixSeconds());
            if (link)
            {
                link->HandleSdp(payload);
            }
        });
    }

    PeerLink::State StateOf(const std::shared_ptr<PeerLink>& link)
    {
        auto state = PeerLink::State::Connecting;
        RunOnThread([&] { state = link ? link->GetState() : PeerLink::State::Closed; });
        return state;
    }

    void RunOnThread(const std::function<void()>& task)
    {
        std::promise<void> done;
        m_thread.Post([&] {
            task();
            done.set_value();
        });
        done.get_future().wait();
    }

   public:
    bool m_sdpVerified = true;

   private:
    mango::SessionThread m_thread;
    DeviceIdentity m_a;
    DeviceIdentity m_b;
    std::unique_ptr<PeerLinkContext> m_contextA;
    std::unique_ptr<PeerLinkContext> m_contextB;
    std::shared_ptr<PeerLink> m_linkA;
    std::shared_ptr<PeerLink> m_linkB;
    std::function<void(std::string)> m_binaryA;
    std::function<void(std::string)> m_binaryB;
};

void PeerLinkLoopback()
{
    LinkPair pair;
    pair.Connect();

    const bool ready = WaitFor(
        [&] { return pair.StateA() == PeerLink::State::Ready && pair.StateB() == PeerLink::State::Ready; },
        std::chrono::seconds(20));
    Check(ready, "both sides finish the hello handshake");
    Check(pair.m_sdpVerified, "every SDP carries a valid signature");
    if (!ready)
    {
        return;
    }
    Check(pair.PeerVersionSeenByA() == "b-version", "A read B's hello");

    pair.PingFromB();
    Check(WaitFor([&] { return pair.PingB().has_value(); }, std::chrono::seconds(5)), "a ping crosses the channel");

    pair.CloseA();
    Check(WaitFor([&] { return PeerLink::IsFinished(pair.StateB()); }, std::chrono::seconds(15)),
        "B sees A close the link");
}

// A's adapter and B's client on a real data channel: binary frames, the
// channel's buffering and the acks together.
void ForwardingOverWebRtc()
{
    FakeHttpServer server;
    LinkPair pair;
    pair.Connect();
    const bool ready = WaitFor(
        [&] { return pair.StateA() == PeerLink::State::Ready && pair.StateB() == PeerLink::State::Ready; },
        std::chrono::seconds(20));
    Check(ready, "link up");
    if (!ready)
    {
        return;
    }

    auto adapter = std::make_unique<ForwardingAdapter>(pair.TransportA(),
        ForwardingAdapter::Host{.serverUrl = [&] { return std::optional(server.Url()); }, .localRoute = {}});
    const auto client = std::make_shared<ForwardingClient>(pair.TransportB());
    pair.SetBinaryHandlers(
        [&adapter](std::string bytes) {
            if (const auto frame = forward::Decode(bytes))
            {
                adapter->OnFrame(*frame);
            }
        },
        [client](std::string bytes) {
            if (const auto frame = forward::Decode(bytes))
            {
                client->OnFrame(*frame);
            }
        });

    const auto response = FetchWhole(*client, "/stream/abc/seg.m4s", 16 * 1024 * 1024, std::chrono::seconds(20));
    Check(response && response->body == server.Body(), "5 MB crosses the data channel intact");

    pair.SetBinaryHandlers({}, {});
    adapter.reset();
    pair.CloseA();
    Check(!pair.TransportA()->Send("x"), "a closed link's transport refuses frames");
}

// ---- Live: two RemoteStreaming instances through the real service ---------

// MANGO_LIVE_SIGNALING: the service the live tests use; unset skips them.
std::string LiveSignalingUrl()
{
    const char* url = std::getenv("MANGO_LIVE_SIGNALING");
    return url ? url : "";
}

struct LiveDevice
{
    explicit LiveDevice(const std::string& name)
        : remote(mango::RemoteOptions{.folder = FreshTempFolder(name), .signalingUrl = LiveSignalingUrl(), .useSettings = false})
    {
        remote.SetAppVersion("test-" + name);
        remote.Start();
        remote.SetDeviceName(name);
    }

    mango::RemoteState State() const
    {
        mango::RemoteState state;
        std::ignore = glz::read<glz::opts{.error_on_unknown_keys = false}>(state, remote.GetStateJson());
        return state;
    }

    mango::RemoteStreaming remote;
};

// Offline (no service): the setting shows in the state, and a completed
// pairing is what turns it back on (covered by LivePairAndConnect).
void StreamingAllowedState()
{
    mango::RemoteStreaming remote(
        mango::RemoteOptions{.folder = FreshTempFolder("allowed"), .signalingUrl = {}, .useSettings = false});
    remote.Start();
    const auto allowed = [&remote]
    {
        mango::RemoteState state;
        std::ignore = glz::read<glz::opts{.error_on_unknown_keys = false}>(state, remote.GetStateJson());
        return state.streamingAllowed;
    };
    Check(WaitFor([&] { return allowed(); }, std::chrono::seconds(5)), "allowed by default");
    remote.SetStreamingAllowed(false);
    Check(WaitFor([&] { return !allowed(); }, std::chrono::seconds(5)), "turned off");
    remote.SetStreamingAllowed(true);
    Check(WaitFor([&] { return allowed(); }, std::chrono::seconds(5)), "turned on again");
}

// Opens host's invite and has client join it; false (with a Check) when
// the invite doesn't open.
bool JoinLive(LiveDevice& host, LiveDevice& client)
{
    host.remote.OpenInvite();
    if (!WaitFor([&] { return !host.State().hostPairing.link.empty(); }, std::chrono::seconds(20)))
    {
        Check(false, "the invite opens");
        return false;
    }
    Check(client.remote.JoinInvite(host.State().hostPairing.link), "the link is accepted");
    return true;
}

void LivePairAndConnect()
{
    if (LiveSignalingUrl().empty())
    {
        return;  // Needs the internet: opt in.
    }

    LiveDevice a("live-a");
    LiveDevice b("live-b");
    if (!JoinLive(a, b))
    {
        return;
    }

    const bool paired = WaitFor(
        [&] { return a.State().clients.size() == 1 && b.State().sources.size() == 1; }, std::chrono::seconds(20));
    Check(paired, "both sides paired");
    if (!paired)
    {
        return;
    }
    Check(a.State().clients[0].name == "live-b" && a.State().sources.empty(), "A has B as a client only");
    Check(b.State().sources[0].name == "live-a" && b.State().clients.empty(), "B has A as a source only");

    const bool connected = WaitFor(
        [&] { return a.State().clients[0].link == "connected" && b.State().sources[0].link == "connected"; },
        std::chrono::seconds(40));
    Check(connected, "B connects to A on its own after pairing");
    if (!connected)
    {
        return;
    }
    Check(b.State().sources[0].peerAppVersion == "test-live-a", "hello exchanged");

    b.remote.PingSource(a.State().deviceId);
    Check(WaitFor([&] { return b.State().sources[0].pingMs.has_value(); }, std::chrono::seconds(5)), "ping");

    a.remote.SetStreamingAllowed(false);
    Check(WaitFor([&] { return a.State().clients[0].link != "connected"; }, std::chrono::seconds(5)),
        "not allowed: A closes B's link");
    std::this_thread::sleep_for(std::chrono::seconds(3));
    Check(a.State().clients[0].link != "connected" && a.State().clients.size() == 1,
        "B's offers are ignored, and B stays paired");
    a.remote.SetStreamingAllowed(true);
    Check(WaitFor([&] { return a.State().clients[0].link == "connected"; }, std::chrono::seconds(40)),
        "allowed again: B reconnects");

    a.remote.RemoveClient(b.State().deviceId);
    Check(WaitFor([&] { return a.State().clients.empty(); }, std::chrono::seconds(5)), "A removes B");
}

// A's engine in the live test: one video, streams served by a FakeHttpServer.
struct FakeHost final : RemoteHost
{
    explicit FakeHost(std::string serverUrl) : url(std::move(serverUrl)) {}

    std::vector<mango::LibraryEntry> SharedLibrary() override
    {
        mango::LibraryEntry entry;
        entry.fastHash = "movie1";
        entry.title = "Movie";
        entry.durationSeconds = 100;
        return {entry};
    }
    std::optional<fs::path> CoverFile(const std::string&) override { return std::nullopt; }
    HostedStream StartStream(const std::string& fastHash, const TrackRequest& tracks, const StreamTarget& target) override
    {
        startedFastHash = fastHash;
        startedTracks = tracks;
        startedWithEncoding = target.encoding.has_value();
        HostedStream hosted;
        hosted.initiation.success = true;
        // A picks what B didn't choose: here audio 1, subtitle 2.
        hosted.initiation.audioTrackId = tracks.audioTrackId.value_or(1);
        hosted.initiation.subtitleTrackId = tracks.subtitleTrackId.value_or(2);
        hosted.initiation.streamId = "abc";
        hosted.initiation.streamAddress = "abc/seg.m4s";
        hosted.subtitleTracks = {2};
        return hosted;
    }
    std::optional<std::string> ServerUrl() override { return url; }
    std::string CurrentStreamFastHash() override { return startedFastHash; }
    mango::TranscodeStatus CurrentTranscode() override
    {
        return {.fastHash = startedFastHash, .percent = transcodePercent.load(), .error = {}};
    }
    std::string Title(const std::string&) override { return "Movie"; }
    void PlaybackStarted(const std::string&, double) override {}
    void ReportPlaybackProgress(const std::string&, double positionSeconds, int, int) override
    {
        lastPosition = positionSeconds;
    }
    void PlaybackEnded(const std::string& fastHash) override { ended = fastHash == "movie1"; }

    std::string url;
    std::string startedFastHash;
    bool startedWithEncoding = false;
    TrackRequest startedTracks;
    std::atomic<double> lastPosition{-1.0};
    std::atomic<double> transcodePercent{40.0};
    std::atomic<bool> ended{false};
};

bool HasEntry(const LiveDevice& device, const std::string& entryId)
{
    return device.remote.GetLibraryEntriesJson().contains("\"" + entryId + "\"");
}

// Makes client a client of host and waits for their link; false (with a
// Check) if it fails.
bool PairLive(LiveDevice& host, LiveDevice& client)
{
    if (!JoinLive(host, client))
    {
        return false;
    }
    const auto hostId = host.State().deviceId;
    const auto clientId = client.State().deviceId;
    const auto linkOf = [](const std::vector<mango::RemotePeerState>& peers, const std::string& deviceId) {
        const auto found = std::ranges::find(peers, deviceId, &mango::RemotePeerState::deviceId);
        return found != peers.end() ? found->link : std::string();
    };
    const bool connected = WaitFor(
        [&] {
            return linkOf(host.State().clients, clientId) == "connected" &&
                   linkOf(client.State().sources, hostId) == "connected";
        },
        std::chrono::seconds(60));
    Check(connected, "paired and connected");
    return connected;
}

void LiveRemotePlayback()
{
    if (LiveSignalingUrl().empty())
    {
        return;  // Needs the internet: opt in.
    }

    FakeHttpServer server;
    LiveDevice a("live-play-a");
    LiveDevice b("live-play-b");
    const auto host = std::make_shared<FakeHost>(server.Url());
    a.remote.Core().SetHost(host);
    if (!PairLive(a, b))
    {
        return;
    }

    const auto aId = a.State().deviceId;
    const auto entryId = RemoteEntryId::Make(aId, "movie1");
    Check(WaitFor([&] { return HasEntry(b, entryId); }, std::chrono::seconds(20)), "B loads A's library");
    Check(b.State().sources[0].library == "ready" && b.State().sources[0].libraryCount == 1, "library status");

    const auto result = b.remote.Core().Playback().Initiate(entryId, TrackRequest{3, std::nullopt}, PlaylistType::DASH, "");
    Check(result.success && result.streamAddress == "abc/seg.m4s", "A starts the stream for B");
    Check(host->startedTracks == TrackRequest{3, std::nullopt}, "only B's chosen track reaches A");
    Check(result.audioTrackId == 3 && result.subtitleTrackId == 2, "A says which tracks play");
    Check(host->startedFastHash == "movie1" && host->startedWithEncoding, "A got B's full target");

    const auto client = b.remote.Core().FindClient(aId);
    const auto body = client ? FetchWhole(*client, "/stream/abc/seg.m4s", 16 * 1024 * 1024, std::chrono::seconds(60))
                             : std::nullopt;
    Check(body && body->body == server.Body(), "the stream crosses the link");

    const auto transcode = [&] {
        const auto stream = b.remote.Core().Playback().Find(entryId);
        return stream ? stream->transcode : std::nullopt;
    };
    Check(WaitFor([&] { return transcode() && transcode()->percent == 40.0; }, std::chrono::seconds(5)),
        "A's conversion progress reaches B");
    Check(transcode() && transcode()->fastHash == entryId, "under B's id for the entry");
    host->transcodePercent = 75.0;
    Check(WaitFor([&] { return transcode() && transcode()->percent == 75.0; }, std::chrono::seconds(5)),
        "and keeps coming");

    b.remote.Core().Playback().ReportProgress(entryId, 42.0, 1, 2);
    Check(WaitFor([&] { return host->lastPosition == 42.0; }, std::chrono::seconds(5)), "progress reaches A");

    b.remote.Core().Playback().Ended(entryId);
    Check(WaitFor([&] { return host->ended.load(); }, std::chrono::seconds(5)), "the end reaches A");

    b.remote.RemoveSource(aId);
    a.remote.RemoveClient(b.State().deviceId);
    Check(WaitFor([&] { return !HasEntry(b, entryId); }, std::chrono::seconds(5)), "removing A drops its library");
}

// A and B pair both ways: each is the other's client and source, with a
// link per direction, and each plays the other's video at the same time.
// Removing one direction leaves the other playing.
void LiveTwoWay()
{
    if (LiveSignalingUrl().empty())
    {
        return;  // Needs the internet: opt in.
    }

    FakeHttpServer serverA;
    FakeHttpServer serverB;
    LiveDevice a("live-two-a");
    LiveDevice b("live-two-b");
    const auto hostA = std::make_shared<FakeHost>(serverA.Url());
    const auto hostB = std::make_shared<FakeHost>(serverB.Url());
    a.remote.Core().SetHost(hostA);
    b.remote.Core().SetHost(hostB);
    if (!PairLive(a, b) || !PairLive(b, a))
    {
        return;
    }

    const auto aId = a.State().deviceId;
    const auto bId = b.State().deviceId;
    Check(a.State().clients.size() == 1 && a.State().sources.size() == 1, "A has B in both lists");
    Check(b.State().clients.size() == 1 && b.State().sources.size() == 1, "B has A in both lists");

    const auto onA = RemoteEntryId::Make(aId, "movie1");
    const auto onB = RemoteEntryId::Make(bId, "movie1");
    Check(WaitFor([&] { return HasEntry(b, onA) && HasEntry(a, onB); }, std::chrono::seconds(20)),
        "each loads the other's library");

    const auto bPlaysA = b.remote.Core().Playback().Initiate(onA, TrackRequest{1, 2}, PlaylistType::DASH, "");
    const auto aPlaysB = a.remote.Core().Playback().Initiate(onB, TrackRequest{1, 2}, PlaylistType::DASH, "");
    Check(bPlaysA.success && aPlaysB.success, "both streams start");
    Check(hostA->startedFastHash == "movie1" && hostB->startedFastHash == "movie1", "each hosts one");

    const auto fetch = [](LiveDevice& device, const std::string& sourceId) {
        const auto client = device.remote.Core().FindClient(sourceId);
        return client ? FetchWhole(*client, "/stream/abc/seg.m4s", 16 * 1024 * 1024, std::chrono::seconds(60))
                      : std::nullopt;
    };
    auto fromA = std::async(std::launch::async, [&] { return fetch(b, aId); });
    auto fromB = std::async(std::launch::async, [&] { return fetch(a, bId); });
    const auto bodyA = fromA.get();
    const auto bodyB = fromB.get();
    Check(bodyA && bodyA->body == serverA.Body(), "A's stream reaches B");
    Check(bodyB && bodyB->body == serverB.Body(), "B's stream reaches A, at the same time");

    a.remote.RemoveClient(bId);
    Check(WaitFor([&] { return a.State().clients.empty(); }, std::chrono::seconds(5)), "A removes B as a client");
    Check(WaitFor([&] { return b.State().sources.empty() || b.State().sources[0].link != "connected"; },
              std::chrono::seconds(10)),
        "B's link to A closes");
    Check(a.State().sources.size() == 1 && a.State().sources[0].link == "connected", "A's link to B stays up");
    const auto again = fetch(a, bId);
    Check(again && again->body == serverB.Body(), "A still plays B's video");

    b.remote.RemoveClient(aId);
    a.remote.RemoveSource(bId);
    b.remote.RemoveSource(aId);
}

}  // namespace

std::vector<TestCase> RemoteTestCases()
{
    return {
        {"remote: SHA-256 and HMAC vectors", HashVectors},
        {"remote: base64url", Base64Url},
        {"remote: Ed25519 RFC 8032 vector", Ed25519Vector},
        {"remote: device identity", IdentityBasics},
        {"remote: device identity persists", IdentityPersists},
        {"remote: key store, new key goes to the system store", KeyStoreNewKeyGoesToPrimary},
        {"remote: key store, file key migrates", KeyStoreMigratesFileKey},
        {"remote: key store, file when the system store can't save", KeyStoreKeepsFileWhenPrimaryCantSave},
        {"remote: key store, unreadable system store", KeyStoreUnreadablePrimary},
        {"remote: key store, leftover file removed", KeyStoreRemovesLeftoverFile},
        {"remote: invite links", InviteLinks},
        {"remote: pairing proofs", PairingProofs},
        {"remote: signed SDP", SignedSdp},
        {"remote: hello round trip", HelloRoundTrip},
        {"remote: hello example", HelloExampleUsesBuiltInDefaults},
        {"remote: wire format is pinned", WireFormatIsPinned},
        {"remote: signaling router", RouterRoutesByKind},
        {"remote: peer store", PeerStorePersists},
        {"remote: PeerLink loopback over WebRTC", PeerLinkLoopback},
        {"remote: forwarding over WebRTC", ForwardingOverWebRtc},
        {"remote: streaming allowed", StreamingAllowedState},
        {"remote: live pairing (MANGO_LIVE_SIGNALING=<url>)", LivePairAndConnect},
        {"remote: live playback (MANGO_LIVE_SIGNALING=<url>)", LiveRemotePlayback},
        {"remote: live both ways (MANGO_LIVE_SIGNALING=<url>)", LiveTwoWay},
    };
}
