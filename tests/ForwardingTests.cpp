// Remote streaming, steps 5-8: the forwarding protocol over an in-process
// loopback, B's /remote route, the target B sends and A reads, and the
// remote library's sections.

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>

#include <glaze/glaze.hpp>
#include <httplib.h>

#include "FakeHttpServer.hpp"
#include "TestCheck.hpp"
#include "remote/streaming/ControlMessages.hpp"
#include "remote/forwarding/ForwardMessages.hpp"
#include "remote/forwarding/ForwardingAdapter.hpp"
#include "remote/forwarding/ForwardingClient.hpp"
#include "remote/link/HelloMessage.hpp"
#include "remote/streaming/RemoteEntryId.hpp"
#include "remote/streaming/RemoteHost.hpp"
#include "remote/streaming/RemotePlayback.hpp"
#include "remote/streaming/RemoteSource.hpp"
#include "remote/forwarding/RemoteLinkHandler.hpp"
#include "remote/streaming/RemoteTarget.hpp"
#include "remote/signaling/SignalingMessages.hpp"
#include "session/LibrarySections.hpp"
#include "session/SessionThread.hpp"

using namespace mango::remote;
using namespace std::chrono_literals;

namespace
{

bool WaitUntil(const std::function<bool()>& condition, std::chrono::milliseconds timeout)
{
    const auto end = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < end)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return condition();
}

// ---- Frames -----------------------------------------------------------------

void FrameRoundTrip()
{
    const auto request = forward::EncodeRequest(
        0x01020304, {.method = "GET", .path = "/stream/a/b.m4s", .headers = {{"Range", "bytes=0-9"}}});
    const auto frame = forward::Decode(request);
    Check(frame && frame->type == forward::FrameType::Request && frame->exchangeId == 0x01020304, "request header");
    const auto read = frame ? forward::ReadRequest(*frame) : std::nullopt;
    Check(read && read->path == "/stream/a/b.m4s" && forward::FindHeader(read->headers, "range") == "bytes=0-9",
        "request body, header lookup ignores case");

    const std::string binary("\0\1\2\xff", 4);
    const auto chunk = forward::Decode(forward::Encode(forward::FrameType::Chunk, 7, binary));
    Check(chunk && chunk->body == binary, "chunk bytes are raw");

    const auto ack = forward::Decode(forward::EncodeAck(9, 5'000'000'000ULL));
    Check(ack && forward::ReadAck(*ack) == 5'000'000'000ULL, "ack count");

    Check(!forward::Decode("\x01\x00"), "too short");
    Check(!forward::Decode(std::string("\x09\0\0\0\1", 5)), "unknown type");
}

void ForwardedPaths()
{
    Check(ForwardingAdapter::IsServerPath("/stream/abc/index.mpd"), "stream file");
    Check(ForwardingAdapter::IsServerPath("/cover/abc"), "cover");
    Check(!ForwardingAdapter::IsServerPath("/stream/abc/../x"), "no ..");
    Check(!ForwardingAdapter::IsServerPath("/stream/abc/sub/x"), "no subfolders");
    Check(!ForwardingAdapter::IsServerPath("/library"), "library is local, not the server's");
    Check(!ForwardingAdapter::IsServerPath("/remote/x/stream/a/b"), "no forwarding loops");
}

// ---- Loopback -----------------------------------------------------------------

// Frames to the other side, delivered on that side's own thread.
class Loopback final : public LinkTransport
{
   public:
    explicit Loopback(mango::SessionThread& thread) : m_thread(thread) {}

    void Connect(std::function<void(const forward::Frame&)> receiver) { m_receiver = std::move(receiver); }

    bool Send(std::string frame) override
    {
        if (!m_open)
        {
            return false;
        }
        m_thread.Post([this, frame = std::move(frame)] {
            if (const auto decoded = forward::Decode(frame))
            {
                m_receiver(*decoded);
            }
        });
        return true;
    }

    [[nodiscard]] std::size_t BufferedAmount() const override { return 0; }
    [[nodiscard]] bool IsOpen() const override { return m_open; }

    std::atomic<bool> m_open{true};

   private:
    mango::SessionThread& m_thread;
    std::function<void(const forward::Frame&)> m_receiver;
};

// A's adapter in front of a FakeHttpServer, and B's client, joined by a
// loopback.
struct ForwardingFixture
{
    ForwardingFixture()
        : toB(std::make_shared<Loopback>(threadB)),
          toA(std::make_shared<Loopback>(threadA)),
          adapter(toB, {.serverUrl = [this] { return std::optional(server.Url()); },
                           .localRoute = [](const std::string& path) -> std::optional<LocalResponse> {
                               if (path != "/library")
                               {
                                   return std::nullopt;
                               }
                               return LocalResponse{.status = 200, .contentType = "application/json", .body = "[]"};
                           }}),
          client(std::make_shared<ForwardingClient>(toA))
    {
        toB->Connect([this](const forward::Frame& frame) { client->OnFrame(frame); });
        toA->Connect([this](const forward::Frame& frame) { adapter.OnFrame(frame); });
    }

    // No frame is delivered once the adapter and client start going.
    ~ForwardingFixture()
    {
        toA->m_open = false;
        toB->m_open = false;
        threadA.Stop();
        threadB.Stop();
    }

    FakeHttpServer server;
    mango::SessionThread threadA;
    mango::SessionThread threadB;
    std::shared_ptr<Loopback> toB;
    std::shared_ptr<Loopback> toA;
    ForwardingAdapter adapter;
    std::shared_ptr<ForwardingClient> client;
};

void ForwardWholeBody()
{
    ForwardingFixture f;
    const auto response = FetchWhole(*f.client, "/stream/abc/seg.m4s", 16 * 1024 * 1024, 10s);
    Check(response && response->head.status == 200, "200 through the link");
    Check(response && response->body == f.server.Body(), "5 MB body arrives intact (past the ack window)");
    Check(response && forward::FindHeader(response->head.headers, "Content-Length") ==
                          std::to_string(FakeHttpServer::kBodySize),
        "Content-Length passes through");
}

void ForwardRange()
{
    ForwardingFixture f;
    const auto exchange =
        f.client->Start({.method = "GET", .path = "/stream/abc/seg.m4s", .headers = {{"Range", "bytes=100-199"}}});
    const auto head = exchange->WaitHead(5s);
    Check(head && head->status == 206, "206 for a range");
    Check(head && forward::FindHeader(head->headers, "Content-Range") ==
                      std::format("bytes 100-199/{}", FakeHttpServer::kBodySize),
        "Content-Range passes through");

    std::string body;
    std::string chunk;
    while (exchange->Next(chunk, 5s) == ForwardExchange::Read::Data)
    {
        body += chunk;
    }
    Check(body == f.server.Body().substr(100, 100), "the range's bytes");
}

void ForwardErrorsAndRefusals()
{
    ForwardingFixture f;
    const auto missing = f.client->Start({.method = "GET", .path = "/stream/abc/missing", .headers = {}});
    const auto missingHead = missing->WaitHead(5s);
    Check(missingHead && missingHead->status == 404, "A's 404 passes through");

    const auto refused = f.client->Start({.method = "GET", .path = "/settings.json", .headers = {}});
    const auto refusedHead = refused->WaitHead(5s);
    Check(!refusedHead && refusedHead.error().status == 404, "other paths are refused");

    const auto post = f.client->Start({.method = "POST", .path = "/stream/abc/seg.m4s", .headers = {}});
    const auto postHead = post->WaitHead(5s);
    Check(!postHead && postHead.error().status == 405, "only GET and HEAD");

    const auto library = FetchWhole(*f.client, "/library", 1024, 5s);
    Check(library && library->body == "[]", "a local route answers in process");
}

void ForwardCancel()
{
    ForwardingFixture f;
    {
        const auto exchange = f.client->Start({.method = "GET", .path = "/stream/abc/slow", .headers = {}});
        std::string chunk;
        Check(exchange->WaitHead(5s).has_value() && exchange->Next(chunk, 5s) == ForwardExchange::Read::Data,
            "the slow response starts");
    }  // Dropping the exchange cancels it.
    Check(WaitUntil([&] { return f.server.SlowEnded(); }, 8s), "a cancel stops A's request to its server");
}

void ForwardLinkDown()
{
    ForwardingFixture f;
    const auto exchange = f.client->Start({.method = "GET", .path = "/stream/abc/slow", .headers = {}});
    Check(exchange->WaitHead(5s).has_value(), "started");
    f.client->FailAll();
    std::string chunk;
    while (exchange->Next(chunk, 5s) == ForwardExchange::Read::Data)
    {
    }
    Check(exchange->Next(chunk, 1s) == ForwardExchange::Read::Failed, "a dropped link fails the response");
    Check(!f.client->Start({.method = "GET", .path = "/x", .headers = {}}), "no new requests on a dropped link");
}

// ---- B's /remote route -----------------------------------------------------

void RemoteRouteServesTheTv()
{
    ForwardingFixture f;
    const std::string peerId(64, 'a');
    const RemoteLinkHandler handler([&](const std::string& routePeerId) { return routePeerId == peerId ? f.client : nullptr; });

    httplib::Server relay;
    relay.Get(RemoteLinkHandler::kRoutePattern,
        [&handler](const httplib::Request& req, httplib::Response& res) { handler.Handle(req, res); });
    const int port = relay.bind_to_any_port("127.0.0.1");
    std::thread thread([&] { relay.listen_after_bind(); });
    relay.wait_until_ready();

    httplib::Client tv("127.0.0.1", port);
    const auto base = "/remote/" + peerId;

    const auto whole = tv.Get(base + "/stream/abc/seg.m4s");
    Check(whole && whole->status == 200 && whole->body == f.server.Body(), "the TV gets the whole segment");

    const auto range = tv.Get(base + "/stream/abc/seg.m4s", httplib::Headers{{"Range", "bytes=1000-1999"}});
    Check(range && range->status == 206 && range->body == f.server.Body().substr(1000, 1000), "a range request");
    Check(range && range->get_header_value("Content-Range") ==
                       std::format("bytes 1000-1999/{}", FakeHttpServer::kBodySize),
        "the TV sees A's Content-Range");

    const auto tail = tv.Get(base + "/stream/abc/seg.m4s", httplib::Headers{{"Range", "bytes=5000000-"}});
    Check(tail && tail->status == 206 && tail->body == f.server.Body().substr(5000000), "an open-ended range");

    const auto head = tv.Head(base + "/stream/abc/seg.m4s");
    Check(head && head->status == 200 &&
              head->get_header_value("Content-Length") == std::to_string(FakeHttpServer::kBodySize),
        "HEAD");

    const auto missing = tv.Get(base + "/stream/abc/missing");
    Check(missing && missing->status == 404, "A's 404");

    const auto offline = tv.Get("/remote/" + std::string(64, 'b') + "/stream/abc/seg.m4s");
    Check(offline && offline->status == 503, "503 for a peer that isn't connected");

    const auto library = tv.Get(base + "/library");
    Check(library && library->status == 404, "A's library isn't reachable from B's network");

    relay.stop();
    thread.join();
}

// ---- Target ----------------------------------------------------------------

std::string BuiltInExampleJson() { return glz::write_json(Hello::BuiltInInitiateExample()).value_or("{}"); }

bool Has(const std::vector<std::string>& list, const std::string& item)
{
    return std::ranges::find(list, item) != list.end();
}

void TargetFitsTheSameVersion()
{
    const auto mine = Hello::BuiltInInitiateExample().target;
    const auto fitted = RemoteTarget::FitToExample(mine, BuiltInExampleJson());
    Check(fitted.notApplied.empty(), "nothing left out between equal versions");

    const auto target = RemoteTarget::Read(fitted.json);
    Check(target.has_value(), "A reads it");
    if (target)
    {
        Check(target->preset == EncodingPresetId::FullHD1080 && target->encoding.has_value(), "preset and encoding");
        Check(target->profile.sidecarSubtitles == StreamProfiles::BuiltInProfile(PlaylistType::DASH).sidecarSubtitles,
            "playlist settings come from the built-in profile");
        Check(target->encoding && target->encoding->Preset(EncodingPresetId::FullHD1080).videoBitrate ==
                                      mine.encoding.videoBitrate,
            "B's bitrate for its preset");
    }
}

// An older A: no rotation, no hdrSupported, only h264/aac, no 4k.
constexpr std::string_view kOlderExample = R"({
  "target": {
    "playlist": "dash", "preset": "1080p", "mode": "automatic",
    "videoCapabilities": [ { "codec": "h264", "maxWidth": 1920, "maxHeight": 1080, "maxFps": 60 } ],
    "audioCapabilities": [ { "codec": "aac", "maxChannels": 6, "maxSampleRate": 48000 } ],
    "encoding": { "videoBitrate": "6000k", "audioBitrate": "256k", "videoPixFmt": "yuv420p", "audioCodec": "aac" },
    "toneMapping": "auto"
  },
  "accepts": { "playlist": ["dash"], "preset": ["720p", "1080p"], "mode": ["automatic"],
    "videoCodecs": ["h264"], "audioCodecs": ["aac"], "videoPixFmt": ["yuv420p"], "audioCodec": ["aac"] }
})";

void TargetFitsAnOlderPeer()
{
    auto mine = Hello::BuiltInInitiateExample().target;
    mine.preset = "4k";
    mine.mode = "forceDecode";
    mine.rotationSupported = true;
    mine.videoCapabilities = {{.codec = "hevc", .maxWidth = 3840, .maxHeight = 2160, .maxFps = 60, .hdrSupported = true},
        {.codec = "h264", .maxWidth = 3840, .maxHeight = 2160, .maxFps = 30, .hdrSupported = false}};

    const auto fitted = RemoteTarget::FitToExample(mine, kOlderExample);
    Check(Has(fitted.notApplied, "hevc") && Has(fitted.notApplied, "4k") && Has(fitted.notApplied, "forceDecode"),
        "dropped codec and replaced values are listed");
    Check(Has(fitted.notApplied, "rotationSupported") && Has(fitted.notApplied, "hdrSupported"),
        "fields A doesn't know are listed");

    glz::generic json;
    Check(!glz::read_json(json, fitted.json), "valid JSON");
    Check(json["preset"].get<std::string>() == "1080p", "the largest preset A has that isn't bigger");
    Check(json["mode"].get<std::string>() == "automatic", "an unknown mode becomes automatic");
    Check(!json.contains("rotationSupported"), "left out");
    Check(json["toneMapping"].get<std::string>() == "auto", "A's own field keeps its example value");
    const auto& video = json["videoCapabilities"].get<glz::generic::array_t>();
    Check(video.size() == 1 && video[0]["codec"].get<std::string>() == "h264" && !video[0].contains("hdrSupported") &&
              video[0]["maxWidth"].get<double>() == 3840,
        "entries keep only the example's subfields, with B's values");
}

void TargetReadIsStrict()
{
    const auto valid = RemoteTarget::FitToExample(Hello::BuiltInInitiateExample().target, BuiltInExampleJson()).json;
    const auto with = [&](const std::string& from, const std::string& to) {
        auto text = valid;
        const auto at = text.find(from);
        return at == std::string::npos ? std::string() : text.replace(at, from.size(), to);
    };

    Check(!RemoteTarget::Read(with("\"playlist\"", "\"bogus\":1,\"playlist\"")), "an unknown key");
    Check(!RemoteTarget::Read(with("\"mode\":\"automatic\",", "")), "a missing field");
    Check(!RemoteTarget::Read(with("\"h264\"", "\"divx\"")), "an unknown codec");
    Check(!RemoteTarget::Read(with("\"maxWidth\":1920", "\"maxWidth\":100000")), "an absurd size");
    Check(!RemoteTarget::Read(with("\"6000k\"", "\"lots\"")), "a bitrate that doesn't parse");
    Check(!RemoteTarget::Read(with("\"yuv420p\"", "\"rgb24\"")), "a pixel format A can't encode");
    Check(RemoteTarget::Read(valid).has_value(), "the valid one still reads");
}

// Every message on the channel is read as a signaling::Envelope first,
// which reads only "type": a control message must still read as one.
void ControlMessagesReadAsEnvelopes()
{
    InitiateRequest request;
    request.requestId = 7;
    request.fastHash = "abc";
    request.target.str = "{}";
    const auto text = glz::write_json(request).value_or("");
    const auto envelope = signaling::Read<signaling::Envelope>(text);
    Check(envelope && envelope->type == "initiate", "initiate reads as an envelope");

    InitiateResult result;
    result.requestId = 7;
    result.success = true;
    const auto answer = glz::write_json(result).value_or("");
    Check(signaling::Read<signaling::Envelope>(answer).has_value(), "initiateResult reads as an envelope");
    const auto back = signaling::Read<InitiateResult>(answer);
    Check(back && back->requestId == 7 && back->success, "round trip");
}

// Only the tracks B's user chose go over the wire; an absent one A picks.
void InitiateTracksAreOptional()
{
    InitiateRequest request;
    request.fastHash = "abc";
    request.target.str = "{}";
    request.subtitleTrackId = -1;
    const auto text = glz::write_json(request).value_or("");
    Check(!text.contains("audioTrackId") && text.contains(R"("subtitleTrackId":-1)"), "an unchosen kind is left out");

    const auto back = signaling::Read<InitiateRequest>(text);
    Check(back && !back->audioTrackId && back->subtitleTrackId == -1, "read back: absent stays absent, off stays off");

    InitiateResult result;
    result.audioTrackId = 2;
    result.subtitleTrackId = 5;
    const auto answer = signaling::Read<InitiateResult>(glz::write_json(result).value_or(""));
    Check(answer && answer->audioTrackId == 2 && answer->subtitleTrackId == 5, "A says which tracks play");
}

// ---- Conversion status -------------------------------------------------------

// A's engine: one stream, converting at percent.
struct ConvertingHost final : RemoteHost
{
    std::vector<mango::LibraryEntry> SharedLibrary() override { return {}; }
    std::optional<std::filesystem::path> CoverFile(const std::string&) override { return std::nullopt; }
    HostedStream StartStream(const std::string& fastHash, const TrackRequest&, const StreamTarget&) override
    {
        {
            std::lock_guard lock(mutex);
            current = fastHash;
        }
        HostedStream hosted;
        hosted.initiation.success = true;
        hosted.initiation.streamId = "s1";
        return hosted;
    }
    std::optional<std::string> ServerUrl() override { return "http://127.0.0.1:1"; }
    std::string CurrentStreamFastHash() override
    {
        std::lock_guard lock(mutex);
        return current;
    }
    mango::TranscodeStatus CurrentTranscode() override
    {
        return {.fastHash = CurrentStreamFastHash(), .percent = percent.load(), .error = {}};
    }
    std::string Title(const std::string& fastHash) override { return fastHash; }
    void PlaybackStarted(const std::string&, double) override {}
    void ReportPlaybackProgress(const std::string&, double, int, int) override {}
    void PlaybackEnded(const std::string&) override {}

    std::mutex mutex;
    std::string current;
    std::atomic<double> percent{40.0};
};

// A's RemoteSource and B's RemotePlayback back to back over two threads,
// with a link that can go down.
void TranscodeStatusReachesB()
{
    mango::SessionThread threadA;
    mango::SessionThread threadB;
    std::atomic<bool> linkUp{true};
    const std::string aId = "peer-a";
    const std::string bId = "peer-b";

    std::unique_ptr<RemotePlayback> playback;
    RemoteSource source(threadA,
        {.send =
                [&](const std::string&, const std::string& text) {
                    if (!linkUp)
                    {
                        return;
                    }
                    threadB.Post([&, text] {
                        const auto type = signaling::Read<signaling::Envelope>(text)->type;
                        type == "initiateResult" ? playback->HandleResult(aId, text)
                                                 : playback->HandleTranscodeStatus(aId, text);
                    });
                },
            .onNotice = [](const std::string&, const std::string&, const std::string&) {}});
    const auto host = std::make_shared<ConvertingHost>();
    source.SetHost(host);

    playback = std::make_unique<RemotePlayback>(threadB,
        RemotePlayback::Callbacks{.example = [](const std::string&) { return std::optional(BuiltInExampleJson()); },
            .send =
                [&](const std::string&, const std::string& text) {
                    const auto type = signaling::Read<signaling::Envelope>(text)->type;
                    threadA.Post([&, type, text] { (void)source.HandleMessage(bId, type, text); });
                    return true;
                },
            .onNotApplied = [](const std::string&, const std::vector<std::string>&) {},
            .onEnded = [](const std::string&) {}});

    const auto entryId = RemoteEntryId::Make(aId, "movie1");
    const auto transcode = [&] {
        const auto stream = playback->Find(entryId);
        return stream ? stream->transcode : std::nullopt;
    };

    const auto result = playback->Initiate(entryId, {}, PlaylistType::DASH, "");
    Check(result.success, "A starts the stream");
    Check(WaitUntil([&] { return transcode() && transcode()->percent == 40.0; }, 3s), "the first status comes");
    Check(transcode() && transcode()->fastHash == entryId, "under B's id for the entry");

    host->percent = 75.0;
    Check(WaitUntil([&] { return transcode() && transcode()->percent == 75.0; }, 3s), "and keeps coming");

    linkUp = false;
    threadB.Post([&] { playback->OnLinkDown(aId); });
    Check(WaitUntil([&] { return !transcode(); }, 3s), "a dropped link forgets it");
    std::this_thread::sleep_for(1500ms);
    Check(!transcode(), "nothing while it's down");

    host->percent = 90.0;
    linkUp = true;
    Check(WaitUntil([&] { return transcode() && transcode()->percent == 90.0; }, 3s), "back after a reconnect");

    threadA.Stop();
    threadB.Stop();
}

// ---- Entry ids and sections ------------------------------------------------

void RemoteEntryIds()
{
    const auto entryId = RemoteEntryId::Make("peer", "hash");
    const auto parts = RemoteEntryId::Parse(entryId);
    Check(parts && parts->peerId == "peer" && parts->fastHash == "hash", "round trip");
    Check(!RemoteEntryId::Parse("abcdef") && !RemoteEntryId::Parse("remote:peer") && !RemoteEntryId::Parse("remote::x"),
        "malformed ids");
}

void RemoteSections()
{
    mango::LibraryEntry local;
    local.fastHash = "local";
    local.title = "Local";
    mango::LibraryEntry remoteB;
    remoteB.fastHash = RemoteEntryId::Make("p1", "b");
    remoteB.title = "B";
    remoteB.addedManually = false;
    remoteB.remotePeerId = "p1";
    remoteB.remotePeerName = "Office";
    auto remoteA = remoteB;
    remoteA.fastHash = RemoteEntryId::Make("p1", "a");
    remoteA.title = "A";

    const auto sections =
        mango::LibrarySections::Build({local, remoteB, remoteA}, {}, mango::LibrarySections::SortOrder::Name);
    Check(sections.size() == 2 && sections[0].sectionId == "manual" && sections[0].fastHashes.size() == 1,
        "remote entries aren't in Added Manually");
    Check(sections.size() == 2 && sections[1].sectionId == "remote:p1" && sections[1].kind == "remote" &&
              sections[1].breadcrumb == std::vector<std::string>{"Office"} &&
              sections[1].fastHashes == std::vector<std::string>{remoteA.fastHash, remoteB.fastHash},
        "one sorted section per peer");
}

// A's folder names group B's view of its library, one level deep.
void RemoteFolderSections()
{
    const auto entry = [](const std::string& hash, const std::string& title, std::vector<std::string> folders) {
        mango::LibraryEntry remote;
        remote.fastHash = RemoteEntryId::Make("p1", hash);
        remote.title = title;
        remote.addedManually = false;
        remote.remotePeerId = "p1";
        remote.remotePeerName = "Office";
        remote.folderNames = std::move(folders);
        return remote;
    };
    const auto loose = entry("a", "Loose", {});
    const auto heat = entry("b", "Heat", {"Movies"});
    const auto both = entry("c", "Both", {"Series", "Movies"});

    const auto sections =
        mango::LibrarySections::Build({heat, loose, both}, {}, mango::LibrarySections::SortOrder::Name);
    Check(sections.size() == 3, "the device and a section per folder name");
    if (sections.size() != 3)
    {
        return;
    }
    Check(sections[0].sectionId == "remote:p1" && sections[0].kind == "remote" &&
              sections[0].fastHashes == std::vector<std::string>{loose.fastHash},
        "videos in no folder stay in the device's section");
    Check(sections[1].sectionId == "remote:p1/Movies" && sections[1].kind == "folder" &&
              sections[1].parentSectionId == "remote:p1" &&
              sections[1].breadcrumb == std::vector<std::string>{"Office", "Movies"} &&
              sections[1].fastHashes == std::vector<std::string>{both.fastHash, heat.fastHash},
        "a folder under its device, sorted");
    Check(sections[2].sectionId == "remote:p1/Series" && sections[2].fastHashes == std::vector<std::string>{both.fastHash},
        "a video in two folders is in each");
}

}  // namespace

std::vector<TestCase> ForwardingTestCases()
{
    return {
        {"forwarding: frames", FrameRoundTrip},
        {"forwarding: forwarded paths", ForwardedPaths},
        {"forwarding: whole body over loopback", ForwardWholeBody},
        {"forwarding: range over loopback", ForwardRange},
        {"forwarding: errors and refusals", ForwardErrorsAndRefusals},
        {"forwarding: cancel reaches A's server", ForwardCancel},
        {"forwarding: link down", ForwardLinkDown},
        {"forwarding: B's /remote route", RemoteRouteServesTheTv},
        {"remote target: same version", TargetFitsTheSameVersion},
        {"remote target: older peer", TargetFitsAnOlderPeer},
        {"remote target: strict read on A", TargetReadIsStrict},
        {"remote control: messages read as envelopes", ControlMessagesReadAsEnvelopes},
        {"remote control: initiate tracks are optional", InitiateTracksAreOptional},
        {"remote control: conversion status reaches B", TranscodeStatusReachesB},
        {"remote library: entry ids", RemoteEntryIds},
        {"remote library: sections", RemoteSections},
        {"remote library: folder sections", RemoteFolderSections},
    };
}
