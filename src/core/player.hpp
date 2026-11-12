#pragma once

#include "rules.hpp"

namespace bc {

/** @brief Deterministic player state machine driven by fixed simulation ticks. */
class Player {
public:
    enum class State : std::uint8_t { FadeIn, Idle, Walk, Death, FadeOut };

    void spawn(Coord at);

    /**
     * @brief Advance the player by one simulation tick.
     *
     * @param[in,out] g Playfield receiving departure and arrival effects.
     * @param[in,out] p Inventory and collectible counts.
     * @param lv Original totals used to check completion after leaving grass.
     * @param held Continuously held direction.
     * @param buffered One-shot input retained until the player can act.
     * @return Cell-transition event flags.
     */
    unsigned update(Grid& g, Progress& p, const Level& lv, Dir held, Dir buffered);

    State state() const      { return state_; }
    Dir facing() const       { return facing_; }
    Coord cell() const       { return cell_; }
    bool isMoving() const    { return moving_; }
    bool deathFinished() const { return deathFinished_; }
    bool fadeOutFinished() const { return fadeOutFinished_; }
    bool consumedBuffered() const { return consumedBuffered_; }

    /** @brief Ticks elapsed since entering the current animation state. */
    int stateTick() const    { return stateTick_; }
    /** @brief Animation counter that continues across consecutive steps. */
    int animTick() const     { return animTick_; }
    int idleTicks() const    { return idleTicks_; }

    /**
     * @brief Interpolated position in cells for rendering.
     *
     * @details Death freezes the position halfway into the hole.
     */
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
