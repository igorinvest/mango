// Engine tests that don't touch the user's library or app data folder.
// Built with -DMANGO_BUILD_TESTS=ON, run with ctest (or ./MangoTests).
//
// The ffmpeg tests run the bundled ffmpeg next to this executable on a
// clip they generate into a temp folder.

#include <algorithm>
#include <chrono>
#include <iterator>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <string>
#include <vector>

#include <glaze/glaze.hpp>

#include "FakeMediaBackend.hpp"
#include "TestCheck.hpp"
#include "file/File.hpp"
#include "file/Serialization.hpp"
#include "log/Logger.hpp"
#include "media/StreamOutputLayout.hpp"
#include "media/ffmpeg/install/FFmpegInstalls.hpp"
#include "media/ffmpeg/process/FFmpegProcess.hpp"
#include "media/ffmpeg/FfmpegBackend.hpp"
#include "media/ffmpeg/FFmpegWrapper.hpp"
#include "media/ffmpeg/conversion/FfmpegConversion.hpp"
#include "pipeline/playback/PlaybackEndDetector.hpp"
#include "pipeline/StreamId.hpp"
#include "server/NetworkUtils.hpp"
#include "server/RequestGuard.hpp"
#include "server/SessionPolicy.hpp"
#include "pipeline/library/external/ExternalAudioFinder.hpp"
#include "pipeline/library/external/ExternalFileFinder.hpp"
#include "pipeline/library/external/ExternalSubtitleFinder.hpp"
#include "pipeline/stream/target/StreamTarget.hpp"
#include "pipeline/stream/target/StreamTargetResolver.hpp"

namespace fs = std::filesystem;

namespace
{
int failures = 0;
}  // namespace

void Check(bool condition, const std::string& what)
{
    if (!condition)
    {
        ++failures;
        std::cerr << "  FAILED: " << what << '\n';
    }
}

namespace
{

// ---- StreamTarget ----------------------------------------------------------

StreamTarget SampleTarget()
{
    StreamProfile profile;
    profile.playlist = PlaylistType::DASH;
    profile.videoCapabilities = {{.codec = "h264", .maxWidth = 1920, .maxHeight = 1080, .maxFps = 60}};
    profile.audioCapabilities = {{.codec = "aac", .maxChannels = 2, .maxSampleRate = 48000}};
    return {.playlist = PlaylistType::DASH,
        .profile = profile,
        .preset = EncodingPresetId::FullHD1080,
        .mode = VideoTranscodeMode::Automatic};
}

void TargetEquality()
{
    const auto target = SampleTarget();
    Check(target == SampleTarget(), "identical targets compare equal");

    auto otherPreset = target;
    otherPreset.preset = EncodingPresetId::HD720;
    Check(target != otherPreset, "a preset change makes targets differ");

    auto otherMode = target;
    otherMode.mode = VideoTranscodeMode::ForceDecode;
    Check(target != otherMode, "a mode change makes targets differ");

    auto otherCapability = target;
    otherCapability.profile.videoCapabilities.front().maxFps = 30;
    Check(target != otherCapability, "a capability change makes targets differ");
}

// ---- StreamTargetResolver --------------------------------------------------

// A 1080i HDTV recording: codec, size and fps all match the TV, but cast
// devices can't decode field-coded H.264, so it is converted and deinterlaced.
void InterlacedVideoIsConverted()
{
    auto fake = std::make_unique<FakeMediaBackend>();
    fake->videoEncoder = "libx264";
    MediaBackends::SetForTesting(std::move(fake));

    auto target = SampleTarget();
    target.encoding = StreamProfiles::BuiltInEncodingDefaults();
    VideoTrack track{.index = 0, .codec = "h264", .width = 1920, .height = 1080, .fps = 25.0,
        .bitrate = 0, .language = "", .title = "", .rotation = 0, .interlaced = false};

    const auto progressive = StreamTargetResolver::Resolve(track, {}, {}, target);
    Check(progressive.video.copy, "progressive H.264 within the capabilities is copied");

    track.interlaced = true;
    const auto interlaced = StreamTargetResolver::Resolve(track, {}, {}, target);
    Check(!interlaced.video.copy && interlaced.video.deinterlace,
        "interlaced H.264 is converted and deinterlaced");

    MediaBackends::SetForTesting(nullptr);
}

// ---- Device profiles -------------------------------------------------------

void DeviceProfileOverrides()
{
    auto target = SampleTarget();
    StreamTargets::Apply(target, DeviceProfile{.name = "TV"});
    Check(target == SampleTarget(), "a name-only entry uses the defaults");

    const DeviceProfile device{.name = "TV",
        .preset = "4k",
        .videoCapabilities = std::vector<VideoCodecConfig>{{.codec = "hevc", .maxWidth = 3840, .maxHeight = 2160, .maxFps = 60}},
        .rotationSupported = true};
    StreamTargets::Apply(target, device);
    Check(target.preset == EncodingPresetId::UHD4K, "the TV's preset is used");
    Check(target.profile.videoCapabilities.size() == 1 && target.profile.videoCapabilities[0].codec == "hevc",
        "the TV's video capabilities replace the default");
    Check(target.profile.audioCapabilities == SampleTarget().profile.audioCapabilities,
        "audio left out keeps the default");
    Check(target.profile.rotationSupported, "the TV's rotation setting is used");
    Check(target.mode == SampleTarget().mode, "mode stays global");
}

void DeviceProfileRemember()
{
    DeviceProfileMap profiles;
    Check(!DeviceProfiles::RememberIn(profiles, {"", "No id"}), "a TV without an id isn't added");
    Check(DeviceProfiles::RememberIn(profiles, {"chromecast:a", "Living Room"}), "a new TV is added");
    Check(!DeviceProfiles::RememberIn(profiles, {"chromecast:a", "Living Room"}), "the same TV changes nothing");

    profiles["chromecast:a"].preset = "720p";
    Check(DeviceProfiles::RememberIn(profiles, {"chromecast:a", "Bedroom"}), "a renamed TV is updated");
    Check(profiles["chromecast:a"].name == "Bedroom" && profiles["chromecast:a"].preset == "720p",
        "with its settings kept");
    Check(DeviceProfiles::ChromecastKey("").empty() && DeviceProfiles::ChromecastKey("a") == "chromecast:a",
        "keys are chromecast:<id>");
}

void DeviceProfileValidation()
{
    Check(DeviceProfiles::IsValid({{"chromecast:a", {.name = "TV"}}}), "a name-only entry is valid");
    Check(!DeviceProfiles::IsValid({{"chromecast:a", {.name = "TV", .preset = "8k"}}}), "an unknown preset isn't");
    Check(!DeviceProfiles::IsValid({{"chromecast:a",
              {.name = "TV", .videoCapabilities = std::vector<VideoCodecConfig>{{.codec = "h264", .maxWidth = 0}}}}}),
        "a zero size isn't");
    Check(!DeviceProfiles::IsValid({{"chromecast:a",
              {.name = "TV", .audioCapabilities = std::vector<AudioCodecConfig>{{.codec = "aac"}}}}}),
        "zero channels aren't");
}

void DeviceProfileJson()
{
    const auto written = glz::write_json(DeviceProfileMap{{"chromecast:a", {.name = "TV"}}});
    Check(written && *written == R"({"chromecast:a":{"name":"TV"}})", "settings left out aren't written");

    DeviceProfileMap read;
    const auto error = glz::read_json(read, R"({"chromecast:a":{"name":"TV","preset":"4k","rotationSupported":true}})");
    Check(!error && read["chromecast:a"].preset == "4k" && read["chromecast:a"].rotationSupported == true,
        "a hand-edited entry is read");

    DeviceProfileMap unknown;
    Check(bool(glz::read_json(unknown, R"({"chromecast:a":{"name":"TV","bitrate":"1M"}})")),
        "an unknown key is rejected");
}

// ---- ffmpeg output parsing -------------------------------------------------

void ProgressParsing()
{
    const auto seconds = FfmpegConversion::ParseLatestOutTime(
        "frame=10\nout_time=00:00:01.000000\nprogress=continue\n"
        "frame=20\nout_time=01:02:03.500000\nprogress=continue\n");
    Check(seconds && *seconds == 3723.5, "latest out_time is parsed");
    Check(!FfmpegConversion::ParseLatestOutTime("frame=1\n"), "no out_time gives nullopt");
    Check(!FfmpegConversion::ParseLatestOutTime("out_time=N/A\n"), "N/A gives nullopt");
}

void LogClassification()
{
    Check(FfmpegConversion::ClassifyLog("[vost#0:0] Unknown encoder 'libx264'") ==
              ConversionError::NoVideoEncoder,
        "unknown encoder");
    Check(FfmpegConversion::ClassifyLog("[in#0] Error opening input: No such file or directory") ==
              ConversionError::FileUnreadable,
        "missing input");
    Check(FfmpegConversion::ClassifyLog("x.mkv: Invalid data found when processing input") ==
              ConversionError::FileUnreadable,
        "unreadable input");
    Check(FfmpegConversion::ClassifyLog("Could not write header (incorrect codec parameters ?)") ==
              ConversionError::OutputFailed,
        "output header");
    Check(FfmpegConversion::ClassifyLog("Conversion failed!") == ConversionError::Unknown,
        "anything else is unknown");
}

// ---- Fake backend ----------------------------------------------------------

void FakeBackendIsUsed()
{
    auto fake = std::make_unique<FakeMediaBackend>();
    auto* backend = fake.get();
    backend->videoEncoder = std::nullopt;
    MediaBackends::SetForTesting(std::move(fake));

    Check(&MediaBackends::Get() == backend, "SetForTesting replaces the backend");
    Check(!MediaBackends::Get().VideoEncoder(StreamProfile{}), "the fake's encoder is reported");

    FileRecord file("hash", "/nowhere/movie.mkv", MediaInfo{});
    StreamProfile profile;
    std::optional<ConversionResult> exit;
    auto conversion = MediaBackends::Get().StartConversion(
        {.file = file, .tracks = {}, .targets = {}, .profile = profile, .outputFolder = "/out"},
        [&exit](ConversionResult result) { exit = result; });

    Check(conversion.has_value() && backend->requests.size() == 1, "conversion recorded");
    backend->lastConversion->Finish(std::unexpected(ConversionError::UnsupportedCodec));
    Check(exit && !exit->has_value() && exit->error() == ConversionError::UnsupportedCodec,
        "a fake conversion reports the error it was finished with");

    MediaBackends::SetForTesting(nullptr);
}

// ---- Real ffmpeg -----------------------------------------------------------

struct TempFolder
{
    fs::path path;

    TempFolder()
    {
        path = fs::temp_directory_path() /
               ("mango-tests-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

bool RunFfmpeg(const std::vector<std::string>& arguments)
{
    bool succeeded = false;
    const auto* install = FFmpegInstalls::Primary();
    auto process =
        install ? FFmpegProcess::Start(*install, arguments, [&](bool ok) { succeeded = ok; }) : nullptr;
    if (!process)
    {
        return false;
    }
    process->Wait();
    return succeeded;
}

// A 3 s, 320x240 MPEG-4 clip with AAC stereo audio.
bool MakeClip(const fs::path& clip)
{
    return RunFfmpeg({"-loglevel", "error", "-y", "-f", "lavfi", "-i",
        "testsrc=size=320x240:rate=25:duration=3", "-f", "lavfi", "-i",
        "sine=frequency=440:duration=3", "-c:v", "mpeg4", "-c:a", "aac", "-ac", "2",
        clip.string()});
}

FileRecord ClipRecord(const fs::path& clip)
{
    MediaInfo info{};
    info.videos = {{.index = 0, .codec = "mpeg4", .width = 320, .height = 240, .fps = 25.0,
        .bitrate = 0, .language = "", .title = "", .rotation = 0}};
    info.audios = {{.index = 1, .codec = "aac", .channels = 2, .sampleRate = 44100, .bitrate = 0,
        .language = "", .title = ""}};
    info.durationSec = 3;
    return FileRecord("clip", clip, info);
}

StreamProfile ProfileFor(PlaylistType playlist)
{
    StreamProfile profile;
    profile.playlist = playlist;
    profile.defaultFileName = "index";
    profile.defaultExtension = playlist == PlaylistType::HLS ? ".m3u8" : ".mpd";
    profile.segmentDurationSec = 1;
    profile.videoCapabilities = {{.codec = "h264", .maxWidth = 1920, .maxHeight = 1080, .maxFps = 60}};
    return profile;
}

ConversionResult Convert(FfmpegBackend& backend, const FileRecord& file, const TrackList& tracks,
    PlaylistType playlist, const fs::path& outputFolder)
{
    const auto profile = ProfileFor(playlist);
    StreamTargetSpecs targets;
    const auto encoder = backend.VideoEncoder(profile);
    if (!encoder)
    {
        return std::unexpected(ConversionError::NoVideoEncoder);
    }
    targets.video = {.copy = false, .codec = encoder->encoder, .preset = "",
        .bitrate = "1000k", .pix_fmt = "yuv420p", .maxWidth = 1280, .maxHeight = 720,
        .deinterlace = false, .driver = encoder->driver};
    targets.audio = {.copy = true, .codec = "copy", .bitrate = ""};
    if (tracks.subtitleTrack)
    {
        targets.subtitle = {.codec = "webvtt", .bitrate = ""};
    }

    fs::create_directories(outputFolder);
    std::promise<ConversionResult> exited;
    auto conversion = backend.StartConversion(
        {.file = file,
            .tracks = tracks,
            .targets = targets,
            .profile = profile,
            .outputFolder = outputFolder},
        [&exited](ConversionResult result) { exited.set_value(result); });
    if (!conversion)
    {
        return std::unexpected(conversion.error());
    }

    auto result = exited.get_future().get();
    if (result)
    {
        Check((*conversion)->ProgressSeconds().has_value(), "progress was reported");
    }
    return result;
}

ConversionResult Convert(FfmpegBackend& backend, const FileRecord& file, PlaylistType playlist,
    const fs::path& outputFolder)
{
    return Convert(backend, file,
        {.videoTrack = file.mediaInfo.videos.front(),
            .audioTrack = file.mediaInfo.audios.front(),
            .subtitleTrack = std::nullopt},
        playlist, outputFolder);
}

void FfmpegConvertsDash()
{
    TempFolder temp;
    const auto clip = temp.path / "clip.mp4";
    Check(MakeClip(clip), "test clip generated");

    FfmpegBackend backend;
    const auto output = temp.path / "dash";
    const auto result = Convert(backend, ClipRecord(clip), PlaylistType::DASH, output);
    Check(result.has_value(), "DASH conversion completes");
    Check(fs::exists(output / "index.mpd"), "DASH manifest written");
    Check(fs::exists(output / "init_0.mp4"), "DASH init segment written");
}

void FfmpegConvertsHls()
{
    TempFolder temp;
    const auto clip = temp.path / "clip.mp4";
    Check(MakeClip(clip), "test clip generated");

    FfmpegBackend backend;
    const auto output = temp.path / "hls";
    const auto result = Convert(backend, ClipRecord(clip), PlaylistType::HLS, output);
    Check(result.has_value(), "HLS conversion completes");
    Check(fs::exists(output / StreamOutputLayout::kHlsMediaPlaylistName), "HLS playlist written");
}

void FfmpegReportsMissingFile()
{
    TempFolder temp;
    FfmpegBackend backend;
    const auto result =
        Convert(backend, ClipRecord(temp.path / "missing.mp4"), PlaylistType::DASH, temp.path / "o");
    Check(!result && result.error() == ConversionError::FileUnreadable,
        "a missing source is refused as FileUnreadable");
}

void FfmpegReportsUnreadableFile()
{
    TempFolder temp;
    const auto garbage = temp.path / "garbage.mp4";
    {
        std::ofstream file(garbage, std::ios::binary);
        file << std::string(4096, '\x5a');
    }

    FfmpegBackend backend;
    const auto result = Convert(backend, ClipRecord(garbage), PlaylistType::DASH, temp.path / "o");
    Check(!result && result.error() == ConversionError::FileUnreadable,
        "a file ffmpeg can't read fails as FileUnreadable");
}
// ---- External audio --------------------------------------------------------

void Touch(const fs::path& file)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << "x";
}

// What a folder scan's walk would record for root.
DirectorySnapshot SnapshotOf(const fs::path& root)
{
    DirectorySnapshot snapshot;
    snapshot.AddDirectory(root);
    for (const auto& entry : fs::recursive_directory_iterator(root))
    {
        if (entry.is_directory())
        {
            snapshot.AddDirectory(entry.path());
        }
        else
        {
            snapshot.AddFile(entry.path());
        }
    }
    return snapshot;
}

std::vector<std::string> Titles(const std::vector<ExternalFileCandidate>& found)
{
    std::vector<std::string> titles;
    for (const auto& candidate : found)
    {
        titles.push_back(candidate.title);
    }
    return titles;
}

void ExternalAudioDetection()
{
    TempFolder temp;
    const auto video = temp.path / "Movie.mkv";
    Touch(video);
    Touch(temp.path / "Movie.rus.ac3");
    Touch(temp.path / "Movie 2.ac3");  // another video's
    Touch(temp.path / "Other.mka");    // not named after the video
    Touch(temp.path / "Movie.en.srt");  // a subtitle, not audio
    Touch(temp.path / "Rus Sound" / "LostFilm" / "Movie.mka");
    Touch(temp.path / "Rus Sound" / "LostFilm" / "Movie 2.mka");
    Touch(temp.path / "Audio" / "Ukr.mka");
    Touch(temp.path / "Music" / "Movie.mp3");  // named after it, in any folder
    Touch(temp.path / "Music" / "Theme.mp3");  // not named after it, not an audio folder

    const auto titles = Titles(ExternalAudioFinder::FindCandidates(SnapshotOf(temp.path), {video}));
    const std::vector<std::string> expected = {
        "Audio/Ukr.mka", "Movie.rus.ac3", "Music/Movie.mp3", "Rus Sound/LostFilm/Movie.mka"};
    Check(titles == expected, "audio files next to the video and in audio folders are found");

    // Subtitles follow the same rules.
    Touch(temp.path / "Subs" / "English.srt");
    Touch(temp.path / "Subs" / "Studio" / "Russian.srt");
    Touch(temp.path / "Subs" / "Movie" / "2_French.srt");
    const auto subtitleTitles =
        Titles(ExternalSubtitleFinder::FindCandidates(SnapshotOf(temp.path), {video}));
    const std::vector<std::string> expectedSubtitles = {"Movie.en.srt", "Subs/English.srt",
        "Subs/Movie/2_French.srt", "Subs/Studio/Russian.srt"};
    Check(subtitleTitles == expectedSubtitles, "subtitles are found by the same rules");
}

void ExternalFileDepth()
{
    TempFolder temp;
    const auto video = temp.path / "S01E01.mkv";
    Touch(video);
    Touch(temp.path / "S01E02.mkv");
    Touch(temp.path / "a" / "b" / "c" / "S01E01.srt");      // 3 levels down
    Touch(temp.path / "a" / "b" / "c" / "d" / "S01E01.srt");  // 4: too deep
    Touch(temp.path / "Extras" / "Commentary.srt");          // not named after it
    Touch(temp.path / "Subs" / "S01E01" / "2_English.srt");
    Touch(temp.path / "Subs" / "S01E02" / "2_English.srt");  // the other episode's
    Touch(temp.path / "S01E02" / "S01E01.srt");             // inside the other episode's folder

    const auto titles =
        Titles(ExternalSubtitleFinder::FindCandidates(SnapshotOf(temp.path), {video}));
    const std::vector<std::string> expected = {"Subs/S01E01/2_English.srt", "a/b/c/S01E01.srt"};
    Check(titles == expected, "subfolders are searched 3 levels deep, skipping other videos'");
}

void ExternalFileReleaseLayout()
{
    TempFolder temp;
    const auto video = temp.path / "[anti-raws]Kusuriya no Hitorigoto S2 ep.01[BDRemux].mkv";
    Touch(video);
    Touch(temp.path / "[anti-raws]Kusuriya no Hitorigoto S2 ep.02[BDRemux].mkv");
    Touch(File::FromUtf8(File::ToUtf8(temp.path) +
        "/RUS Subs/[\u0421runchyroll]/[anti-raws]Kusuriya no Hitorigoto S2 ep.01[BDRemux]."
        "\u0421runchyroll.ass"));
    Touch(File::FromUtf8(File::ToUtf8(temp.path) +
        "/RUS Subs/[\u0421runchyroll]/[anti-raws]Kusuriya no Hitorigoto S2 ep.02[BDRemux]."
        "\u0421runchyroll.ass"));

    const auto titles =
        Titles(ExternalSubtitleFinder::FindCandidates(SnapshotOf(temp.path), {video}));
    Check(titles.size() == 1 && titles.front().ends_with("ep.01[BDRemux].\u0421runchyroll.ass"),
        "a release's nested subtitle folder is searched");

    const auto snapshot = SnapshotOf(temp.path);
    FileRecord record;
    ExternalSubtitleFinder::MergeInto(
        record, ExternalSubtitleFinder::FindCandidates(snapshot, {video}), snapshot);
    Check(record.externalSubtitles.size() == 1 &&
            record.externalSubtitles.front().language == "rus",
        "the language comes from the folder when the name has none");

    // In a "<Language> Subs" folder, files not named after the video count
    // when none is.
    TempFolder movie;
    const auto movieVideo = movie.path / "Movie.mkv";
    Touch(movieVideo);
    Touch(movie.path / "ENG Subs" / "Full.srt");
    const auto movieTitles =
        Titles(ExternalSubtitleFinder::FindCandidates(SnapshotOf(movie.path), {movieVideo}));
    Check(movieTitles == std::vector<std::string>{"ENG Subs/Full.srt"},
        "a <Language> Subs folder is a subtitles folder");
}

void ExternalNameTagsParsing()
{
    const auto tags = ExternalFileFinder::ParseNameTags("Movie", "Movie.en.commentary.mka");
    Check(tags.language == "eng" && tags.commentary, "language and commentary are read");
    Check(ExternalFileFinder::ParseNameTags("Movie", "Movie.rus.ac3").language == "rus",
        "a 3-letter code is read");
}

void ExternalAudioMerge()
{
    TempFolder temp;
    const auto video = temp.path / "Movie.mkv";
    Touch(video);
    Touch(temp.path / "Movie.ac3");
    Touch(temp.path / "Rus Sound" / "Movie.mka");
    Touch(temp.path / "Movie.broken.mka");

    auto fake = std::make_unique<FakeMediaBackend>();
    fake->audioFileInfos[temp.path / "Movie.ac3"] = {.codec = "ac3", .channels = 6,
        .sampleRate = 48000, .bitrate = 0, .durationSeconds = 10, .startTimeSeconds = 0};
    fake->audioFileInfos[temp.path / "Rus Sound" / "Movie.mka"] = {.codec = "aac",
        .channels = 2, .sampleRate = 48000, .bitrate = 0, .durationSeconds = 10,
        .startTimeSeconds = 0};
    MediaBackends::SetForTesting(std::move(fake));

    FileRecord record("hash", video, MediaInfo{});
    const auto snapshot = SnapshotOf(temp.path);
    const auto candidates = ExternalAudioFinder::FindCandidates(snapshot, {video});
    Check(ExternalAudioFinder::MergeInto(record, candidates, snapshot), "files are added");
    Check(record.externalAudios.size() == 2, "an unreadable file isn't added");

    const auto russian = std::ranges::find(
        record.externalAudios, std::string("Rus Sound/Movie.mka"), &ExternalAudio::title);
    Check(russian != record.externalAudios.end() && russian->language == "rus",
        "the language comes from the folder name");
    Check(record.externalAudios.front().index == kExternalAudioIndexBase &&
            record.externalAudios.back().index == kExternalAudioIndexBase + 1,
        "indices start at 1000");
    Check(record.find_audio_by_index(kExternalAudioIndexBase).has_value() &&
            record.AudioTrackCount() == 2,
        "external tracks are found by index and counted");

    Check(!ExternalAudioFinder::MergeInto(record, candidates, snapshot),
        "an unchanged re-scan changes nothing");

    // Another added folder's scan doesn't see these files: they're kept.
    TempFolder other;
    const auto otherSnapshot = SnapshotOf(other.path);
    Check(!ExternalAudioFinder::MergeInto(record, {}, otherSnapshot),
        "files outside the walked folder are kept");

    fs::remove(temp.path / "Movie.ac3");
    const auto fewerSnapshot = SnapshotOf(temp.path);
    const auto fewer = ExternalAudioFinder::FindCandidates(fewerSnapshot, {video});
    Check(ExternalAudioFinder::MergeInto(record, fewer, fewerSnapshot) &&
            record.externalAudios.size() == 1 &&
            record.externalAudios.front().index == kExternalAudioIndexBase + 1,
        "a deleted file is dropped and the rest keep their index");

    MediaBackends::SetForTesting(nullptr);
}

// A 5 s AC3 dub — longer than the 3 s clip, so the output gets capped.
bool MakeDub(const fs::path& dub)
{
    return RunFfmpeg({"-loglevel", "error", "-y", "-f", "lavfi", "-i",
        "sine=frequency=220:duration=5", "-c:a", "ac3", dub.string()});
}

FileRecord ClipWithDub(const fs::path& clip, const fs::path& dub)
{
    auto record = ClipRecord(clip);
    const auto probed = FFmpegWrapper::ReadAudioFile(dub);
    Check(probed && probed->codec == "ac3" && probed->durationSeconds > 4.5,
        "the dub is probed");

    ExternalAudio audio;
    audio.index = kExternalAudioIndexBase;
    audio.path = File::ToUtf8(dub);
    audio.title = "clip.rus.ac3";
    audio.codec = probed ? probed->codec : "";
    audio.channels = probed ? probed->channels : 0;
    audio.sampleRate = probed ? probed->sampleRate : 0;
    audio.durationSeconds = probed ? probed->durationSeconds : 0;
    record.externalAudios.push_back(audio);
    return record;
}

void FfmpegConvertsExternalAudio(PlaylistType playlist)
{
    TempFolder temp;
    const auto clip = temp.path / "clip.mp4";
    const auto dub = temp.path / "clip.rus.ac3";
    const auto subtitle = temp.path / "clip.en.srt";
    Check(MakeClip(clip) && MakeDub(dub), "test clip and dub generated");
    std::ofstream(subtitle) << "1\n00:00:00,500 --> 00:00:02,000\nHello\n";

    FfmpegBackend backend;
    auto record = ClipWithDub(clip, dub);

    TrackList tracks{.videoTrack = record.mediaInfo.videos.front(),
        .audioTrack = record.find_audio_by_index(kExternalAudioIndexBase),
        .subtitleTrack = std::nullopt};

    // HLS muxes the subtitle too: external audio is input 1, the subtitle
    // input 2.
    if (playlist == PlaylistType::HLS)
    {
        ExternalSubtitle external;
        external.index = kExternalSubtitleIndexBase;
        external.path = File::ToUtf8(subtitle);
        external.codec = "subrip";
        record.externalSubtitles.push_back(external);
        tracks.subtitleTrack = record.find_subtitle_by_index(kExternalSubtitleIndexBase);
    }

    const auto output = temp.path / "out";
    const auto result = Convert(backend, record, tracks, playlist, output);
    Check(result.has_value(), "conversion with an external dub completes");
}

void FfmpegConvertsExternalAudioDash() { FfmpegConvertsExternalAudio(PlaylistType::DASH); }
void FfmpegConvertsExternalAudioHls() { FfmpegConvertsExternalAudio(PlaylistType::HLS); }

// ---- PlaybackEndDetector ---------------------------------------------------

PlaybackEndKind DecideCast(const std::string& state, const std::string& idleReason, double currentTime,
    double duration, std::optional<double> lastReported, bool stoppedByUser = false)
{
    const auto end = PlaybackEndDetector::FromCastStatus(state, idleReason, true, currentTime);
    return PlaybackEndDetector::Decide(end, duration, lastReported, stoppedByUser);
}

void PlaybackEndClassification()
{
    constexpr double episode = 45 * 60;  // window: last 81 s (3%)

    Check(DecideCast("Playing", "", 100, episode, 98) == PlaybackEndKind::Playing, "playing");
    Check(DecideCast("Idle", "FINISHED", 0, episode, episode - 2) == PlaybackEndKind::Finished,
        "FINISHED near the end");
    Check(DecideCast("Idle", "FINISHED", 0, episode, 20 * 60) == PlaybackEndKind::Stopped,
        "FINISHED at minute 20 (conversion died)");
    Check(DecideCast("Idle", "FINISHED", episode - 1, episode, 20 * 60) == PlaybackEndKind::Finished,
        "FINISHED with the receiver's own end position");
    Check(DecideCast("Idle", "", 0, episode, episode - 30) == PlaybackEndKind::Finished,
        "session gone right after the end window");
    Check(DecideCast("Active", "", 0, episode, 10 * 60) == PlaybackEndKind::Stopped,
        "session gone mid-episode");
    Check(DecideCast("Idle", "CANCELLED", 0, episode, episode - 2) == PlaybackEndKind::Stopped,
        "CANCELLED");
    Check(DecideCast("Idle", "ERROR", 0, episode, episode - 2) == PlaybackEndKind::Stopped, "ERROR");
    Check(DecideCast("Idle", "FINISHED", 0, episode, episode - 2, true) == PlaybackEndKind::Stopped,
        "stopped by the user");
    Check(DecideCast("Unreachable", "", 0, episode, episode - 2) == PlaybackEndKind::Unknown,
        "unreachable");

    const auto other = PlaybackEndDetector::FromCastStatus("Playing", "", false, 10);
    Check(PlaybackEndDetector::Decide(other, episode, 100, false) == PlaybackEndKind::Stopped,
        "other content took over");

    const auto noMedia = PlaybackEndDetector::FromCastStatus("Playing", "", std::nullopt, 100);
    Check(PlaybackEndDetector::Decide(noMedia, episode, 100, false) == PlaybackEndKind::Unknown,
        "a status without media info (mid-seek) ends nothing");

    Check(PlaybackEndDetector::Decide(PlaybackEndDetector::FromLocalEnd("Ended", 120), 125, 100, false) ==
            PlaybackEndKind::Finished,
        "short clip: the 60 s minimum covers it");
    Check(PlaybackEndDetector::Decide(PlaybackEndDetector::FromLocalEnd("Ended", 1200), episode, 1198, false) ==
            PlaybackEndKind::Stopped,
        "local end short of the end");
    Check(PlaybackEndDetector::Decide(PlaybackEndDetector::FromLocalEnd("Ended", 1200), 0, std::nullopt, false) ==
            PlaybackEndKind::Finished,
        "unknown duration trusts the player");
}

// ---- Logger ----------------------------------------------------------------

std::vector<std::string> ReadLines(const fs::path& path)
{
    std::vector<std::string> lines;
    std::ifstream file(path);
    for (std::string line; std::getline(file, line);)
    {
        lines.push_back(line);
    }
    return lines;
}

bool EndsWith(const std::string& text, std::string_view suffix) { return text.ends_with(suffix); }

bool Contains(const std::vector<std::string>& lines, std::string_view text)
{
    return std::ranges::any_of(lines, [&](const std::string& line) { return line.contains(text); });
}

void LogLineFormat()
{
    const auto time = std::chrono::system_clock::time_point(std::chrono::milliseconds(1'700'000'000'345));
    const auto line = FormatLogLine(time, LogLevel::Info, "Cast", "LOAD sent\nto TV");

    // "YYYY-MM-DD HH:MM:SS.mmm I [Cast] ..." - the hour depends on the time zone.
    Check(line.size() > 24 && line[4] == '-' && line[10] == ' ' && line[19] == '.', "timestamp shape");
    Check(line.substr(20, 3) == "345", "milliseconds");
    Check(EndsWith(line, " I [Cast] LOAD sent to TV"), "level, category, single-line message");
    Check(LogLevelLetter(LogLevel::Error) == 'E' && LogLevelLetter(LogLevel::Warning) == 'W' &&
              LogLevelLetter(LogLevel::Debug) == 'D',
        "level letters");
}

void LogLevelFiltering()
{
    TempFolder temp;
    {
        Logger logger(LoggerOptions{.debugEnabled = false});
        Check(!logger.IsEnabled(LogLevel::Debug) && logger.IsEnabled(LogLevel::Info), "Debug off, Info on");
        logger.Open(temp.path / "off.log", {"header"});
        logger.Write(LogLevel::Debug, "Cast", "poll");
        logger.Write(LogLevel::Info, "Cast", "change");
        logger.Close();
    }
    const auto off = ReadLines(temp.path / "off.log");
    Check(!Contains(off, "poll") && Contains(off, "change"), "Debug line skipped when off");

    {
        Logger logger(LoggerOptions{.debugEnabled = true});
        logger.Open(temp.path / "on.log", {"header"});
        logger.Write(LogLevel::Debug, "Cast", "poll");
        logger.Close();
    }
    Check(Contains(ReadLines(temp.path / "on.log"), "D [Cast] poll"), "Debug line written when on");
}

void LogSizeCap()
{
    TempFolder temp;
    {
        Logger logger(LoggerOptions{.maxFileBytes = 1000});
        logger.Open(temp.path / "capped.log", {"header"});
        for (int i = 0; i < 100; ++i)
        {
            logger.Write(LogLevel::Info, "Stream", "a line long enough to fill the file quickly");
        }
        logger.Close();
    }

    const auto lines = ReadLines(temp.path / "capped.log");
    Check(!lines.empty() && lines.back().contains("log truncated"), "ends with the truncation marker");
    Check(std::ranges::count_if(lines, [](const std::string& l) { return l.contains("log truncated"); }) == 1,
        "one truncation marker");
    Check(fs::file_size(temp.path / "capped.log") < 1200, "file stays near the cap");
}

void LogCarryOver()
{
    TempFolder temp;
    {
        Logger logger(LoggerOptions{.carryOverLines = 2});
        logger.Write(LogLevel::Info, "Engine", "startup");
        logger.Write(LogLevel::Debug, "Server", "request");
        logger.Write(LogLevel::Info, "Playlist", "countdown");
        logger.Write(LogLevel::Info, "Playlist", "next video");
        logger.Open(temp.path / "b.log", {"Mango log header"});
        logger.Write(LogLevel::Info, "Stream", "started");
        logger.Close();
    }

    const auto lines = ReadLines(temp.path / "b.log");
    Check(lines.size() == 6, "header, marker, 2 carried lines, marker, own line");
    if (lines.size() == 6)
    {
        Check(lines[0] == "Mango log header", "header first");
        Check(lines[1] == "--- before this stream ---", "carry-over start marker");
        Check(EndsWith(lines[2], "[Playlist] countdown") && EndsWith(lines[3], "[Playlist] next video"),
            "only the last N Info+ lines carried");
        Check(lines[4] == "--- this stream ---", "carry-over end marker");
        Check(EndsWith(lines[5], "[Stream] started"), "the stream's own line after");
    }
}

void LogClosesBeforeDelete()
{
    TempFolder temp;
    const auto folder = temp.path / "stream";
    fs::create_directories(folder);

    Logger logger;
    logger.Open(folder / "mango.log", {"header"});
    logger.Write(LogLevel::Info, "Stream", "stopped");
    logger.Close();

    Check(Contains(ReadLines(folder / "mango.log"), "[Stream] stopped"), "lines before Close are written");

    std::error_code ec;
    fs::remove_all(folder, ec);
    Check(!ec && !fs::exists(folder), "folder deletable after Close");

    // With no file open, lines only reach the carry-over buffer.
    logger.Write(LogLevel::Info, "Engine", "after close");
    logger.Flush();
    Check(!fs::exists(folder), "nothing recreated after Close");
}

// ---- RequestGuard ----------------------------------------------------------

void SafePathSegments()
{
    Check(RequestGuard::IsSafePathSegment("a1b2c3"), "hash segment");
    Check(RequestGuard::IsSafePathSegment("stream.mpd"), "file name");
    Check(RequestGuard::IsSafePathSegment("chunk-1_00012.m4s"), "segment file name");
    Check(!RequestGuard::IsSafePathSegment(""), "empty");
    Check(!RequestGuard::IsSafePathSegment("."), "dot");
    Check(!RequestGuard::IsSafePathSegment(".."), "dot dot");
    Check(!RequestGuard::IsSafePathSegment("../x"), "slash");
    Check(!RequestGuard::IsSafePathSegment("..\\..\\win.ini"), "backslash");
    Check(!RequestGuard::IsSafePathSegment("C:x"), "drive prefix");
    Check(!RequestGuard::IsSafePathSegment("a b"), "space");
    Check(!RequestGuard::IsSafePathSegment(std::string("a\0b", 3)), "NUL");
}

void AllowedHosts()
{
    Check(RequestGuard::IsAllowedHost(""), "missing Host");
    Check(RequestGuard::IsAllowedHost("192.168.0.34:8080"), "IPv4 with port");
    Check(RequestGuard::IsAllowedHost("10.0.0.1"), "IPv4 without port");
    Check(RequestGuard::IsAllowedHost("localhost:8080"), "localhost");
    Check(RequestGuard::IsAllowedHost("LocalHost"), "localhost any case");
    Check(RequestGuard::IsAllowedHost("[::1]:8080"), "IPv6 with port");
    Check(RequestGuard::IsAllowedHost("[fe80::1]"), "IPv6 without port");
    Check(RequestGuard::IsAllowedHost("my-mac.local:8080"), "mDNS name");
    Check(!RequestGuard::IsAllowedHost(".local"), "bare .local");
    Check(!RequestGuard::IsAllowedHost("evil.example.com:8080"), "public domain");
    Check(!RequestGuard::IsAllowedHost("192.168.0.34.evil.com"), "IP-looking domain");
    Check(!RequestGuard::IsAllowedHost("256.1.1.1"), "octet out of range");
    Check(!RequestGuard::IsAllowedHost("1.2.3"), "three octets");
    Check(!RequestGuard::IsAllowedHost("localhost.evil.com"), "localhost prefix");
}

// ---- StreamId --------------------------------------------------------------

void StreamIdGeneration()
{
    const auto first = StreamId::Generate();
    const auto second = StreamId::Generate();
    Check(first.size() == 32, "32 hex digits");
    Check(first.find_first_not_of("0123456789abcdef") == std::string::npos, "lowercase hex only");
    Check(RequestGuard::IsSafePathSegment(first), "safe as a path segment");
    Check(first != second, "a new ID every time");
}

void StreamIdFromUrl()
{
    Check(StreamId::FromStreamUrl("http://10.0.0.2:8080/stream/abc123/index.mpd") == "abc123", "stream URL");
    Check(StreamId::FromStreamUrl("abc123/index.mpd") == std::nullopt, "relative address");
    Check(StreamId::FromStreamUrl("http://10.0.0.2:8080/stream/abc123") == std::nullopt, "no file");
    Check(StreamId::FromStreamUrl("http://10.0.0.2:8080/stream//index.mpd") == std::nullopt, "empty ID");
    Check(StreamId::FromStreamUrl("http://10.0.0.2:8080/cover/abc123") == std::nullopt, "cover URL");
    Check(StreamId::EntryPath("abc123", "3.vtt") == "abc123/3.vtt", "entry path");
}

// ---- LAN address -----------------------------------------------------------

void LanAddressChoice()
{
    using Kind = NetworkInterfaceKind;
    const std::vector<NetworkInterface> vpnOn = {
        {"lo0", "127.0.0.1", Kind::Loopback},
        {"en0", "192.168.1.20", Kind::Other},
        {"utun4", "10.8.0.2", Kind::Tunnel},
    };
    Check(NetworkUtils::ChooseLanAddress(vpnOn, "10.8.0.2") == "192.168.1.20", "full-tunnel VPN skipped");
    Check(NetworkUtils::ChooseLanAddress(vpnOn, "192.168.1.20") == "192.168.1.20", "default route kept");

    const std::vector<NetworkInterface> wiredAndWifi = {
        {"wlan0", "192.168.1.30", Kind::WiFi},
        {"eth0", "192.168.1.31", Kind::Ethernet},
        {"vbox", "169.254.3.4", Kind::Ethernet},
    };
    Check(NetworkUtils::ChooseLanAddress(wiredAndWifi, "") == "192.168.1.31", "Ethernet over Wi-Fi");
    Check(NetworkUtils::ChooseLanAddress(wiredAndWifi, "169.254.3.4") == "192.168.1.31", "link-local skipped");

    const std::vector<NetworkInterface> onlyTunnel = {{"utun4", "10.8.0.2", Kind::Tunnel}};
    Check(NetworkUtils::ChooseLanAddress(onlyTunnel, "10.8.0.2") == "10.8.0.2", "falls back to default route");
    Check(NetworkUtils::ChooseLanAddress({}, "") == "", "nothing at all");
}

// ---- SessionPolicy ---------------------------------------------------------

void SessionEndsAfterQuietPeriod()
{
    using namespace std::chrono_literals;
    const SessionPolicy::TimePoint start{};
    SessionPolicy policy;
    policy.OnStreamStarted(start);
    Check(!policy.ShouldClose(start + 10s, start + 9s), "open while fetching");

    policy.OnPlaybackEnded(start + 10s);
    Check(!policy.ShouldClose(start + 20s, start + 12s), "a straggler fetch doesn't cancel the end");
    policy.OnPlaybackEnded(start + 30s);
    Check(policy.ShouldClose(start + 40s, start + 12s), "closes 30 s after the first end");
}

void SessionEndCancelled()
{
    using namespace std::chrono_literals;
    const SessionPolicy::TimePoint start{};

    SessionPolicy byFetch;
    byFetch.OnStreamStarted(start);
    byFetch.OnPlaybackEnded(start + 10s);
    Check(!byFetch.ShouldClose(start + 60s, start + 20s), "a new fetch after the end keeps it open");

    SessionPolicy byNewStream;
    byNewStream.OnStreamStarted(start);
    byNewStream.OnPlaybackEnded(start + 10s);
    byNewStream.OnStreamStarted(start + 12s);
    Check(!byNewStream.ShouldClose(start + 60s, std::nullopt), "a new stream cancels the end");
}

void SessionIdleBackstop()
{
    using namespace std::chrono_literals;
    const SessionPolicy::TimePoint start{};

    SessionPolicy idle;
    idle.OnStreamStarted(start);
    Check(!idle.ShouldClose(start + 1h, start + 1min), "paused an hour");
    Check(idle.ShouldClose(start + 2h + 2min, start + 1min), "2 hours without a fetch");

    SessionPolicy paused;
    paused.OnStreamStarted(start);
    paused.OnStillPlaying(start + 3h);
    Check(!paused.ShouldClose(start + 3h + 1min, start + 1min), "a paused status keeps it open");
}
}  // namespace

// ./MangoTests [text]: only the tests whose name contains text.
// library_folders.json: "folderId", and "id" from before it still read.
void LibraryFolderJson()
{
    std::vector<LibraryFolder> folders;
    const auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(folders,
        std::string(R"([{"id":"old","rootPath":"/a"},{"folderId":"new","rootPath":"/b"}])"));
    Check(!error && folders.size() == 2, "old and new folders read");
    Check(folders.size() == 2 && folders[0].legacyId == "old" && folders[0].folderId.empty(), "old \"id\" kept as legacyId");
    Check(folders.size() == 2 && folders[1].folderId == "new" && !folders[1].legacyId, "new \"folderId\"");

    LibraryFolder folder;
    folder.folderId = "f1";
    const auto json = glz::write_json(folder).value_or("");
    Check(json.contains(R"("folderId":"f1")") && !json.contains(R"("id")"), "written with folderId only");
}

int main(int argc, char** argv)
{
    const std::string filter = argc > 1 ? argv[1] : "";

    std::vector<TestCase> tests = {
        {"StreamTarget equality", TargetEquality},
        {"interlaced video is converted", InterlacedVideoIsConverted},
        {"library folder JSON", LibraryFolderJson},
        {"device profile overrides", DeviceProfileOverrides},
        {"device profile remember", DeviceProfileRemember},
        {"device profile validation", DeviceProfileValidation},
        {"device profile JSON", DeviceProfileJson},
        {"ffmpeg progress parsing", ProgressParsing},
        {"ffmpeg log classification", LogClassification},
        {"fake backend", FakeBackendIsUsed},
        {"ffmpeg DASH conversion", FfmpegConvertsDash},
        {"ffmpeg HLS conversion", FfmpegConvertsHls},
        {"ffmpeg missing file", FfmpegReportsMissingFile},
        {"ffmpeg unreadable file", FfmpegReportsUnreadableFile},
        {"external audio detection", ExternalAudioDetection},
        {"external file depth", ExternalFileDepth},
        {"external file release layout", ExternalFileReleaseLayout},
        {"external name tags", ExternalNameTagsParsing},
        {"external audio merge", ExternalAudioMerge},
        {"ffmpeg DASH with external audio", FfmpegConvertsExternalAudioDash},
        {"ffmpeg HLS with external audio and subtitle", FfmpegConvertsExternalAudioHls},
        {"playback end classification", PlaybackEndClassification},
        {"log line format", LogLineFormat},
        {"log level filtering", LogLevelFiltering},
        {"log size cap", LogSizeCap},
        {"log carry-over", LogCarryOver},
        {"log closes before delete", LogClosesBeforeDelete},
        {"request guard path segments", SafePathSegments},
        {"request guard hosts", AllowedHosts},
        {"stream ID generation", StreamIdGeneration},
        {"stream ID from URL", StreamIdFromUrl},
        {"LAN address choice", LanAddressChoice},
        {"session ends after quiet period", SessionEndsAfterQuietPeriod},
        {"session end cancelled", SessionEndCancelled},
        {"session idle backstop", SessionIdleBackstop},
    };

    std::ranges::move(SessionTestCases(), std::back_inserter(tests));
    std::ranges::move(RemoteTestCases(), std::back_inserter(tests));
    std::ranges::move(ForwardingTestCases(), std::back_inserter(tests));
    std::ranges::move(TrackSelectionTestCases(), std::back_inserter(tests));
    std::ranges::move(FFmpegInstallTestCases(), std::back_inserter(tests));
    std::ranges::move(EncoderOptionTestCases(), std::back_inserter(tests));
#if defined(__linux__)
    std::ranges::move(ControlTestCases(), std::back_inserter(tests));
#endif

    for (const auto& test : tests)
    {
        if (!std::string_view(test.name).contains(filter))
        {
            continue;
        }
        const int before = failures;
        test.run();
        std::cout << (failures == before ? "[ ok ] " : "[FAIL] ") << test.name << '\n';
    }

    std::cout << (failures == 0 ? "All tests passed\n" : "Some tests failed\n");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
