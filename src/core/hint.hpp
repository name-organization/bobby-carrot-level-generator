// Hint logic: can the level still be won from the current state, and how?
//
// The live grid is the state dump. Collected carrots are already
// T_CARROT_TAKEN and used spikes are already T_HOLE, so the grid plus Bobby's
// cell says everything. Three checks run in order:
//
//   1. Flood      BFS over the map: is every remaining carrot and the exit
//                 still reachable at all?                  -> fast "dead end"
//   2. Reference  BFS over safe cells back to the reference path, then the
//                 rest of that path                        -> fast "winnable"
//   3. Solver     SAT search from the current state        -> exact answer
//
// Only the base cell set is covered: ground, carrots, spikes (cracked tiles),
// start and exit. See docs/smt-formalization.md for the model.
#pragma once

#include <vector>

#include "level.hpp"
#include "types.hpp"

namespace bc::hint {

enum class Verdict { Winnable, DeadEnd, Unknown };
enum class Stage { Won, Flood, Reference, Solver, Unsupported };

struct Result {
    Verdict verdict = Verdict::Unknown;
    Stage stage = Stage::Unsupported;
    // Moves from Bobby's cell to the exit. Filled only for Verdict::Winnable.
    std::vector<Dir> moves;
};

struct Options {
    // Total SAT conflicts the solver stage may spend before giving up with
    // Verdict::Unknown.
    long long conflictBudget = 400000;
    // false: stop after the two cheap stages. When the reference cannot be
    // rejoined the answer is Verdict::Unknown with Stage::Reference.
    bool useSolver = true;
};

// False when the grid holds tiles outside the base cell set (conveyors, keys,
// rails, ...); compute() then answers Stage::Unsupported.
bool supported(const Grid& grid);

// Cells visited by `moves` starting at `start`, including `start` itself.
std::vector<Coord> cellsAlong(Coord start, const std::vector<Dir>& moves);

// `reference` is a winning path as cells, starting anywhere (normally the
// level start). After a Winnable answer, cellsAlong(pos, result.moves) is the
// reference to pass next time.
Result compute(const Grid& grid, Coord pos, const std::vector<Coord>& reference,
               const Options& options = {});

const char* stageName(Stage stage);
const char* verdictName(Verdict verdict);

}  // namespace bc::hint
