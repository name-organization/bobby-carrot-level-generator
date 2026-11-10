# Bobby Carrot Level Generator

Here is C++ / WebAssembly implementation of Bobby Carrot game.

Dependencies: SDL2 library, game assets. `make run` installs them locally.

To build and run Docker container with game:
```bash
$ make run
```
This downloads assets (images, sound, etc.) and SDL2 library. Then Docker builds an image and runs it.
Game available at `localhost:8080`

### TODO: add `.lvl` format specification

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
