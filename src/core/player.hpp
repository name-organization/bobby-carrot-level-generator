#pragma once

#include "rules.hpp"

namespace bc {

// Bobby's state machine. Deterministic and tick-based: no floats feed back
// into the simulation, so replays and the test solver stay reproducible.
class Player {
public:
    enum class State : std::uint8_t { FadeIn, Idle, Walk, Death, FadeOut };

    void spawn(Coord at);

    // One 60 Hz simulation tick. `held` is the direction currently pressed,
    // `buffered` a one-shot press that survives until Bobby is free to move.
    //
    // The level is passed in rather than a precomputed "all collected" flag:
    // the last patch of grass is counted while Bobby leaves it, in the very
    // same transition that lands him on the exit, so the check has to happen
    // after onLeave has run.
    unsigned update(Grid& g, Progress& p, const Level& lv, Dir held, Dir buffered);

    State state() const      { return state_; }
    Dir facing() const       { return facing_; }
    Coord cell() const       { return cell_; }
    bool isMoving() const    { return moving_; }
    bool deathFinished() const { return deathFinished_; }
    bool fadeOutFinished() const { return fadeOutFinished_; }
    bool consumedBuffered() const { return consumedBuffered_; }

    // Progress of the current animation, in ticks since the state was entered.
    int stateTick() const    { return stateTick_; }
    // Free-running counter so the walk cycle is continuous across cells.
    int animTick() const     { return animTick_; }
    int idleTicks() const    { return idleTicks_; }

    // Fractional cell position used for rendering. During a step this slides
    // smoothly from the source cell to the destination; during the death
    // animation it freezes halfway into the hole, exactly like the original.
    float fx() const;
    float fy() const;

private:
    void enterState(State s);
    bool tryStartStep(Grid& g, const Progress& p, Dir d);
    unsigned arrive(Grid& g, Progress& p, const Level& lv);

    State state_        = State::FadeIn;
    Dir facing_         = Dir::Down;
    Coord cell_{};
    Coord target_{};
    bool moving_        = false;
    bool dyingStep_     = false;
    int stepTick_       = 0;
    int stateTick_      = 0;
    int animTick_       = 0;
    int idleTicks_      = 0;
    Dir forced_         = Dir::None;
    bool deathFinished_ = false;
    bool fadeOutFinished_ = false;
    bool consumedBuffered_ = false;
    float deathFX_      = 0.0f;
    float deathFY_      = 0.0f;
};

}  // namespace bc
