#pragma once

#include <cstdint>

/* Handoff protocol between the LVGL task and the single decode worker of the
 * image viewer.
 *
 * Every method is called with the application's `request_mutex_` held, so the
 * protocol is a deterministic single-threaded state machine that the host
 * harness can drive directly.  Two properties are load-bearing:
 *
 *  - the worker is *persistent*: it is created once by `start()` and only
 *    leaves on cancellation, so there is no create/terminate handoff window in
 *    which a freshly enqueued job could be stranded;
 *  - `next()` clears `alive()` and `pending()` in the same critical section
 *    that observes them, so a request that runs afterwards always sees
 *    `alive() == false` and is told to create a task.
 */
namespace cyberdeck_image {

class worker_state final {
  public:
    enum class step : std::uint8_t {
        /** A job was taken and must be run. */
        run,
        /** Nothing to do: the worker waits for the next wake-up. */
        wait,
        /** Cancelled: the worker leaves and marks itself not alive. */
        exit,
    };

    /** `start()`: no pending job and no live worker. */
    void reset();
    /** After the task was created successfully. */
    void mark_alive();

    /** Stores the job.  Returns true when the caller must create the worker
     * task (no live worker); false means "just wake the live worker". */
    bool enqueue();

    /** Worker side.  `cancelled` wins over any pending job, so a job queued
     * during stop never runs. */
    step next(bool cancelled);

    bool pending() const {
        return pending_;
    }
    bool alive() const {
        return alive_;
    }

  private:
    bool pending_ = false;
    bool alive_ = false;
};

} // namespace cyberdeck_image
