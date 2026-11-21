#include "RemoteTarget.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include <set>

#include "../../configuration/EncodingPreset.hpp"
#include "../../configuration/VideoTranscodeMode.hpp"
#include "../../pipeline/stream/profiles/SupportedFormats.hpp"

namespace mango::remote::RemoteTarget
{

namespace
{

using Json = glz::generic;
using Object = Json::object_t;

const std::vector<std::string> kPixelFormats{"yuv420p", "yuv420p10le"};
const std::vector<std::string> kAudioEncoders{"aac"};

bool Contains(const std::vector<std::string>& list, const std::string& value)
{
    return std::ranges::find(list, value) != list.end();
}

// An empty list from an older A means it didn't say: anything goes.
bool Accepted(const std::vector<std::string>& accepted, const std::string& value)
{
    return accepted.empty() || Contains(accepted, value);
}

std::optional<Json> ParseJson(std::string_view text)
{
    Json json;
    if (glz::read_json(json, text))
    {
        return std::nullopt;
    }
    return json;
}

template <typename T>
Json ToJson(const T& value)
{
    return ParseJson(glz::write_json(value).value_or("{}")).value_or(Json{});
}

std::string StringAt(const Json& object, const std::string& key)
{
    if (!object.holds<Object>() || !object.contains(key))
    {
        return {};
    }
    const auto* text = object[key].get_if<std::string>();
    return text ? *text : std::string();
}

// ---- B: fitting its target to A's example ----------------------------------

class Fitter
{
   public:
    Fitter(const InitiateAccepts& accepts, std::vector<std::string>& notApplied)
        : m_accepts(accepts), m_notApplied(notApplied)
    {
    }

    // example's fields, with mine's values where B has them.
    Json MergeObject(const Json& example, const Json& mine)
    {
        if (!example.holds<Object>() || !mine.holds<Object>())
        {
            return example;
        }

        Json result = example;
        for (const auto& [key, value] : mine.get<Object>())
        {
            if (!example.contains(key))
            {
                NotApplied(key);  // A doesn't know the field.
                continue;
            }
            result[key] = MergeValue(key, example[key], value);
        }
        return result;
    }

    void ApplyAccepts(Json& result, const Json& example)
    {
        ReplaceIfNotAccepted(result, example, "playlist", m_accepts.playlist);
        FitPreset(result);
        FitMode(result);
        if (result.contains("encoding") && example.contains("encoding"))
        {
            ReplaceIfNotAccepted(result["encoding"], example["encoding"], "videoPixFmt", m_accepts.videoPixFmt);
            ReplaceIfNotAccepted(result["encoding"], example["encoding"], "audioCodec", m_accepts.audioCodec);
        }
    }

   private:
    Json MergeValue(const std::string& key, const Json& example, const Json& mine)
    {
        if (key == "videoCapabilities" || key == "audioCapabilities")
        {
            const auto& codecs = key == "videoCapabilities" ? m_accepts.videoCodecs : m_accepts.audioCodecs;
            return MergeCapabilities(example, mine, codecs);
        }
        if (example.holds<Object>())
        {
            return MergeObject(example, mine);
        }
        if (example.data.index() != mine.data.index())
        {
            NotApplied(key);  // A reads it as another type: its value stays.
            return example;
        }
        return mine;
    }

    // Each of mine's entries with only the subfields of the example's
    // entries, and only for codecs A accepts.
    Json MergeCapabilities(const Json& example, const Json& mine, const std::vector<std::string>& codecs)
    {
        const auto* mineEntries = mine.get_if<Json::array_t>();
        const auto* exampleEntries = example.get_if<Json::array_t>();
        if (!mineEntries || !exampleEntries)
        {
            return example;
        }

        const auto pattern = EntryPattern(*exampleEntries);
        Json::array_t result;
        for (const auto& entry : *mineEntries)
        {
            const auto codec = StringAt(entry, "codec");
            if (!Accepted(codecs, codec))
            {
                NotApplied(codec);
                continue;
            }
            result.push_back(pattern ? MergeObject(*pattern, entry) : entry);
        }
        Json array;
        array.data = std::move(result);  // Json{result} would nest it in another array.
        return array;
    }

    // Every subfield any example entry has, with the first one's values.
    static std::optional<Json> EntryPattern(const Json::array_t& entries)
    {
        std::optional<Json> pattern;
        for (const auto& entry : entries)
        {
            if (!entry.holds<Object>())
            {
                continue;
            }
            if (!pattern)
            {
                pattern = entry;
                continue;
            }
            for (const auto& [key, value] : entry.get<Object>())
            {
                if (!pattern->contains(key))
                {
                    (*pattern)[key] = value;
                }
            }
        }
        return pattern;
    }

    void ReplaceIfNotAccepted(
        Json& result, const Json& example, const std::string& key, const std::vector<std::string>& accepted)
    {
        const auto value = StringAt(result, key);
        if (!result.contains(key) || Accepted(accepted, value))
        {
            return;
        }
        NotApplied(value);
        result[key] = example[key];
    }

    // The largest preset A has that isn't bigger than mine.
    void FitPreset(Json& result)
    {
        const auto value = StringAt(result, "preset");
        const auto mine = EncodingPresetSetting::FromString(value);
        if (!result.contains("preset") || !mine || Accepted(m_accepts.preset, value))
        {
            return;
        }

        std::optional<std::string> fitted;
        for (const auto presetId : EncodingPresetSetting::kAll)
        {
            const auto name = EncodingPresetSetting::ToString(presetId);
            if (Contains(m_accepts.preset, name) && (presetId <= *mine || !fitted))
            {
                fitted = name;
            }
        }
        NotApplied(value);
        if (fitted)
        {
            result["preset"] = *fitted;
        }
    }

    void FitMode(Json& result)
    {
        const auto value = StringAt(result, "mode");
        if (!result.contains("mode") || Accepted(m_accepts.mode, value))
        {
            return;
        }
        NotApplied(value);
        result["mode"] = VideoTranscodeModeSetting::ToString(VideoTranscodeMode::Automatic);
    }

    void NotApplied(const std::string& item)
    {
        if (!item.empty() && !Contains(m_notApplied, item))
        {
            m_notApplied.push_back(item);
        }
    }

    const InitiateAccepts& m_accepts;
    std::vector<std::string>& m_notApplied;
};

// ---- A: reading a target -----------------------------------------------------

constexpr glz::opts kStrict{.error_on_unknown_keys = true, .error_on_missing_keys = true};

std::optional<int> ParseKbps(const std::string& bitrate)
{
    if (!bitrate.ends_with('k'))
    {
        return std::nullopt;
    }
    int value = 0;
    const auto* end = bitrate.data() + bitrate.size() - 1;
    const auto [stop, error] = std::from_chars(bitrate.data(), end, value);
    if (error != std::errc() || stop != end || value <= 0 || value > kMaxBitrateKbps)
    {
        return std::nullopt;
    }
    return value;
}

bool InRange(int value, int max) { return value > 0 && value <= max; }

std::optional<std::string> CheckVideo(const std::vector<VideoCodecConfig>& capabilities)
{
    if (capabilities.size() > kMaxCapabilities)
    {
        return "videoCapabilities";
    }
    for (const auto& entry : capabilities)
    {
        if (!Contains(SupportedFormats::kVideoCodecs, entry.codec))
        {
            return "videoCapabilities.codec";
        }
        if (!InRange(entry.maxWidth, kMaxWidth) || !InRange(entry.maxHeight, kMaxWidth) ||
            std::min(entry.maxWidth, entry.maxHeight) > kMaxHeight)
        {
            return "videoCapabilities.maxWidth";
        }
        if (!InRange(entry.maxFps, kMaxFps))
        {
            return "videoCapabilities.maxFps";
        }
    }
    return std::nullopt;
}

std::optional<std::string> CheckAudio(const std::vector<AudioCodecConfig>& capabilities)
{
    if (capabilities.size() > kMaxCapabilities)
    {
        return "audioCapabilities";
    }
    for (const auto& entry : capabilities)
    {
        if (!Contains(SupportedFormats::kAudioCodecs, entry.codec))
        {
            return "audioCapabilities.codec";
        }
        if (!InRange(entry.maxChannels, kMaxChannels))
        {
            return "audioCapabilities.maxChannels";
        }
        if (!InRange(entry.maxSampleRate, kMaxSampleRate))
        {
            return "audioCapabilities.maxSampleRate";
        }
    }
    return std::nullopt;
}

std::optional<std::string> CheckEncoding(const InitiateEncoding& encoding)
{
    if (!ParseKbps(encoding.videoBitrate))
    {
        return "encoding.videoBitrate";
    }
    if (!ParseKbps(encoding.audioBitrate))
    {
        return "encoding.audioBitrate";
    }
    if (!Contains(kPixelFormats, encoding.videoPixFmt))
    {
        return "encoding.videoPixFmt";
    }
    if (!Contains(kAudioEncoders, encoding.audioCodec))
    {
        return "encoding.audioCodec";
    }
    return std::nullopt;
}

std::optional<PlaylistType> ReadPlaylist(const std::string& playlist)
{
    if (playlist == "dash")
    {
        return PlaylistType::DASH;
    }
    if (playlist == "hls")
    {
        return PlaylistType::HLS;
    }
    return std::nullopt;
}

// A's built-in encoding defaults, with B's values for the chosen preset.
EncodingDefaults EncodingFor(EncodingPresetId preset, const InitiateEncoding& encoding)
{
    auto defaults = StreamProfiles::BuiltInEncodingDefaults();
    auto& rung = defaults.presets.at(EncodingPresetSetting::ToString(preset));
    rung.videoBitrate = encoding.videoBitrate;
    rung.audioBitrate = encoding.audioBitrate;
    defaults.videoPixFmt = encoding.videoPixFmt;
    defaults.audioCodec = encoding.audioCodec;
    return defaults;
}

std::expected<StreamTarget, std::string> ToStreamTarget(const InitiateTarget& target)
{
    const auto playlist = ReadPlaylist(target.playlist);
    const auto preset = EncodingPresetSetting::FromString(target.preset);
    const auto mode = VideoTranscodeModeSetting::FromString(target.mode);
    if (!playlist)
    {
        return std::unexpected("playlist");
    }
    if (!preset)
    {
        return std::unexpected("preset");
    }
    if (!mode)
    {
        return std::unexpected("mode");
    }

    auto profile = StreamProfiles::BuiltInProfile(*playlist);
    profile.videoCapabilities = target.videoCapabilities;
    profile.audioCapabilities = target.audioCapabilities;
    profile.rotationSupported = target.rotationSupported;
    return StreamTarget{.playlist = *playlist,
        .profile = std::move(profile),
        .preset = *preset,
        .mode = *mode,
        .encoding = EncodingFor(*preset, target.encoding)};
}

}  // namespace

InitiateTarget Describe(const StreamTarget& target, const EncodingDefaults& encoding)
{
    const auto& preset = encoding.Preset(target.preset);
    return {.playlist = target.playlist == PlaylistType::HLS ? "hls" : "dash",
        .preset = EncodingPresetSetting::ToString(target.preset),
        .mode = VideoTranscodeModeSetting::ToString(target.mode),
        .videoCapabilities = target.profile.videoCapabilities,
        .audioCapabilities = target.profile.audioCapabilities,
        .rotationSupported = target.profile.rotationSupported,
        .encoding = {.videoBitrate = preset.videoBitrate,
            .audioBitrate = preset.audioBitrate,
            .videoPixFmt = encoding.videoPixFmt,
            .audioCodec = encoding.audioCodec}};
}

Fitted FitToExample(const InitiateTarget& mine, std::string_view exampleJson)
{
    Fitted fitted;
    const auto example = ParseJson(exampleJson);
    if (!example || !example->holds<Object>() || !example->contains("target") ||
        !(*example)["target"].holds<Object>())
    {
        fitted.json = glz::write_json(mine).value_or("{}");
        return fitted;
    }

    InitiateAccepts accepts;
    if (example->contains("accepts"))
    {
        std::ignore = glz::read<glz::opts{.error_on_unknown_keys = false}>(
            accepts, glz::write_json((*example)["accepts"]).value_or("{}"));
    }

    const auto& exampleTarget = (*example)["target"];
    Fitter fitter(accepts, fitted.notApplied);
    auto result = fitter.MergeObject(exampleTarget, ToJson(mine));
    fitter.ApplyAccepts(result, exampleTarget);
    fitted.json = glz::write_json(result).value_or("{}");
    return fitted;
}

std::expected<StreamTarget, std::string> Read(std::string_view targetJson)
{
    InitiateTarget target;
    if (const auto error = glz::read<kStrict>(target, targetJson))
    {
        return std::unexpected(glz::format_error(error, targetJson));
    }

    if (auto field = CheckVideo(target.videoCapabilities))
    {
        return std::unexpected(*field);
    }
    if (auto field = CheckAudio(target.audioCapabilities))
    {
        return std::unexpected(*field);
    }
    if (auto field = CheckEncoding(target.encoding))
    {
        return std::unexpected(*field);
    }
    return ToStreamTarget(target);
}

}  // namespace mango::remote::RemoteTarget
