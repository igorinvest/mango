#pragma once

#include <atomic>
#include <chrono>
#include <format>
#include <string>
#include <thread>

#include <httplib.h>

// A stand-in for A's HTTP server in forwarding tests, on 127.0.0.1:
//   /stream/abc/seg.m4s  body() with Range support (5 MB: more than the
//                        forwarding window, so acks matter)
//   /stream/abc/missing  404 "nope"
//   /stream/abc/slow     one chunk, then holds the response for up to 10 s;
//                        slowEnded() once the reader went away
class FakeHttpServer
{
   public:
    static constexpr std::size_t kBodySize = 5 * 1024 * 1024 + 123;

    FakeHttpServer()
    {
        m_body.resize(kBodySize);
        for (std::size_t i = 0; i < m_body.size(); ++i)
        {
            m_body[i] = static_cast<char>((i * 31 + i / 7) & 0xff);
        }

        m_server.Get("/stream/abc/seg.m4s", [this](const httplib::Request&, httplib::Response& res) {
            res.set_content_provider(m_body.size(), "video/mp4",
                [this](std::size_t offset, std::size_t length, httplib::DataSink& sink) {
                    return sink.write(m_body.data() + offset, length);
                });
        });
        m_server.Get("/stream/abc/missing", [](const httplib::Request&, httplib::Response& res) {
            res.status = 404;
            res.set_content("nope", "text/plain");
        });
        m_server.Get("/stream/abc/slow", [this](const httplib::Request&, httplib::Response& res) {
            res.set_chunked_content_provider("video/mp4", [this](std::size_t, httplib::DataSink& sink) {
                return HoldSlow(sink);
            });
        });

        m_port = m_server.bind_to_any_port("127.0.0.1");
        m_thread = std::thread([this] { m_server.listen_after_bind(); });
        m_server.wait_until_ready();
    }

    ~FakeHttpServer()
    {
        m_server.stop();
        m_thread.join();
    }

    FakeHttpServer(const FakeHttpServer&) = delete;
    FakeHttpServer& operator=(const FakeHttpServer&) = delete;

    [[nodiscard]] std::string Url() const { return std::format("http://127.0.0.1:{}", m_port); }
    [[nodiscard]] const std::string& Body() const { return m_body; }
    [[nodiscard]] bool SlowEnded() const { return m_slowEnded; }

   private:
    bool HoldSlow(httplib::DataSink& sink)
    {
        const std::string chunk(64 * 1024, 'x');
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < end)
        {
            if (!sink.is_writable() || !sink.write(chunk.data(), chunk.size()))
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        m_slowEnded = true;
        return false;
    }

    httplib::Server m_server;
    std::thread m_thread;
    int m_port = 0;
    std::string m_body;
    std::atomic<bool> m_slowEnded{false};
};
