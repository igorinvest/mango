#pragma once

#include <filesystem>
#include "../../pipeline/MediaInfo.hpp"
#include "../MediaBackend.hpp"

class FFmpegWrapper
{
public:
 static std::optional<MediaInfo> Read(const std::filesystem::path &filePath);
 static void PrintMetadata(const MediaInfo &metadata);

 // A light ffprobe of a standalone audio file's first audio stream (no
 // 100M analyze window: it holds one stream). The report goes to a temporary
 // file, removed afterwards. std::nullopt when ffprobe fails or the file
 // has no audio stream.
 static std::optional<AudioFileInfo> ReadAudioFile(const std::filesystem::path &filePath);

 // Fills in VideoTrack::rotation / interlaced and the audio and subtitle
 // tracks' disposition flags for tracks probed before they were read, from the raw ffprobe report Read() saved
 // (FileRecord::getMetadataFilePath) - no ffprobe run needed. Returns
 // whether anything was filled in.
 static bool BackfillTrackFields(MediaInfo &info, const std::filesystem::path &metadataFilePath);

};
