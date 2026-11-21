#pragma once

// "Play next video in folder automatically": whether the native apps start
// the next video of a folder section when one finishes. Stored in
// settings.json as "autoplayNextInFolder" (default true). When off, the
// apps still show "Up next" and a Next button.
class AutoplaySetting
{
   public:
    static constexpr bool defaultValue = true;

    static bool Get();
    static void Set(bool enabled);
};
