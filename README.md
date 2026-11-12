# Bobby Carrot Level Generator

> [!NOTE]  
> Игра доступна по адресу https://name-organization.github.io/bobby-carrot-level-generator/

Проект представляет из себя кросс-платформенную реализацию игры Bobby Carrot с дополнительным функционалом:
- генерация уровней различной сложности и размеров
- навигация по уровням, основанная на SAT-решателе
- экспорт/импорт сгенерированных уровней
- улучшенный пользовательский интерфейс

Программный стек:
- Игра написана на C++, компилируется в WASM (WebAssembly) с помощью `Emscripten`.
- Библиотеки: `SDL2` (графика), `CaDiCaL` (SAT-решатель)
- Для сборки используется `Docker`

## Сборка

Проверено на WSL2, Ubuntu 24.04, Fedora, Mac OS.
Для сборки проекта нужен `Docker`, `Docker Compose`, `make`.

Чтобы собрать и запустить проект:
```bash
$ make run
```
Эта команда скачивает зависимости: SDL2, игровые ассеты (текстуры, звуки).

В случае проблем с командой `make run` (ненулевой ход выхода со всякой ошибкой) стоит попробовать собрать вручную:
```bash
# Сборка
wget -O docker/sdl-release-2.32.10.zip https://github.com/libsdl-org/SDL/archive/release-2.32.10.zip
tools/fetch_assets.sh
docker compose build bobby
# Запуск игры в контейнере
docker compose up
```

По умолчанию игра запускается на порту `8080`, можно изменить в `docker-compose.yaml`.

## Как играть

1. Откройте https://name-organization.github.io/bobby-carrot-level-generator/ (`localhost:8080`, если собираете вручную).
2. Выберите уровень:
  - **Generate level** создает новый уровень: нажмите на **Random seed**, или
    **Enter seed** и введите значение сида (1 … 4294967295), чтобы получить уровень, соответствующий этому сиду, или
  - **Select level**, который открывает меню с выбором оригинальных уровней игры. Уровени открываются один за другим, 
  прогресс сохраняется в браузере (local storage).
3. Соберите все морковки (в уровнях с яйцами нужно пройти все клетки с травой). Счетчик в правом верхнем углу показывает, сколько морковок осталось.
4. Двигайтесь с помощью стрелочек; на телефоне стрелочки на экране.
5. Застряли? Нажмите на клавишу `H` (hint), чтобы увидеть путь до финиша.
6. Экран покраснел? Уровень не может быть пройден до конца: нажмите `R`
   для начала заново.
7. После сбора всех морковок, доберитесь до мигающего финиша.
8. Следующий уровень стартует автоматически.
9. Нажмите `Q` для выхода в главное меню.
10. **Settings** в главном меню позволяют настраивать громкость звуков и музыки.

Фоновая музыка -- оригинальная тема из игры.

На стартовой странице, в разделах «Настройки» и «Новый уровень» кнопки выбираются с помощью <kbd>↑</kbd> <kbd>↓</kbd> или мыши и нажимаются клавишей <kbd>Enter</kbd> или щелчком; <kbd>Esc</kbd> возвращает назад. Клавиши для текущего экрана или страницы перечислены под игрой; каждую из них также можно нажать. Все клавиши:

| Клавиша | Действие |
|---|---|
| <kbd>↑</kbd> <kbd>↓</kbd> <kbd>←</kbd> <kbd>→</kbd> | перемещение / выбор; в настройках <kbd>←</kbd> <kbd>→</kbd> задают выбранную громкость |
| <kbd>Enter</kbd> | подтвердить, начать выбранный уровень |
| <kbd>H</kbd> | показать или скрыть подсказку |
| <kbd>R</kbd> | перезапустить уровень |
| <kbd>G</kbd> | сгенерировать новый уровень: случайное зерно или заданное |
| <kbd>B</kbd> | размер генерируемых карт: 16, 24 или 32 клетки по стороне (генерирует одну в сгенерированном уровне) |
| <kbd>T</kbd> | уровни с морковкой / яйцом (не в сгенерированном уровне) |
| <kbd>E</kbd> | скопировать ссылку на сгенерированный уровень (его зерно и размер карты) |
| <kbd>Esc</kbd> | назад: с уровня к списку уровней, из списка в главное меню; закрывает «Настройки» (возврат к уровню) и «Новый уровень» |
| <kbd>Q</kbd> | главное меню |
| <kbd>F</kbd> | вся карта / прокручиваемый вид крупным планом |
| <kbd>M</kbd> | звук включён / выключен |
| <kbd>V</kbd> | настройки звука: громкость музыки и звуков, без звука (приостанавливает уровень) |

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

A generated level is fully given by its seed, map size and difficulty: the
generator uses its own random numbers (`src/core/rng.hpp`), limits the solver by
a count, not a time, and scores levels in integers, so a seed gives the same
level natively and in the browser. <kbd>E</kbd> puts all three into the page
address and copies it, e.g.
`http://localhost:8080/?seed=123456&size=24&difficulty=hard`; opening that link
starts the same level. `size` is 16, 24 or 32 and may be left out for 16.
`difficulty` is `easy`, `medium` or `hard`; a link without one is a link from
before difficulties and gives the level it always gave (the first medium level
of the seed, not picked by its score). The native build prints
`--seed=… --size=… --difficulty=…` instead and takes the same options on start.
Levels made with <kbd>G</kbd> and the ones that follow a finished generated
level keep the map size and difficulty and get a random seed without asking.

### Difficulty

A generated level is easy, medium or hard. `src/core/difficulty.{hpp,cpp}`
scores a level from 0 to 100 out of three measures, each compared with what is
typical of a map of its size (so the difficulties mean the same on 16, 24 and
32 cells):

- **cover**: moves of the winning solution the generator verified the level
  with, per walkable cell -- how many times over Bobby walks the island;
- **spikes**: spikes per 100 walkable cells;
- **naive**: how often a player who always goes for the nearest carrot cuts
  himself off with a spike (out of 20 tries); it counts twice.

Below 38 is easy, 62 and up is hard, medium lies between. A difficulty first
sets the generator's parameters (`roomgen::Params::forSize`: easy has few spike
doors and spikes and a smaller island, hard spikes every door it can, twice the
spikes and fewer cycles), then up to three levels are built (the first from
the seed itself) and the first whose score lands in the difficulty's band is
kept, or the closest of the three. The score is printed with the level in the
browser console.

The generator builds a level from rooms (`src/core/roomgen.{hpp,cpp}`, the
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
model is learned from `assets/v1/level` with the first generated level.

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
32 × 32 it is verified and played. The New level window picks 16, 24 or 32 cells a side;
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
