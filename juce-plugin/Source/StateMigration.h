#pragma once

#include <algorithm>

namespace wavelens::state
{
inline constexpr int kCurrentStateVersion = 2;

/**
    Migrates a saved layer timing shift (ms) for PRE layers (0..2).
    In version 1 states, `preRollMs` was stored in the preset but ignored by the
    engine (which defaulted PRE `layerDelay` to -50 ms relative to the lookahead
    anchor). Version 2 automatically subtracts `preRollMs` for PRE layers, so v1
    PRE delays must be shifted by `+preRollMs` once during load to preserve net
    sample alignment without double-applying pre-roll.
*/
inline float migratePreDelayMsForVersion (int stateVersion,
                                          int layerIndex,
                                          int numPreLayers,
                                          float savedDelayShiftMs,
                                          float restoredPreRollMs) noexcept
{
    if (stateVersion >= kCurrentStateVersion || layerIndex < 0 || layerIndex >= numPreLayers)
        return std::max (-100.0f, std::min (100.0f, savedDelayShiftMs));

    const float migrated = savedDelayShiftMs + restoredPreRollMs;
    return std::max (-100.0f, std::min (100.0f, migrated));
}
} // namespace wavelens::state
