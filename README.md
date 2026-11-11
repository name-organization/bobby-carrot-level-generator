# Bobby Carrot Level Generator

Here is C++ / WebAssembly implementation of Bobby Carrot game.

Dependencies: SDL2 library, game assets. `make run` installs them locally.

To build and run Docker container with game:
```bash
$ make run
```
This downloads assets (images, sound, etc.) and SDL2 library. Then Docker builds an image and runs it.
Game available at [http://localhost:8080](http://localhost:8080)

## How to play

1. Open [http://localhost:8080](http://localhost:8080).
2. Choose a level:
  - **Generate level** makes a new level: pick **Random seed**, or
    **Enter seed** and type one (1 … 4294967295) to get that seed's level, or
  - **Select level** opens the list: click a level number. Levels unlock one
    by one, and progress is saved in the browser.
3. Collect every carrot (in egg levels, walk over every patch of grass). The
   counter in the top right shows how many are left.
4. Move with the arrow keys; on a phone, swipe.
5. Stuck? Press <kbd>H</kbd> to see the path to the exit.
6. Board turned red? The level can't be finished any more: press <kbd>R</kbd>
   to restart. If Bobby falls into a hole, the level restarts by itself.
7. Walk onto the exit when it opens.
8. The next level starts by itself.
9. Press <kbd>Q</kbd> to go back to the start page.
10. **Settings** on the start page, or <kbd>V</kbd> anywhere, sets the music
    and sound volume and mutes the game. Opened over a level, it pauses the
    level, which goes on where it was when Settings closes. The settings are
    saved in the browser with the progress.

The music is the original game's theme; it loops behind every screen but the
ending.

On the start page, Settings and New level the buttons are picked with
<kbd>↑</kbd> <kbd>↓</kbd> or the mouse and pressed with <kbd>Enter</kbd> or a
click; <kbd>Esc</kbd> goes back. The keys for the current screen or page are
listed under the game; each of them can also be clicked. All keys:

| Key | Action |
|---|---|
| <kbd>↑</kbd> <kbd>↓</kbd> <kbd>←</kbd> <kbd>→</kbd> | move / choose; in Settings <kbd>←</kbd> <kbd>→</kbd> set the selected volume |
| <kbd>Enter</kbd> | confirm, start the selected level |
| <kbd>H</kbd> | show or hide the hint |
| <kbd>R</kbd> | restart the level |
| <kbd>G</kbd> | generate a new level: random seed or a given one |
| <kbd>B</kbd> | size of generated maps: 16, 24 or 32 cells a side (generates one in a generated level) |
| <kbd>T</kbd> | carrot / egg levels (not in a generated level) |
| <kbd>E</kbd> | copy a link to the generated level (its seed and map size) |
| <kbd>Esc</kbd> | back: from a level to the level list, from the list to the main menu; closes Settings (back to the level) and New level |
| <kbd>Q</kbd> | main menu |
| <kbd>F</kbd> | whole map / scrolling close-up view |
| <kbd>M</kbd> | sound on / off |
| <kbd>V</kbd> | sound settings: music and sound volume, mute (pauses the level) |

## `.lvl` format

A level is a plain-text map, one character per cell, of up to 32 × 32 cells;
the original levels are 16 × 16. Files: `assets/v1/level/normal01.lvl` …
`normal30.lvl` and `egg01.lvl` … `egg20.lvl`.

- Map rows top to bottom, at most 32 of them and at most 32 characters each.
  The level is as high as there are rows and as wide as the longest row; a
  shorter row, or an empty line, is filled up with path.
- A line starting with `;` is a comment and not a row. The first comment is the
  level name.
- Line endings: LF or CRLF.
- One start `0` and one exit `a` per level.
- If the map has carrots it is a carrot level, otherwise an egg level (grass
  counts).
- Any other character is an error that names its line and column.

```
; Example
################
#0  *    *    a#
################
```

(The example is a 16 × 3 level; the original levels have 16 rows.)

| Char | Cell |
|---|---|
| `#` `1`–`9` `(` `)` | hedge, impassable |
| `=` `"` `{` `}` `[` `]` | fence, impassable |
| space, `.`, `C` | path |
| `0` | start |
| `a` (or `@`) | exit; opens once everything is collected |
| `*` | carrot |
| `X` | grass (egg levels): counted when Bobby leaves it, then turns into `x` and cannot be entered again |
| `_` | cracked floor: turns into a hole `W` when Bobby leaves it; stepping into `W` kills him |
| `K` `G` | grey key, grey lock (one key opens one lock) |
| `Y` `H` | yellow key, yellow lock |
| `R` `D` | red key, red lock |
| `-` `\|` | rail, horizontal / vertical: passable only along it, turns 90° each time Bobby leaves it |
| `F` `T` `J` `L` | rail corner joining east–south, west–south, west–north, north–east: entered and left only through those sides, turns clockwise each time Bobby leaves it |
| `b` / `B` | red button, active / used: stepping on `b` turns every rail and rail corner, and swaps every `b` and `B` |
| `n` / `N` | yellow button, active / used: stepping on `n` reverses every conveyor, and swaps every `n` and `N` |
| `<` `>` `^` `V` | conveyor: carries Bobby in its direction; cannot be entered from the side or head-on |

`o` (a taken carrot) and `?` are internal and do not appear in files. The
table matches `kTileChars` in `src/core/tilechar.hpp`.

## Level generator

### Sharing a generated level

A generated level is fully given by its seed and map size: the generator uses
its own random numbers (`src/core/rng.hpp`) and limits the solver by a count,
not a time, so a seed gives the same level natively and in the browser.
<kbd>E</kbd> puts both into the page address and copies it, e.g.
`http://localhost:8080/?seed=123456&size=24`; opening that link starts the same
level. `size` is 16, 24 or 32 and may be left out for 16. The native build
prints `--seed=… --size=…` instead and takes the same two options on start.
Levels made with <kbd>B</kbd> and the ones that follow a finished generated
level get a random seed without asking.

<kbd>G</kbd> builds a level from rooms (`src/core/roomgen.{hpp,cpp}`, the
Spelunky approach): the map is cut into a grid of room slots, a random path of
rooms runs from the start to the exit, side rooms hang off it, and more doors
between neighbouring rooms close cycles, so that most rooms open into two to
four others and there is more than one way through. Any door may be spikes:
crossed once, at the wrong moment it cuts Bobby off from what lies behind it.

The rooms are filled by wave function collapse (`src/core/wfc.{hpp,cpp}`):
every 3 × 3 window of the 30 original carrot levels, written in five symbols
(hedge, fence, path, carrot, spike) and taken in all eight orientations, is a
pattern, and the map is filled so that every 3 × 3 window in it is one of them
-- the whole map in one run first, so that each wall is chosen knowing both
rooms it separates, and room by room (each with its walls) if that fails. The
layout pins the doors, the cells they lead to, the start and the exit; a
one-cell wall between two rooms may come out hedge or fence. A room WFC cannot
fill (about 4%) gets fixed patterns (carrot fields, rows, pillars) instead. The
model is learned from `assets/v1/level` on the first <kbd>G</kbd>.

Hedge and fence pieces are then picked from their neighbours by
`src/core/autotile.{hpp,cpp}`, whose tables were read off the 50 original
levels. Every level is checked with the hint solver: the spikes are put in
one at a time, door spikes first, each kept only if some order still wins;
more are added towards a target (six on 16 × 16), each also leaving at most two
of the 3 × 3 windows around it unlike the originals. Each check walks the
current solution, and only when the new spike breaks it does the solver look
for another way, a few times a level at most. The solution is handed to the hint as the reference. The
seed and the time it took are printed to the browser console, and the seed gives
the same level in the bench.

The generator works on a `Canvas` of any size (`src/core/canvas.hpp`); up to
32 × 32 it is verified and played. <kbd>B</kbd> picks 16, 24 or 32 cells a side;
`roomgen::Params::forSize` sets the room size (4, 5, 6), the carrot weight and
the spike target for each, with what they measured.

The bench generates levels, verifies them, replays each solution through the
real `Game` and compares area, carrots, spikes and 3 × 3 patterns with the
originals:

```bash
$ tools/build_hint_tests.sh && build-hint/gen_bench --count 5      # print 5 levels
$ build-hint/gen_bench --count 300 -q --room 5                   # summary only
$ build-hint/gen_bench --count 300 -q --holdout                  # WFC scored on unseen originals
$ build-hint/gen_bench --count 300 -q --content patterns         # fixed patterns instead of WFC
$ build-hint/gen_bench --algo walk --count 300 -q                # the old random walk
$ build-hint/gen_bench --size 32x32 --count 3                     # a bigger map
```

The summary also has the room graph (doors per room, cycles) and how often a
naive player, who always walks to the nearest carrot, cuts himself off: 43% of
tries on 16 × 16 levels, 59% on the original levels without extra mechanics.

"patterns" in the summary is the share of a level's 3 × 3 windows that also
occur in an original. With `--holdout` WFC learns from the odd originals and is
scored against the even ones; the originals score 37% against each other, the
fixed patterns 40% and WFC 48% (500 levels, 4 × 4 rooms). The knobs behind
these numbers (carrot and hedge weights, added spikes, fill) are documented in
`roomgen::Params` with what each setting measured.

All options are listed at the top of `tests/gen_bench.cpp`.

## Hint

Press <kbd>H</kbd> during a level to show a path to the exit, press it again to
hide it. The path follows Bobby; stepping off it re-routes back to the
remembered route. On a dead end the board turns red.

- `src/core/hint.{hpp,cpp}` -- the hint logic: flood fill, return to the
  reference path, then a SAT check on the map collapsed to regions and spikes.
  Model and proofs: `docs/smt-formalization.md`.
- `src/core/sat.{hpp,cpp}` -- thin wrapper over the CaDiCaL SAT solver
  (`src/third_party/cadical`, MIT).
- `Game::toggleHint()`, `Game::hint()`, `Game::hintPath()` -- the API for the
  front end. `Game::setReference()` takes the reference solution once the level
  generator ships one; until then the first hint finds it with the solver.
- `src/gfx/hint_overlay.{hpp,cpp}` -- temporary debug drawing of the path; how
  to remove it is described at the top of the header.

Tests and the prototype:

```bash
$ tools/build_hint_tests.sh && build-hint/hint_game   # hint through Game, levels 1-7
$ pip install z3-solver && tools/smt_solver.py assets/v1/level/normal01.lvl
```

`tools/smt_solver.py` is the Python/z3 prototype of the move-by-move encoding
from the document (shortest solutions, `--selftest` against brute force).
