#include "apps/image/cyberdeck_image_worker_state.h"

namespace cyberdeck_image {

void worker_state::reset() {
    pending_ = false;
    alive_ = false;
}

void worker_state::mark_alive() {
    alive_ = true;
}

bool worker_state::enqueue() {
    pending_ = true;
    return !alive_;
}

worker_state::step worker_state::next(bool cancelled) {
    if (cancelled) {
        /* Cancellation discards the queue and retires the worker in one step:
         * a later enqueue() therefore has to create a new task. */
        pending_ = false;
        alive_ = false;
        return step::exit;
    }
    if (!pending_)
        return step::wait;
    pending_ = false;
    return step::run;
}

} // namespace cyberdeck_image
