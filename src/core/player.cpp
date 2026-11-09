#include "player.hpp"

namespace bc {

void Player::spawn(Coord at) {
    cell_   = at;
    target_ = at;
    moving_ = false;
    dyingStep_ = false;
    stepTick_  = 0;
    idleTicks_ = 0;
    forced_    = Dir::None;
    facing_    = Dir::Down;
    deathFinished_ = false;
    fadeOutFinished_ = false;
    consumedBuffered_ = false;
    animTick_ = 0;
    enterState(State::FadeIn);
}

void Player::enterState(State s) {
    state_ = s;
    stateTick_ = 0;
}

float Player::fx() const {
    if (state_ == State::Death) return deathFX_;
    if (!moving_) return static_cast<float>(cell_.x);
    const float t = static_cast<float>(stepTick_) / kTicksPerStep;
    return cell_.x + (target_.x - cell_.x) * t;
}

float Player::fy() const {
    if (state_ == State::Death) return deathFY_;
    if (!moving_) return static_cast<float>(cell_.y);
    const float t = static_cast<float>(stepTick_) / kTicksPerStep;
    return cell_.y + (target_.y - cell_.y) * t;
}

bool Player::tryStartStep(Grid& g, const Progress& p, Dir d) {
    if (d == Dir::None) return false;

    const Coord to{cell_.x + dirDX(d), cell_.y + dirDY(d)};
    if (!Grid::inBounds(to.x, to.y)) return false;

    facing_ = d;
    if (!rules::canStep(g, cell_, to, d, p.inv)) return false;

    target_    = to;
    moving_    = true;
    stepTick_  = 0;
    dyingStep_ = (g.at(to) == T_HOLE);
    idleTicks_ = 0;
    // Keep the walk cycle running across consecutive cells instead of
    // restarting it on every tile boundary.
    if (state_ != State::Walk) enterState(State::Walk);
    return true;
}

namespace {
bool everythingCollected(const Progress& p, const Level& lv) {
    return lv.carrotTotal > 0 ? p.carrotCount == lv.carrotTotal
                              : p.eggCount == lv.eggTotal;
}
}  // namespace

unsigned Player::arrive(Grid& g, Progress& p, const Level& lv) {
    const Coord from = cell_;
    cell_   = target_;
    moving_ = false;
    stepTick_ = 0;

    // Order matters: leaving the final patch of grass is what completes the
    // level, and the exit must already see that count.
    unsigned events = rules::onLeave(g, from, p);

    const EnterResult r = rules::onEnter(g, cell_, p, everythingCollected(p, lv));
    events |= r.events;

    if (r.reachedExit) {
        forced_ = Dir::None;
        enterState(State::FadeOut);
    } else {
        forced_ = r.forced;
    }
    return events;
}

unsigned Player::update(Grid& g, Progress& p, const Level& lv, Dir held, Dir buffered) {
    unsigned events = ev::None;
    ++stateTick_;
    ++animTick_;
    consumedBuffered_ = false;

    switch (state_) {
        case State::FadeIn:
            if (stateTick_ >= kFadeTicks) {
                enterState(State::Idle);
                facing_ = Dir::Down;
            }
            return events;

        case State::FadeOut:
            if (stateTick_ >= kFadeTicks) fadeOutFinished_ = true;
            return events;

        case State::Death:
            if (stateTick_ >= kDeathTicks) deathFinished_ = true;
            return events;

        case State::Idle:
        case State::Walk:
            break;
    }

    // --- decide where to go -------------------------------------------------
    if (!moving_) {
        Dir want = Dir::None;
        if (forced_ != Dir::None) {
            want = forced_;          // conveyor belt keeps carrying Bobby
            forced_ = Dir::None;
        } else if (buffered != Dir::None) {
            want = buffered;
            consumedBuffered_ = true;
        } else {
            want = held;
        }

        if (want != Dir::None) {
            idleTicks_ = 0;
            if (!tryStartStep(g, p, want)) {
                // Bobby still turns to face the obstacle, as in the original.
                events |= ev::Blocked;
                if (state_ != State::Walk) enterState(State::Walk);
            }
        } else {
            ++idleTicks_;
            if (idleTicks_ >= kIdleAfterTicks && state_ != State::Idle) {
                enterState(State::Idle);
            }
        }
    }

    // --- advance the step ---------------------------------------------------
    if (moving_) {
        ++stepTick_;

        // The original starts the death animation once Bobby is halfway into
        // the hole rather than when he reaches the far cell.
        if (dyingStep_ && stepTick_ >= kTicksPerStep / 2) {
            deathFX_ = cell_.x + (target_.x - cell_.x) * 0.5f;
            deathFY_ = cell_.y + (target_.y - cell_.y) * 0.5f;
            moving_    = false;
            dyingStep_ = false;
            forced_    = Dir::None;
            enterState(State::Death);
            return events | ev::Death;
        }

        if (stepTick_ >= kTicksPerStep) {
            events |= arrive(g, p, lv);
        }
    }

    return events;
}

}  // namespace bc
