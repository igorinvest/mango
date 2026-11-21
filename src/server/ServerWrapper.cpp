#include "ServerWrapper.hpp"

#include <charconv>
#include <chrono>
#include <ctime>
#include <format>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

#include "../pipeline/library/Library.hpp"
#include "../remote/forwarding/RemoteLinkHandler.hpp"
#include "../pipeline/stream/StreamManager.hpp"
#include "../pipeline/stream/subtitles/SubtitleExtractor.hpp"
#include "NetworkUtils.hpp"
#include "RequestGuard.hpp"
#include "StreamActivity.hpp"
#include "../log/Log.hpp"

namespace
{
// The Chromecast is told about every text subtitle track at LOAD and fetches
// them all right away, but a 404 for any one of them turns subtitles off for
// the whole session — so a request for a track still being extracted waits
// for the extraction instead. Only for the file currently streaming: starting
// an extraction for any other file would cancel the current one's.
void WaitForSubtitleTrack(const FileRecord &fileRecord, const std::string &filename)
{
    const auto stem = std::filesystem::path(filename).stem().string();
    int subtitleTrackIndex = 0;
    const auto [end, error] =
        std::from_chars(stem.data(), stem.data() + stem.size(), subtitleTrackIndex);
    if (error != std::errc() || end != stem.data() + stem.size())
    {
        return;
    }

    SubtitleExtractor::WaitUntilExtracted(fileRecord, subtitleTrackIndex);
}

// "2026-10-24 13:31:56.755123" — ffmpeg's "-loglevel datetime"
// format plus microseconds, so reads.log lines up with ffmpeg.log.
std::string Timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif

    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count() %
        1'000'000;
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:06}", local.tm_year + 1900,
        local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min, local.tm_sec, micros);
}

// Appends one line to reads.log next to ffmpeg.log.
void LogRead(const std::filesystem::path &filepath, const std::string &message)
{
    const auto timestamp = Timestamp();

    static std::mutex mutex;
    std::lock_guard lock(mutex);
    std::ofstream log(filepath.parent_path() / "reads.log", std::ios::app);
    log << timestamp << ' ' << filepath.filename().string() << ' ' << message << '\n';
}

// Reads a manifest/.vtt whole, logging when it was open and for how long.
std::optional<std::string> ReadWholeFile(const std::filesystem::path &filepath, const std::string &client)
{
    LogRead(filepath, "open  (" + client + ")");
    const auto start = std::chrono::steady_clock::now();

    std::ifstream file(filepath, std::ios::binary);
    if (!file)
    {
        LogRead(filepath, "open FAILED (" + client + ")");
        return std::nullopt;
    }

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    const auto heldFor = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start);
    LogRead(filepath, std::format("close ({}, {} bytes, held {} us)", client, content.size(),
                          heldFor.count()));
    return content;
}

// A library file's cover image, with the MIME type to serve it as.
struct CoverContent
{
    std::string bytes;
    std::string mimeType;
};

// The current stream's cover; nothing for any other ID.
std::optional<CoverContent> ReadCover(const std::string &streamId, const std::string &client)
{
    const auto stream = StreamManager::GetInstance().FindStream(streamId);
    const auto fileRecord = stream ? Library::GetInstance().GetCachedFile(stream->fastHash) : std::nullopt;
    if (!fileRecord || !fileRecord->coverFileName)
    {
        return std::nullopt;
    }

    const auto filepath = fileRecord->getMetadataFolder() / *fileRecord->coverFileName;
    auto bytes = ReadWholeFile(filepath, client);
    if (!bytes)
    {
        return std::nullopt;
    }

    const auto mimeType = filepath.extension() == ".png" ? "image/png" : "image/jpeg";
    return CoverContent{std::move(*bytes), mimeType};
}

// A manifest written in place (see FFmpegArgumentFormatter::BuildDashOptions)
// can be read mid-write; a complete .mpd always ends with its closing tag.
bool IsCompleteManifest(const std::string &filename, std::string_view content)
{
    if (!filename.ends_with(".mpd"))
    {
        return true;
    }

    const auto last = content.find_last_not_of(" \t\r\n");
    return last != std::string_view::npos && content.substr(0, last + 1).ends_with("</MPD>");
}

// Answers a cut-off read with the last complete copy of that manifest served.
// One slot is enough: only one stream converts at a time.
std::optional<std::string> WithLastCompleteFallback(
    const std::filesystem::path &filepath, std::optional<std::string> content)
{
    static std::mutex mutex;
    static std::filesystem::path lastPath;
    static std::string lastContent;

    std::lock_guard lock(mutex);
    if (content && IsCompleteManifest(filepath.filename().string(), *content))
    {
        lastPath = filepath;
        lastContent = *content;
        return content;
    }

    if (content && lastPath == filepath)
    {
        LogRead(filepath, std::format("cut off at {} bytes - served last complete copy", content->size()));
        return lastContent;
    }
    return std::nullopt;
}

// A file of the stream a /stream/<streamid>/<filename> request names.
struct StreamFile
{
    FileRecord fileRecord;
    std::filesystem::path path;
};

// nullopt unless streamid is the current stream's (see StreamId) and
// filename a plain name (no "..", separators or drive prefixes). The ID is
// only compared, never joined onto a path.
std::optional<StreamFile> ResolveStreamFile(const std::string &streamId, const std::string &filename)
{
    if (!RequestGuard::IsSafePathSegment(filename))
    {
        return std::nullopt;
    }

    auto fileRecord = StreamManager::GetInstance().FindStream(streamId);
    if (!fileRecord)
    {
        return std::nullopt;
    }

    auto path = fileRecord->getStreamFolder() / filename;
    return StreamFile{std::move(*fileRecord), std::move(path)};
}

bool IsRegularFile(const std::filesystem::path &filepath)
{
    std::error_code error;
    return std::filesystem::is_regular_file(filepath, error);
}

void Reject(httplib::Response &res, int status, std::string_view error)
{
    res.status = status;
    res.set_content(std::format(R"({{"error":"{}"}})", error), "application/json");
}

// Stops DNS rebinding: a page on another site that resolves its own domain
// name to this machine can't read the responses.
bool IsRequestAllowed(const httplib::Request &req, httplib::Response &res)
{
    const auto host = req.get_header_value("Host");
    if (!RequestGuard::IsAllowedHost(host))
    {
        Log::Warning(LogCategory::Server, "rejected {} {} from {}: Host '{}'", req.method,
            req.path, req.remote_addr, host);
        Reject(res, 403, "Host not allowed");
        return false;
    }

    return true;
}

}  // namespace

namespace fs = std::filesystem;

ServerWrapper::~ServerWrapper() { Close(); }

void ServerWrapper::SetRemoteHandler(RemoteHandler handler)
{
    std::lock_guard lock(m_mutex);
    m_remoteHandler = std::move(handler);
}

std::optional<std::string> ServerWrapper::Listen(const std::string &ip)
{
    std::lock_guard lock(m_mutex);
    if (m_server && ip == m_ip)
    {
        return std::format("http://{}:{}", m_ip, m_port);
    }

    if (m_server)
    {
        Log::Info(LogCategory::Server, "LAN address changed from {} to {}", m_ip, ip);
        closeLocked();
    }

    if (ip.empty() || !startListeningLocked(ip, NetworkUtils::FindAvailablePort()))
    {
        return std::nullopt;
    }
    return std::format("http://{}:{}", m_ip, m_port);
}

bool ServerWrapper::startListeningLocked(const std::string &ip, int port)
{
    m_server = std::make_unique<httplib::Server>();
    setupRoutes();

    if (!m_server->bind_to_port(ip, port))
    {
        Log::Error(LogCategory::Server, "can't listen on {}:{}", ip, port);
        m_server.reset();
        return false;
    }

    m_ip = ip;
    m_port = port;
    m_listenThread = std::thread([server = m_server.get()] { server->listen_after_bind(); });

    // stop() is a no-op until the accept loop runs, so a Close() right
    // after this must not get here first.
    m_server->wait_until_ready();
    Log::Info(LogCategory::Server, "listening on http://{}:{}", m_ip, m_port);
    return true;
}

void ServerWrapper::Close()
{
    std::lock_guard lock(m_mutex);
    closeLocked();
}

void ServerWrapper::closeLocked()
{
    if (!m_server)
    {
        return;
    }

    m_server->stop();
    if (m_listenThread.joinable())
    {
        m_listenThread.join();
    }
    m_server.reset();
    Log::Info(LogCategory::Server, "stopped listening on http://{}:{}", m_ip, m_port);
}

mango::ServerStatus ServerWrapper::GetStatus() const
{
    std::lock_guard lock(m_mutex);
    return {m_ip, m_port, false, m_server != nullptr};
}

void ServerWrapper::setupRoutes()
{
    setupLimits();
    setupRequestGuard();
    setupExceptionHandler();
    setupLogger();
    setupCorsRoutes();
    setupStreamRoutes();
    setupCoverRoute();
    setupRemoteRoute();
}

// No route takes a request body. A queue limit stops a burst of
// connections piling up behind busy workers without bound.
void ServerWrapper::setupLimits()
{
    constexpr size_t MaxPayloadBytes = 4 * 1024;
    constexpr size_t MaxQueuedRequests = 64;

    m_server->set_payload_max_length(MaxPayloadBytes);
    m_server->new_task_queue = []
    {
        return new httplib::ThreadPool(
            CPPHTTPLIB_THREAD_POOL_COUNT, CPPHTTPLIB_THREAD_POOL_MAX_COUNT, MaxQueuedRequests);
    };
    m_server->set_default_headers({{"X-Content-Type-Options", "nosniff"}});
}

void ServerWrapper::setupRequestGuard()
{
    m_server->set_pre_routing_handler(
        [](const httplib::Request &req, httplib::Response &res)
        {
            return IsRequestAllowed(req, res) ? httplib::Server::HandlerResponse::Unhandled
                                              : httplib::Server::HandlerResponse::Handled;
        });
}

void ServerWrapper::setupExceptionHandler()
{
    m_server->set_exception_handler(
        [](const httplib::Request &req, httplib::Response &res, std::exception_ptr ep)
        {
            std::string message = "Unknown error";

            try
            {
                std::rethrow_exception(ep);
            }
            catch (const std::exception &e)
            {
                message = e.what();
            }
            catch (...)
            {
            }

            // The details (paths, internals) go to the log, not the client.
            Log::Error(LogCategory::Server, "{} {} failed: {}", req.method, req.path, message);
            Reject(res, 500, "Internal server error");
        });
}

void ServerWrapper::setupLogger()
{
    m_server->set_logger(
        [](const httplib::Request &req, const httplib::Response &res)
        {
            if (!Log::IsEnabled(LogLevel::Debug))
            {
                return;
            }

            // Segment and manifest requests belong to the stream; the rest
            // (covers, rejected requests) to the server.
            const auto category = req.path.starts_with("/stream/") || req.path.starts_with("/remote/")
                                       ? LogCategory::Stream
                                       : LogCategory::Server;
            Log::Debug(category, "{} {} -> {}{}", req.method, req.path, res.status,
                req.has_header("Range") ? " (Range " + req.get_header_value("Range") + ")" : std::string());
        });
};
void ServerWrapper::setupCorsRoutes()
{
    m_server->Options("/(.*)",
        [](const httplib::Request &, httplib::Response &res)
        {
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS, HEAD");
            res.set_header("Access-Control-Allow-Headers", "Range, Content-Type");
            res.status = 204;
        });
};
void ServerWrapper::setupStreamRoutes()
{
    m_server->Get("/stream/:streamid/:filename",
        [](const httplib::Request &req, httplib::Response &res)
        {
            const std::string filename = req.path_params.at("filename");

            const auto resolved = ResolveStreamFile(req.path_params.at("streamid"), filename);
            if (!resolved)
            {
                res.status = 404;
                return;
            }
            const fs::path filepath = resolved->path;

            if (filename.ends_with(".vtt") && !IsRegularFile(filepath))
            {
                WaitForSubtitleTrack(resolved->fileRecord, filename);
            }

            if (!IsRegularFile(filepath))
            {
                res.status = 404;
                return;
            }

            StreamActivity::NoteRequest();

            std::string mime = NetworkUtils::Get_mime_type(filename);

            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_header("Access-Control-Expose-Headers", "Content-Length, Content-Range");
            res.set_header("Accept-Ranges", "bytes");

            if (filename.ends_with(".mpd") || filename.ends_with(".m3u8") || filename.ends_with(".vtt"))
            {
                // Read in one go rather than streamed: ffmpeg replaces the
                // manifest after every segment, so a size taken up front can
                // belong to a different version than the bytes read later.
                auto content =
                    WithLastCompleteFallback(filepath, ReadWholeFile(filepath, req.remote_addr));
                if (!content)
                {
                    res.status = 404;
                    return;
                }

                res.set_content(std::move(*content), mime);
                return;
            }

            res.set_content_provider(fs::file_size(filepath), mime,
                [filepath](size_t offset, size_t length, httplib::DataSink &sink)
                {
                    std::ifstream file(filepath, std::ios::binary);
                    if (!file)
                    {
                        return false;
                    }

                    file.seekg(offset);

                    constexpr size_t BufferSize = 64 * 1024;
                    std::vector<char> buffer(BufferSize);

                    size_t bytes_to_read = length;

                    while (bytes_to_read > 0 && sink.is_writable())
                    {
                        size_t chunk_size = std::min(buffer.size(), bytes_to_read);

                        file.read(buffer.data(), chunk_size);

                        std::streamsize bytes_read = file.gcount();
                        if (bytes_read <= 0)
                        {
                            break;
                        }

                        sink.write(buffer.data(), bytes_read);
                        bytes_to_read -= bytes_read;
                    }

                    return true;
                });
        });
};

// The current stream's extracted cover (see CoverExtractor) - the poster a
// Chromecast shows while a stream loads (see Engine::LoadCastDevice). Looked
// up by stream ID, never built into a path from the request.
void ServerWrapper::setupCoverRoute()
{
    m_server->Get("/cover/:streamid",
        [](const httplib::Request &req, httplib::Response &res)
        {
            const auto content = ReadCover(req.path_params.at("streamid"), req.remote_addr);
            if (!content)
            {
                res.status = 404;
                return;
            }

            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(content->bytes, content->mimeType);
        });
}


// A paired peer's stream, forwarded over the encrypted link: the TV only
// ever talks to this server. Called with m_mutex held (see setupRoutes).
void ServerWrapper::setupRemoteRoute()
{
    m_server->Get(mango::remote::RemoteLinkHandler::kRoutePattern,
        [handler = m_remoteHandler](const httplib::Request &req, httplib::Response &res)
        {
            if (!handler)
            {
                res.status = 404;
                return;
            }
            handler(req, res);
        });
}
