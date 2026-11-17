#pragma once

#include "tetris/reachability.hpp"

namespace tetris {

// Builds the TOP visual piece shapes used by the existing TetrisVisionPilot
// implementation and a nominal 3x3 / 4x4 rotation-pivot mapping.
//
// This is a geometric baseline, not a verified wall-kick table. No wall-kick
// offsets are added. Validate the pivot mapping against the running client
// before using the result to dispatch live actions.
bool ConfigureTopPieceProfile(PieceCatalog& catalog,
                              ReachabilityOptions& options);

} // namespace tetris
