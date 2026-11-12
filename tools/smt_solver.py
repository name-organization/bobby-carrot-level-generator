#!/usr/bin/env python3
"""SAT/SMT check for a winning strategy in a Bobby Carrot level.

Prototype of the algorithm from docs/smt-formalization.md. Covers the base
cell set only: path, wall, carrot, spikes (cracked tile), start, exit.

    pip install z3-solver
    tools/smt_solver.py assets/v1/level/normal01.lvl
    tools/smt_solver.py level.txt --check        # compare with brute force
    tools/smt_solver.py --selftest               # random levels vs brute force

Text levels use one character per cell:
    #  wall      .  path      C  carrot      ^  spikes
    x  dead spikes (wall)     S  start       E  exit
"""

import argparse
import collections
import dataclasses
import pathlib
import random
import struct
import sys
import time

import z3

MOVES = {(0, -1): "U", (0, 1): "D", (-1, 0): "L", (1, 0): "R"}

T_GROUND, T_CARROT, T_CARROT_TAKEN, T_START = 18, 19, 20, 21
T_CRACKED, T_HOLE, T_EXIT = 30, 31, 44


@dataclasses.dataclass(frozen=True)
class Level:
    cells: frozenset      # P: walkable cells as (x, y)
    carrots: frozenset    # K
    spikes: frozenset     # S
    start: tuple
    exit: tuple

    def adj(self, c):
        x, y = c
        return [n for n in ((x, y - 1), (x, y + 1), (x - 1, y), (x + 1, y))
                if n in self.cells]


@dataclasses.dataclass(frozen=True)
class State:
    pos: tuple
    got: frozenset = frozenset()
    dead: frozenset = frozenset()


def initial_state(level):
    return State(level.start)


def _build(items):
    """items: iterable of ((x, y), kind) with kind in '.C^SE'."""
    cells, carrots, spikes, starts, exits = set(), set(), set(), [], []
    for c, kind in items:
        cells.add(c)
        if kind == "C":
            carrots.add(c)
        elif kind == "^":
            spikes.add(c)
        elif kind == "S":
            starts.append(c)
        elif kind == "E":
            exits.append(c)
    if len(starts) != 1 or len(exits) != 1:
        raise ValueError("level needs exactly one start and one exit")
    return Level(frozenset(cells), frozenset(carrots), frozenset(spikes),
                 starts[0], exits[0])


def parse_text(text):
    items = []
    for y, line in enumerate(text.splitlines()):
        for x, ch in enumerate(line):
            if ch in ".C^SE":
                items.append(((x, y), ch))
            elif ch not in "#x ":
                raise ValueError(f"unknown cell {ch!r} at {(x, y)}")
    return _build(items)


def parse_grid(grid):
    """256 tile ids, row-major, as in src/core/tiles.hpp."""
    kinds = {T_GROUND: ".", T_CARROT_TAKEN: ".", T_CARROT: "C",
             T_CRACKED: "^", T_START: "S", T_EXIT: "E"}
    items = []
    for i, tile in enumerate(grid):
        if tile < 18 or tile == T_HOLE:
            continue
        if tile not in kinds:
            raise ValueError(f"tile {tile} is outside the base cell set")
        items.append(((i % 16, i // 16), kinds[tile]))
    return _build(items)


def parse_blm(data):
    if len(data) != 260 or struct.unpack(">I", data[:4])[0] != 256:
        raise ValueError("not a .blm level")
    return parse_grid(data[4:])


def parse_lvl(text):
    """Decode a classic .lvl using the canonical C++ character table."""
    from blm2lvl import HEADER, load_table, to_grid
    try:
        return parse_grid(to_grid(text, load_table(HEADER)))
    except KeyError as e:
        raise ValueError(f"unknown character {e}") from None


def load(path):
    """A game level (.lvl, or the original .blm) or a solver text level."""
    path = pathlib.Path(path)
    if path.suffix == ".lvl":
        return parse_lvl(path.read_text(encoding="utf-8"))
    if path.suffix == ".blm":
        return parse_blm(path.read_bytes())
    return parse_text(path.read_text())


def distances(level, src, blocked):
    dist = {src: 0}
    queue = collections.deque([src])
    while queue:
        c = queue.popleft()
        for n in level.adj(c):
            if n not in dist and n not in blocked:
                dist[n] = dist[c] + 1
                queue.append(n)
    return dist


def is_won(level, state):
    return state.pos == level.exit and state.got == level.carrots


def step(level, state, to):
    """Apply one move; returns None when the move is illegal."""
    if to not in level.adj(state.pos) or to in state.dead:
        return None
    dead = state.dead | {state.pos} if state.pos in level.spikes else state.dead
    got = state.got | {to} if to in level.carrots else state.got
    return State(to, got, dead)


def replay(level, state, path):
    """True when `path` (cells after each move) is a legal winning play."""
    for to in path:
        state = step(level, state, to)
        if state is None:
            return False
    return is_won(level, state)


def path_to_moves(start, path):
    out, cur = [], start
    for c in path:
        out.append(MOVES[(c[0] - cur[0], c[1] - cur[1])])
        cur = c
    return "".join(out)


class Unrolling:
    """Incremental bounded unrolling of the game, constraints 1-7 of the doc."""

    def __init__(self, level, state):
        self.level = level
        self.cells = sorted(level.cells - state.dead)
        self.carrots = sorted(level.carrots)
        self.spikes = sorted(level.spikes - state.dead)
        self.solver = z3.Solver()
        self.at, self.got, self.dead, self.done = [], [], [], []
        self._frame()
        s = self.solver
        # (1) initial state
        s.add(self.at[0][state.pos])
        for k in self.carrots:
            s.add(self.got[0][k] if k in state.got else z3.Not(self.got[0][k]))
        for sp in self.spikes:
            s.add(z3.Not(self.dead[0][sp]))

    def _frame(self):
        t = len(self.at)
        self.at.append({c: z3.Bool(f"at_{t}_{c[0]}_{c[1]}") for c in self.cells})
        self.got.append({k: z3.Bool(f"got_{t}_{k[0]}_{k[1]}") for k in self.carrots})
        self.dead.append({p: z3.Bool(f"dead_{t}_{p[0]}_{p[1]}") for p in self.spikes})
        at = list(self.at[t].values())
        # (2) exactly one position
        self.solver.add(z3.Or(at), z3.AtMost(*at, 1))
        done = z3.Bool(f"done_{t}")
        self.solver.add(done == z3.And(self.at[t][self.level.exit],
                                       *self.got[t].values()))
        self.done.append(done)

    def extend(self, horizon):
        s, fin = self.solver, self.level.exit
        while len(self.at) <= horizon:
            t = len(self.at) - 1
            self._frame()
            at, nxt = self.at[t], self.at[t + 1]
            for c in self.cells:
                targets = [nxt[n] for n in self.level.adj(c) if n in nxt]
                if c == fin:
                    targets.append(nxt[c])
                # (3) move to a neighbour; (4) waiting only on the exit ...
                s.add(z3.Implies(at[c], z3.Or(targets)))
            # (4) ... and only once the level is won
            s.add(z3.Implies(z3.And(at[fin], nxt[fin]), self.done[t]))
            for p in self.spikes:
                # (5) no entering dead spikes; (6) spikes die when left
                s.add(z3.Implies(nxt[p], z3.Not(self.dead[t][p])))
                s.add(self.dead[t + 1][p] ==
                      z3.Or(self.dead[t][p], z3.And(at[p], z3.Not(nxt[p]))))
            for k in self.carrots:
                # (7) carrot pickup
                s.add(self.got[t + 1][k] == z3.Or(self.got[t][k], nxt[k]))

    def winnable_within(self, horizon):
        """(8) goal as an assumption, so the unrolling stays reusable."""
        self.extend(horizon)
        return self.solver.check(self.done[horizon]) == z3.sat

    def path(self, horizon):
        model = self.solver.model()
        out = []
        for t in range(1, horizon + 1):
            out.append(next(c for c, v in self.at[t].items()
                            if z3.is_true(model.eval(v))))
        return out


@dataclasses.dataclass
class Result:
    winnable: bool
    path: list = None       # cells after each move, shortest if minimize=True
    horizon_max: int = 0
    calls: int = 0


def solve(level, state=None, minimize=True):
    """Is there a winning strategy from `state`? Returns a Result."""
    state = state or initial_state(level)
    if is_won(level, state):
        return Result(True, [])

    todo = level.carrots - state.got
    from_pos = distances(level, state.pos, state.dead)
    if level.exit not in from_pos or not todo <= from_pos.keys():
        return Result(False)

    events = len(todo) + len(level.spikes - state.dead)
    horizon_max = (events + 1) * len(level.cells - state.dead) - 1
    to_exit = distances(level, level.exit, state.dead)
    lo = max([from_pos[k] + to_exit[k] for k in todo] or [from_pos[level.exit]])

    unrolling = Unrolling(level, state)
    calls = 0

    hi = max(lo, 1)
    while True:
        calls += 1
        if unrolling.winnable_within(hi):
            break
        if hi >= horizon_max:
            return Result(False, None, horizon_max, calls)
        lo = hi + 1
        hi = min(2 * hi, horizon_max)

    while minimize and lo < hi:
        mid = (lo + hi) // 2
        calls += 1
        if unrolling.winnable_within(mid):
            hi = mid
        else:
            lo = mid + 1

    calls += 1
    assert unrolling.winnable_within(hi)
    # Bounded models may pad the route after the first winning state.
    path, cur = [], state
    for to in unrolling.path(hi):
        cur = step(level, cur, to)
        assert cur is not None, "model produced an illegal move"
        path.append(to)
        if is_won(level, cur):
            break
    return Result(True, path, horizon_max, calls)


def brute_force(level, state=None):
    """BFS over (pos, got, dead). Returns shortest length or None."""
    state = state or initial_state(level)
    seen = {state}
    queue = collections.deque([(state, 0)])
    while queue:
        cur, depth = queue.popleft()
        if is_won(level, cur):
            return depth
        for n in level.adj(cur.pos):
            nxt = step(level, cur, n)
            if nxt is not None and nxt not in seen:
                seen.add(nxt)
                queue.append((nxt, depth + 1))
    return None


def random_level(rng):
    w, h = rng.randint(3, 6), rng.randint(3, 6)
    cells = [(x, y) for x in range(w) for y in range(h) if rng.random() > 0.2]
    if len(cells) < 2:
        return None
    rng.shuffle(cells)
    start, exit_, rest = cells[0], cells[1], cells[2:]
    n_carrots = rng.randint(0, min(4, len(rest)))
    n_spikes = rng.randint(0, min(5, len(rest) - n_carrots))
    items = [(start, "S"), (exit_, "E")]
    items += [(c, "C") for c in rest[:n_carrots]]
    items += [(c, "^") for c in rest[n_carrots:n_carrots + n_spikes]]
    items += [(c, ".") for c in rest[n_carrots + n_spikes:]]
    return _build(items)


def selftest(count, seed):
    rng = random.Random(seed)
    done = winnable = 0
    while done < count:
        level = random_level(rng)
        if level is None:
            continue
        expected = brute_force(level)
        result = solve(level)
        got = len(result.path) if result.winnable else None
        if expected != got:
            print(f"MISMATCH: brute force {expected}, smt {got}\n{level}")
            return 1
        if result.winnable and not replay(level, initial_state(level), result.path):
            print(f"INVALID PATH: {result.path}\n{level}")
            return 1
        done += 1
        winnable += result.winnable
    print(f"selftest ok: {done} levels, {winnable} winnable, "
          f"{done - winnable} not winnable")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*")
    ap.add_argument("--check", action="store_true",
                    help="also run brute force and compare (small levels only)")
    ap.add_argument("--any", action="store_true",
                    help="stop at the first solution instead of the shortest")
    ap.add_argument("--selftest", type=int, nargs="?", const=300, metavar="N",
                    help="compare with brute force on N random levels")
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.selftest, args.seed)
    if not args.files:
        ap.error("give level files or --selftest")

    status = 0
    for name in args.files:
        try:
            level = load(name)
        except ValueError as e:
            print(f"{name}: skipped ({e})")
            continue
        started = time.perf_counter()
        result = solve(level, minimize=not args.any)
        elapsed = time.perf_counter() - started
        head = (f"{name}: cells={len(level.cells)} carrots={len(level.carrots)} "
                f"spikes={len(level.spikes)}")
        if not result.winnable:
            print(f"{head} -> NOT WINNABLE ({elapsed:.2f}s)")
        else:
            ok = replay(level, initial_state(level), result.path)
            print(f"{head} -> WINNABLE in {len(result.path)} moves "
                  f"({elapsed:.2f}s, {result.calls} solver calls, "
                  f"T_max={result.horizon_max}, replay "
                  f"{'ok' if ok else 'FAILED'})")
            print(f"  {path_to_moves(level.start, result.path)}")
            status |= not ok
        if args.check:
            expected = brute_force(level)
            got = len(result.path) if result.winnable else None
            print(f"  brute force: {expected} -> "
                  f"{'match' if expected == got else 'MISMATCH'}")
            status |= expected != got
    return status


if __name__ == "__main__":
    sys.exit(main())
