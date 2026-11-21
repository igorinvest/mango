#include <glaze/glaze.hpp>

#include "../pipeline/library/FileRecord.hpp"
#include "../pipeline/library/LibraryFolder.hpp"
#include "../pipeline/stream/profiles/StreamProfile.hpp"

#pragma once
#include <glaze/glaze.hpp>  // Не забудьте подключить Glaze

// --- РЕГИСТРАЦИЯ ДЛЯ GLAZE ---

template <>
struct glz::meta<VideoTrack>
{
    using T = VideoTrack;
    static constexpr auto value =
        object("index", &T::index, "codec", &T::codec, "width", &T::width, "height", &T::height,
            "fps", &T::fps, "bitrate", &T::bitrate, "language", &T::language, "title", &T::title,
            "rotation", &T::rotation, "interlaced", &T::interlaced);
};

template <>
struct glz::meta<AudioTrack>
{
    using T = AudioTrack;
    static constexpr auto value =
        object("index", &T::index, "codec", &T::codec, "channels", &T::channels, "sampleRate",
            &T::sampleRate, "bitrate", &T::bitrate, "language", &T::language, "title", &T::title);
};

template <>
struct glz::meta<SubtitleTrack>
{
    using T = SubtitleTrack;
    static constexpr auto value = object(
        "index", &T::index, "codec", &T::codec, "language", &T::language, "title", &T::title);
};

template <>
struct glz::meta<ExternalSubtitle>
{
    using T = ExternalSubtitle;
    static constexpr auto value = object("index", &T::index, "path", &T::path, "language",
        &T::language, "title", &T::title, "forced", &T::forced, "hearingImpaired",
        &T::hearingImpaired, "codec", &T::codec, "charset", &T::charset, "fileSizeBytes",
        &T::fileSizeBytes, "lastWriteUnixSeconds", &T::lastWriteUnixSeconds);
};

template <>
struct glz::meta<ExternalAudio>
{
    using T = ExternalAudio;
    static constexpr auto value = object("index", &T::index, "path", &T::path, "language",
        &T::language, "title", &T::title, "commentary", &T::commentary, "codec", &T::codec,
        "channels", &T::channels, "sampleRate", &T::sampleRate, "bitrate", &T::bitrate,
        "durationSeconds", &T::durationSeconds, "startTimeSeconds", &T::startTimeSeconds,
        "fileSizeBytes", &T::fileSizeBytes, "lastWriteUnixSeconds", &T::lastWriteUnixSeconds);
};

template <>
struct glz::meta<AttachmentTrack>
{
    using T = AttachmentTrack;
    static constexpr auto value = object("index", &T::index, "attachmentIndex",
        &T::attachmentIndex, "fileName", &T::fileName, "mimeType", &T::mimeType);
};

template <>
struct glz::meta<AttachedPictureTrack>
{
    using T = AttachedPictureTrack;
    static constexpr auto value = object("index", &T::index, "codec", &T::codec);
};

template <>
struct glz::meta<MediaInfo>
{
    using T = MediaInfo;
    static constexpr auto value = object("videos", &T::videos, "audios", &T::audios, "subtitles",
        &T::subtitles, "attachments", &T::attachments, "attachedPictures", &T::attachedPictures,
        "durationSec", &T::durationSec, "fileSizeBytes", &T::fileSizeBytes, "format",
        &T::format, "title", &T::title);
};

template <>
struct glz::meta<StreamTrackSelection>
{
    using T = StreamTrackSelection;
    static constexpr auto value = object("videoTrackId", &T::videoTrackId, "audioTrackId",
        &T::audioTrackId, "subtitleTrackId", &T::subtitleTrackId);
};

// Регистрация для FileRecord через кастомные функции чтения/записи
template <>
struct glz::meta<FileRecord>
{
    using T = FileRecord;
    static constexpr auto value = object("fastHash", &T::fastHash, "filePath", &T::filePath,
        "securityBookmark", &T::securityBookmark, "libraryName", &T::libraryName, "mediaInfo",
        &T::mediaInfo, "coverFileName", &T::coverFileName, "addedThisSession",
        &T::addedThisSession, "addedAtUnixSeconds", &T::addedAtUnixSeconds, "addedManually",
        &T::addedManually, "folderLocations", &T::folderLocations, "externalSubtitles",
        &T::externalSubtitles, "nextExternalSubtitleIndex", &T::nextExternalSubtitleIndex,
        "externalAudios", &T::externalAudios, "nextExternalAudioIndex",
        &T::nextExternalAudioIndex);
};

template <>
struct glz::meta<FolderLocation>
{
    using T = FolderLocation;
    static constexpr auto value = object("folderId", &T::folderId, "relativePath",
        &T::relativePath, "fileSizeBytes", &T::fileSizeBytes, "lastWriteUnixSeconds",
        &T::lastWriteUnixSeconds);
};

template <>
struct glz::meta<LibraryFolder>
{
    using T = LibraryFolder;
    static constexpr auto value = object("folderId", &T::folderId, "rootPath", &T::rootPath,
        "securityBookmark", &T::securityBookmark, "addedAtUnixSeconds", &T::addedAtUnixSeconds,
        "lastScanUnixSeconds", &T::lastScanUnixSeconds, "id", &T::legacyId);
};

// Inside stream-profiles.json, device-profiles.json and remote streaming's
// hello (see remote/link/HelloMessage.hpp): these keys are part of the remote
// protocol, so they're only ever added to, never renamed.
template <>
struct glz::meta<VideoCodecConfig>
{
    using T = VideoCodecConfig;
    static constexpr auto value = object("codec", &T::codec, "maxWidth", &T::maxWidth, "maxHeight",
        &T::maxHeight, "maxFps", &T::maxFps, "hdrSupported", &T::hdrSupported);
};

template <>
struct glz::meta<AudioCodecConfig>
{
    using T = AudioCodecConfig;
    static constexpr auto value = object(
        "codec", &T::codec, "maxChannels", &T::maxChannels, "maxSampleRate", &T::maxSampleRate);
};

template <>
struct glz::meta<SubtitleCodecConfig>
{
    using T = SubtitleCodecConfig;
    static constexpr auto value = object("codec", &T::codec);
};

// playlist is deliberately omitted - the user-facing stream-profiles.json
// stores this profile under a "dash"/"hls" key already (see StreamProfiles),
// so a "playlist" field inside it would be redundant and StreamProfiles sets
// it explicitly after loading.
template <>
struct glz::meta<StreamProfile>
{
    using T = StreamProfile;
    static constexpr auto value = object("videoCapabilities", &T::videoCapabilities,
        "audioCapabilities", &T::audioCapabilities, "subtitleCapabilities", &T::subtitleCapabilities,
        "defaultFileName", &T::defaultFileName, "defaultExtension", &T::defaultExtension,
        "encryptionRequired", &T::encryptionRequired, "segmentDurationSec", &T::segmentDurationSec,
        "maxBufferLengthSec", &T::maxBufferLengthSec, "sidecarSubtitles", &T::sidecarSubtitles,
        "rotationSupported", &T::rotationSupported);
};
