#pragma once
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "profiles/StreamProfile.hpp"
#include "target/StreamTarget.hpp"
#include "target/StreamTargetSpecs.hpp"
#include "../../media/MediaJob.hpp"
#include "../library/FileRecord.hpp"
#include "../MediaInfo.hpp"

class StreamManager
{
   public:
    // Global access point to the single instance
    static StreamManager& GetInstance();

    // Delete copy constructor and assignment operator
    StreamManager(const StreamManager&) = delete;
    StreamManager& operator=(const StreamManager&) = delete;

    // Delete move constructor and assignment operator
    StreamManager(StreamManager&&) = delete;
    StreamManager& operator=(StreamManager&&) = delete;

    // The started stream's file record, or why nothing was started.
    std::expected<FileRecord, ConversionError> StartStream(
        const FileRecord& fileRecord, const TrackList& trackList, const StreamTarget& target);

    void StopCurrentStream();

    std::optional<FileRecord> GetCurrentFileRecord();

    // The random ID the current stream is served under (see StreamId);
    // std::nullopt when nothing is streaming. A new one for every started
    // stream; a finished stream restored after a relaunch keeps its own.
    std::optional<std::string> GetCurrentStreamId();

    // The current stream's file record if streamId is its ID, otherwise
    // std::nullopt — so a stopped or replaced stream's URLs stop working.
    std::optional<FileRecord> FindStream(const std::string& streamId);
    std::optional<TrackList> GetCurrentTrackList();
    // What the current stream was made with. std::nullopt when nothing is
    // streaming, or for a stream restored from a state file written before
    // targets were saved - which is then never reused.
    std::optional<StreamTarget> GetCurrentTarget();

    // How much of the current stream's source file is fully converted, as
    // a percentage of its duration (0-100) — the conversion's
    // ProgressSeconds against m_currentFileRecord's known duration. 100 once
    // m_transcodeCompleted is set. std::nullopt when nothing is currently
    // streaming, or its conversion failed.

    std::optional<double> GetTranscodeProgressPercent();

    // True once the current stream's conversion ran to the end: its
    // manifest is final and every segment exists.
    bool IsCurrentTranscodeCompleted();

    // True once the current stream's conversion ended with an error (e.g. a
    // failed manifest rename) — its stream folder can never be completed,
    // so it must be restarted rather than reused.
    bool HasCurrentTranscodeFailed();

    // Why the current stream's conversion failed; std::nullopt while it
    // runs, after it finished, or when nothing is streaming.
    std::optional<ConversionError> GetCurrentConversionError();

   private:
    // Private constructor prevents direct instantiation
    StreamManager();
    ~StreamManager() = default;

    // The tracks the main conversion muxes: a sidecar-subtitle profile's
    // subtitle track is left out.
    [[nodiscard]] static TrackList MuxedTracks(const TrackList& trackList, const StreamProfile& profile);

    // Resolves the specs and starts the backend conversion into
    // m_currentConversion. Called with m_mutex held.
    std::expected<void, ConversionError> StartConversionLocked(
        const FileRecord& fileRecord, const TrackList& muxedTrackList, const StreamTarget& target);

    // Hand-writes the HLS master playlist (the profile's entry file)
    // referencing the backend's video/audio media playlist plus, when
    // subtitleTrack is set, its companion WebVTT playlist (see
    // StreamOutputLayout). Written here rather than by the backend because
    // ffmpeg can't write one that carries a WebVTT rendition (its webvtt
    // muxer rejects a second subtitle stream, and a subtitle-only variant
    // stream fails outright even alone).
    static bool WriteHlsMasterPlaylist(const std::filesystem::path& streamFolder,
        const StreamProfile& profile, const std::optional<SubtitleTrack>& subtitleTrack);

    void LoadState();
    void SaveState();

    // The conversion's exit callback, from a backend thread.
    void OnConversionExit(ConversionResult result);
    void LogConversionExitLocked(const ConversionResult& result) const;

    // A Debug line each time the progress crosses another 10%.
    void LogProgressLocked(double percent);

    std::mutex m_mutex;
    std::optional<FileRecord> m_currentFileRecord;
    std::string m_currentStreamId;
    std::optional<TrackList> m_currentTrackList;
    std::optional<StreamTarget> m_currentTarget;
    bool m_transcodeCompleted = false;
    bool m_transcodeFailed = false;
    std::optional<ConversionError> m_conversionError;
    std::unique_ptr<Conversion> m_currentConversion;
    int m_loggedProgressDecile = -1;
};
