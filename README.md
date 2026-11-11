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
  - **Generate level** gives a new random level, or
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

The keys for the current screen are listed under the game; each of them can
also be clicked. All keys:

| Key | Action |
|---|---|
| <kbd>↑</kbd> <kbd>↓</kbd> <kbd>←</kbd> <kbd>→</kbd> | move / choose |
| <kbd>Enter</kbd> | confirm, start the selected level |
| <kbd>H</kbd> | show or hide the hint |
| <kbd>R</kbd> | restart the level |
| <kbd>G</kbd> | generate a new level |
| <kbd>T</kbd> | carrot / egg levels (not in a generated level) |
| <kbd>Esc</kbd> | back: from a level to the level list, from the list to the main menu |
| <kbd>Q</kbd> | main menu |
| <kbd>F</kbd> | whole map / scrolling close-up view |
| <kbd>M</kbd> | sound on / off |

## `.lvl` format

A level is a plain-text map of 16 × 16 cells, one character per cell.
Files: `assets/v1/level/normal01.lvl` … `normal30.lvl` and `egg01.lvl` …
`egg20.lvl`.

- Exactly 16 map rows, top to bottom; each row has at most 16 characters.
  A shorter row, or an empty line, is filled up with path.
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

(The example shows 3 rows only; a real file has 16.)

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
