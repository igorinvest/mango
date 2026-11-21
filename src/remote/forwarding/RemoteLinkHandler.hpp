#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>

#include "ForwardMessages.hpp"

namespace httplib
{
struct Request;
struct Response;
}  // namespace httplib

namespace mango::remote
{

class ForwardingClient;

// B's /remote/<peerId>/<path> route (step 7 of
// docs/scope/hybrid_streaming_architecture.md): forwards the TV's request
// to the paired peer A as <path> — only a path A's HTTP server has (see
// ForwardingAdapter::IsServerPath) — and streams A's answer back through
// httplib's content provider. Status, Content-Type, Content-Range and
// Content-Length pass through; Range requests keep working because httplib
// cuts the range out of the total size A reported. The request is cancelled
// on A when the TV disconnects, and fails if A stops answering.
class RemoteLinkHandler
{
   public:
    // The connected peer's client; nullptr when it isn't connected.
    using ClientLookup = std::function<std::shared_ptr<ForwardingClient>(const std::string& peerId)>;

    // The regex ServerWrapper registers: group 1 the peer id, group 2 the
    // path forwarded to A.
    static constexpr auto kRoutePattern = R"(/remote/([0-9a-f]{64})(/.*))";

    // A's read timeout plus a margin: a held request answers within it.
    static constexpr std::chrono::seconds kHeadTimeout{150};
    static constexpr std::chrono::seconds kChunkTimeout{60};

    explicit RemoteLinkHandler(ClientLookup lookup);

    // Handles a request matched by kRoutePattern.
    void Handle(const httplib::Request& request, httplib::Response& response) const;

   private:
    ClientLookup m_lookup;
};

}  // namespace mango::remote
