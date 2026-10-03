/*
 * Fase 8 - window manager: testes comportamentais host.
 *
 * Rastreabilidade (host, sem hardware):
 *   REQ/AC-8.1  superficie principal por aplicacao -> TEST-WM-01  surfaces_scenario
 *   REQ/AC-8.2  foco e ownership de input centralizados -> TEST-WM-02  focus_scenario
 *   REQ/AC-8.3  barra de sistema persistente -> TEST-WM-03  adapter_scenario
 *   REQ/AC-8.4  notificacoes e transicoes entre apps -> TEST-WM-04  notifications_scenario
 *   REQ/AC-8.5  sem acesso direto das apps a arvore LVGL -> TEST-WM-05
 *                (forma da API + contrato estrutural test_window_manager_contract.py)
 *   REQ/AC-8.6  API controlada de view/contexto -> TEST-WM-06  api_shape_scenario
 *   REQ/AC-8.7  teardown, quiesciencia e invalidacao -> TEST-WM-07  teardown_scenario
 *   BUG-8-WM-BAR (regressao da barra) system_bar 42 px em coluna flex, content
 *                abaixo da barra sem intersecao e teclado virtual em overlay
 *                                                      -> TEST-REG-8-BAR  bar_layout_scenario
 *   TEST-REG-8-CHROME (regressao do cromo da barra) system_bar chrome-only preto,
 *                sem borda, sem padding e sem rolagem, 100% x 42 com flex_grow 0,
 *                header ocupando a area inteira, content abaixo e padding externo
 *                                                      -> TEST-REG-8-CHROME  bar_chrome_scenario
 *
 * O binario liga os TUs reais de producao (a politica pura, o adaptador LVGL e a
 * view do header) contra o shim host completo, no mesmo padrao de
 * test_display_views e test_keyboard_dispatch.  Nenhum corpo de producao e
 * copiado aqui: o unico duplo e o proprio shim LVGL.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "lvgl.h"

#include "apps/runtime/cyberdeck_window_manager.h"
#include "platform/display/cyberdeck_header_view.h"
#include "platform/display/cyberdeck_window_manager_adapter.h"

namespace {

int failures = 0;
int checks = 0;

using cyberdeck_window_manager::k_max_notifications;
using cyberdeck_window_manager::k_max_surfaces;
using cyberdeck_window_manager::manager;
using cyberdeck_window_manager::notification;
using cyberdeck_window_manager::surface_state;
using cyberdeck_window_manager::view_context;
using cyberdeck_window_manager::view_status;

void check(bool condition, const char *message)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("FAIL: %s\n", message);
    }
}

const char *to_text(view_status status)
{
    switch (status) {
    case view_status::ok: return "ok";
    case view_status::invalid_context: return "invalid_context";
    case view_status::expired_context: return "expired_context";
    case view_status::not_focused: return "not_focused";
    case view_status::overflow: return "overflow";
    case view_status::no_surface: return "no_surface";
    }
    return "unknown";
}

void check_status(view_status actual, view_status expected, const char *message)
{
    ++checks;
    if (actual != expected) {
        ++failures;
        std::printf("FAIL: %s (got %s, want %s)\n", message, to_text(actual), to_text(expected));
    }
}

constexpr std::size_t k_text_capacity = sizeof(notification::text);

/* REQ/AC-8.5 e 8.6: a API controlada e opaca em tempo de compilacao.  Um app
 * recebe um handle copiavel, mas nao consegue forjar um handle valido nem
 * inspecionar o bookkeeping do manager. */
static_assert(std::is_default_constructible_v<view_context>, "a view context starts empty");
static_assert(std::is_copy_constructible_v<view_context>, "a view context is a copyable capability");
static_assert(!std::is_constructible_v<view_context, std::uint16_t, std::uint64_t>,
              "an application must not be able to forge a valid view context");
static_assert(!std::is_copy_constructible_v<manager>, "the window manager is not copyable");
static_assert(std::is_same_v<decltype(std::declval<manager &>().validate(view_context{})), view_status>,
              "validate is a total, non-throwing query");
static_assert(std::is_same_v<decltype(std::declval<const manager &>().validate(view_context{})),
                             view_status>, "validate is const-qualified");
static_assert(std::is_same_v<decltype(std::declval<const manager &>().status(
                                 view_context {}, std::declval<surface_state &>())),
                             view_status>, "status is const-qualified and reports the surface state");
static_assert(std::is_same_v<decltype(std::declval<const manager &>().focused(view_context {})), bool>,
              "focused is a const-qualified predicate");
static_assert(std::is_same_v<decltype(std::declval<manager &>().notify(view_context {},
                                                                       std::string_view {})),
                             bool>, "notify reports acceptance without throwing");
static_assert(std::is_same_v<decltype(std::declval<manager &>().pop_notification(
                                 std::declval<notification &>())),
                             bool>, "pop_notification is fail-closed on an empty queue");
static_assert(std::is_same_v<decltype(std::declval<manager &>().create(
                                 std::uint16_t {}, std::declval<view_context &>())),
                             view_status>, "create always reports an explicit status");
static_assert(k_max_surfaces > 1 && k_max_notifications > 1,
              "the surface and notification registries are bounded but usable");

/* TEST-WM-06: a API controlada responde sobre um contexto vazio e trata o handle
 * copiavel como a mesma autoridade. */
void api_shape_scenario()
{
    manager policy;
    view_context empty;

    check(empty.empty(), "a default view context is empty");
    check_status(policy.validate(empty), view_status::invalid_context, "validate rejects an empty context");
    check_status(policy.begin_teardown(empty), view_status::invalid_context,
                 "begin_teardown rejects an empty context");
    check_status(policy.remove(empty), view_status::invalid_context,
                 "remove rejects an empty context");
    check_status(policy.activate(empty), view_status::invalid_context,
                 "activate rejects an empty context");
    check_status(policy.hide(empty), view_status::invalid_context, "hide rejects an empty context");
    check_status(policy.focus(empty), view_status::invalid_context, "focus rejects an empty context");
    surface_state state = surface_state::creating;
    check_status(policy.status(empty, state), view_status::invalid_context,
                 "status rejects an empty context");
    check(!policy.focused(empty), "an empty context is never focused");
    check(!policy.notify(empty, "orphan"), "an empty context cannot publish a notification");
    check(policy.notification_count() == 0, "a rejected notification does not reach the queue");

    view_context original;
    check_status(policy.create(3, original), view_status::ok, "an application registers a surface");
    view_context copy = original;
    check(!copy.empty(), "the copied capability is still live");
    check_status(policy.validate(original), view_status::ok, "the original capability owns the input");
    check_status(policy.validate(copy), view_status::ok, "the copied capability owns the input too");
    check_status(policy.hide(copy), view_status::ok, "hiding through the copy hides the surface");
    check_status(policy.status(original, state), view_status::ok, "the original observes the new state");
    check(state == surface_state::hidden, "hiding through the copy reached the original surface");
    check(!policy.focused(original), "a hidden surface loses the input ownership");
    check(policy.focused(original) == (policy.validate(original) == view_status::ok),
          "focused() is exactly validate() == ok");
    check(policy.surface_count() == 1, "the copied capability did not register a second surface");
}

/* TEST-WM-01: uma superficie principal por aplicacao, com limite bounded. */
void surfaces_scenario()
{
    manager policy;
    std::vector<view_context> contexts;

    for (std::size_t index = 0; index < k_max_surfaces; ++index) {
        view_context context;
        const auto app = static_cast<std::uint16_t>(100 + index);
        check_status(policy.create(app, context), view_status::ok,
                     "each application registers its own main surface");
        check(!context.empty(), "a registered surface yields a live capability");
        contexts.push_back(context);
        check(policy.surface_count() == index + 1, "the surface registry counts registrations");
    }

    view_context duplicate;
    check(policy.create(100, duplicate) != view_status::ok,
          "a second surface for the same application is refused");
    check(policy.surface_count() == k_max_surfaces, "the refused duplicate does not grow the registry");
    check(duplicate.empty(), "a refused registration never hands back a capability");

    view_context overflow;
    check_status(policy.create(999, overflow), view_status::overflow,
                 "the surface registry is bounded and fails closed when full");
    check(overflow.empty(), "the overflowing registration yields no capability");
    check(policy.surface_count() == k_max_surfaces, "the overflow does not grow the registry");

    /* Um handle previamente emitido nao pode sobreviver a um registro recusado. */
    view_context recycled = contexts.front();
    check(!recycled.empty(), "the recycled variable starts holding a live capability");
    check(policy.create(999, recycled) != view_status::ok, "the overflowing registration is refused");
    check(recycled.empty(), "a refused create clears the output instead of leaking a handle");
    check_status(policy.begin_teardown(contexts.front()), view_status::ok,
                 "the original capability still controls its surface");
    check_status(policy.remove(contexts.front()), view_status::ok, "the first surface is removed");
    check(policy.surface_count() == k_max_surfaces - 1, "removal decrements the registry");
    view_context replacement;
    check_status(policy.create(999, replacement), view_status::ok, "a freed slot accepts a new application");
    check(policy.surface_count() == k_max_surfaces, "the registry never exceeds its bound");
}

/* TEST-WM-02: foco e ownership de input centralizados em um unico dono. */
void focus_scenario()
{
    manager policy;
    view_context first;
    view_context second;
    view_context third;
    check_status(policy.create(1, first), view_status::ok, "first application registers");
    check_status(policy.create(2, second), view_status::ok, "second application registers");
    check_status(policy.create(3, third), view_status::ok, "third application registers");

    check(policy.focused(first), "the first registered surface takes the input ownership");
    check(!policy.focused(second), "a newly registered surface does not steal the ownership");
    check_status(policy.validate(second), view_status::not_focused,
                 "a valid but unfocused surface reports not_focused");

    check_status(policy.focus(second), view_status::ok, "the focus can be moved explicitly");
    check(!policy.focused(first), "the previous owner loses the input ownership");
    check(policy.focused(second), "the new owner holds the input ownership");

    std::size_t owners = 0;
    for (const view_context &context : {first, second, third}) {
        if (policy.focused(context)) ++owners;
    }
    check(owners == 1, "at most one surface owns the input at a time");

    check_status(policy.hide(second), view_status::ok, "the owner can be hidden");
    check(!policy.focused(second), "a hidden surface does not own the input");
    check_status(policy.focus(second), view_status::not_focused,
                 "a hidden surface cannot reclaim the input ownership");
    check_status(policy.activate(second), view_status::ok, "activating a hidden surface restores it");
    check(policy.focused(second), "activating also returns the input ownership");

    check_status(policy.begin_teardown(second), view_status::ok, "the owner starts tearing down");
    check(!policy.focused(second), "a tearing down surface never owns the input");
    check_status(policy.remove(second), view_status::ok, "the tearing down surface is removed");
    check(policy.focused(first), "the input ownership falls back to a remaining active surface");
    check_status(policy.validate(third), view_status::not_focused,
                 "the fallback does not hand the ownership to another surface");
    check(policy.surface_count() == 2, "the focus bookkeeping follows the registry");
}

/* TEST-WM-07: teardown, quiesciencia e invalidacao de handles tardios. */
void teardown_scenario()
{
    manager policy;
    view_context shell;
    view_context other;
    check_status(policy.create(1, shell), view_status::ok, "the shell surface registers");
    check_status(policy.create(2, other), view_status::ok, "a second surface registers");

    check_status(policy.remove(shell), view_status::invalid_context,
                 "removal without a teardown is refused");
    check(policy.surface_count() == 2, "the refused removal does not free the surface");

    check_status(policy.begin_teardown(shell), view_status::ok, "the surface starts tearing down");
    check_status(policy.begin_teardown(shell), view_status::ok, "teardown is idempotent");
    surface_state state = surface_state::absent;
    check_status(policy.status(shell, state), view_status::ok, "the tearing down state is observable");
    check(state == surface_state::tearing_down, "teardown publishes the tearing down state");
    check_status(policy.validate(shell), view_status::expired_context,
                 "a tearing down surface is invalidated for late callers");
    check_status(policy.activate(shell), view_status::expired_context,
                 "a tearing down surface cannot be activated");
    check_status(policy.hide(shell), view_status::expired_context,
                 "a tearing down surface cannot be hidden");
    check_status(policy.focus(shell), view_status::expired_context,
                 "focus reports the same expiry reason as activate/hide/validate during teardown");
    check(!policy.notify(shell, "late"),
          "a tearing down surface is quiesced and cannot publish notifications");

    /* Uma superficie em teardown continua registrada: o app nao pode ocupar a
     * propria vaga com outra superficie antes de liberar a primeira. */
    view_context duplicate;
    check(policy.create(1, duplicate) != view_status::ok,
          "an application tearing down cannot register another surface");
    check(policy.surface_count() == 2, "the refused re-registration does not grow the registry");
    view_context third;
    check_status(policy.create(3, third), view_status::ok,
                 "another application still registers while one tears down");

    check_status(policy.remove(shell), view_status::ok, "the torn down surface is removed");
    check(policy.surface_count() == 2, "the registry shrank with the removal");
    check_status(policy.validate(shell), view_status::expired_context,
                 "the removed capability is expired, not merely hidden");
    check_status(policy.begin_teardown(shell), view_status::expired_context,
                 "a removed capability cannot be torn down again");
    check_status(policy.remove(shell), view_status::expired_context,
                 "a removed capability cannot be removed twice");
    check(!policy.focused(shell), "a removed capability never owns the input");
    check(!policy.notify(shell, "late"), "a removed capability cannot publish notifications");

    /* Reuso de slot com geracao nova: o handle antigo nao volta a valer. */
    check(policy.focused(other), "the remaining app still owns the input across the removal");
    view_context reused_slot;
    check_status(policy.create(1, reused_slot), view_status::ok, "the application registers again");
    check_status(policy.validate(shell), view_status::expired_context,
                 "the handle of the previous lifetime stays expired after the slot reuse");
    check_status(policy.validate(reused_slot), view_status::not_focused,
                 "the new capability is live but does not steal the input ownership");
    check(policy.focused(other), "re-creating a surface leaves the input with its current owner");
    check_status(policy.focus(reused_slot), view_status::ok,
                 "the recreated surface can take the input ownership explicitly");
    check_status(policy.remove(third), view_status::invalid_context,
                 "the untouched surface still refuses removal without teardown");

    /* O handle anterior continua recusando depois de um reset do manager. */
    check_status(policy.begin_teardown(reused_slot), view_status::ok, "the new lifetime tears down");
    check_status(policy.remove(reused_slot), view_status::ok, "the new lifetime is removed");
    view_context before_reset;
    check_status(policy.create(5, before_reset), view_status::ok, "a surface registers before reset");
    check(policy.notify(before_reset, "queued"), "a notification is queued before reset");
    policy.reset();
    check(policy.surface_count() == 0, "reset empties the surface registry");
    check(policy.notification_count() == 0, "reset empties the notification queue");
    check(policy.dropped_notifications() == 0, "reset clears the drop diagnostic");
    check_status(policy.validate(before_reset), view_status::expired_context,
                 "reset invalidates every outstanding capability");
    view_context after_reset;
    check_status(policy.create(7, after_reset), view_status::ok, "the manager is reusable after reset");
    check_status(policy.validate(before_reset), view_status::expired_context,
                 "a pre-reset capability does not become valid again");
    notification drained;
    check(!policy.pop_notification(drained), "the notification queue is empty after reset");
}

/* TEST-WM-04: notificacoes bounded em FIFO e transicoes entre apps. */
void notifications_scenario()
{
    manager policy;
    view_context shell;
    view_context other;
    check_status(policy.create(1, shell), view_status::ok, "the shell surface registers");
    check_status(policy.create(2, other), view_status::ok, "the second surface registers");

    check(policy.notify(shell, "shell ready"), "a surface publishes a notification");
    check(policy.notify(other, "other ready"), "a second surface publishes a notification");
    check(policy.notification_count() == 2, "the queue counts the published notifications");
    notification first;
    check(policy.pop_notification(first), "the queue pops in FIFO order");
    check(first.sequence == 1, "the first published notification has the first sequence");
    check(std::string(first.text.data()) == "shell ready", "the first pop returns the oldest notification");
    notification second;
    check(policy.pop_notification(second), "the queue pops again");
    check(second.sequence == 2, "the sequence is monotonic");
    check(second.surface != first.surface, "each notification carries its own origin surface");
    check(std::string(second.text.data()) == "other ready", "the second pop preserves FIFO order");
    check(policy.notification_count() == 0, "the queue is empty after draining");

    /* Overflow bounded: a fila cheia falha fechado e contabiliza. */
    for (std::size_t index = 0; index < k_max_notifications; ++index) {
        const std::string text = "n" + std::to_string(index);
        check(policy.notify(shell, text), "the bounded notification queue accepts up to its capacity");
    }
    check(policy.notification_count() == k_max_notifications, "the queue is exactly at its capacity");
    check(!policy.notify(shell, "overflow"), "a full queue refuses the next notification");
    check(policy.notification_count() == k_max_notifications, "the refused notification does not grow the queue");
    check(policy.dropped_notifications() == 1, "the refused notification is accounted as dropped");

    std::size_t popped = 0;
    bool order_preserved = true;
    notification item;
    while (policy.pop_notification(item)) {
        if (std::string(item.text.data()) != "n" + std::to_string(popped)) order_preserved = false;
        ++popped;
    }
    check(popped == k_max_notifications, "every queued notification is delivered");
    check(order_preserved, "the drained queue preserves insertion order");
    check(policy.dropped_notifications() == 1, "draining does not rewrite the drop diagnostic");

    /* Reuso do anel apos um pop: a proxima notificacao entra no fim da fila. */
    check(policy.notify(shell, "first"), "a notification is published after draining");
    check(policy.notify(shell, "second"), "a second notification is published");
    check(policy.notify(shell, "third"), "a third notification is published");
    check(policy.pop_notification(item), "the head is popped");
    check(std::string(item.text.data()) == "first", "the head is the oldest entry");
    check(policy.notify(shell, "wrapped"), "the freed slot accepts a new notification");
    check(policy.pop_notification(item), "the next entry pops");
    check(std::string(item.text.data()) == "second", "the wrap does not reorder the queue");
    check(policy.pop_notification(item), "the following entry pops");
    check(std::string(item.text.data()) == "third", "the wrap preserves the remaining order");
    check(policy.pop_notification(item), "the wrapped entry pops last");
    check(std::string(item.text.data()) == "wrapped", "the wrapped entry keeps its content");
    check(policy.notification_count() == 0, "the queue is drained again");

    /* Payload no limite exato da capacidade e payload oversized. */
    const std::string at_limit(k_text_capacity - 1, 'x');
    check(at_limit.size() < k_text_capacity, "the payload limit leaves room for the terminator");
    check(policy.notify(shell, at_limit), "the largest accepted payload is published");
    const std::string oversized(k_text_capacity, 'y');
    check(!policy.notify(shell, oversized), "an oversized payload is refused fail-closed");
    check(policy.notification_count() == 1, "the refused payload does not consume a queue slot");
    check(policy.pop_notification(item), "the queue still pops after the refusal");
    check(std::string(item.text.data()) == at_limit, "the largest accepted payload is preserved byte for byte");
    check(!policy.pop_notification(item), "the queue is empty after the accepted payload");

    /* Transicao entre apps: ocultar e reativar nao corrompe a fila. */
    check(policy.notify(other, "before transition"), "the incoming app notifies");
    check_status(policy.hide(other), view_status::ok, "the incoming app is hidden");
    check_status(policy.activate(shell), view_status::ok, "the outgoing app takes the input back");
    check(policy.notify(shell, "after transition"), "the outgoing app notifies");
    check(policy.pop_notification(item), "the queue pops across the transition");
    check(std::string(item.text.data()) == "before transition",
          "a notification enqueued before the transition is preserved");
    check(policy.pop_notification(item), "the queue pops the notification after the transition");
    check(std::string(item.text.data()) == "after transition", "the transition notification is preserved");
    check(!policy.pop_notification(item), "the queue is drained after the transition");

    /* Handles expirados nao publicam nada e nao corrompem a fila. */
    check_status(policy.begin_teardown(shell), view_status::ok, "the outgoing app tears down");
    check(!policy.notify(shell, "late"), "a tearing down surface cannot publish");
    check_status(policy.remove(shell), view_status::ok, "the outgoing app is removed");
    check(!policy.notify(shell, "late"), "a removed surface cannot publish");
    check(policy.notification_count() == 0, "the refusals never reached the queue");
}

/* TEST-WM-03: o adaptador e o unico dono da raiz LVGL composta. */
void adapter_scenario()
{
    lv_obj_t *root = new lv_obj_t{};
    lv_shim_active_screen() = root;

    auto &adapter = cyberdeck_window_manager_adapter::global();
    check(adapter.init(), "the adapter builds the composed root");
    check(adapter.ready(), "the adapter is ready after a successful init");
    check(adapter.screen() == root, "the adapter exposes the LVGL root it owns");
    check(adapter.system_bar() != nullptr, "the persistent system bar exists");
    check(adapter.content() != nullptr, "the content area exists");
    check(adapter.system_bar() != adapter.content(), "the system bar is not the content area");
    check(adapter.system_bar()->parent == root && adapter.content()->parent == root,
          "both areas are composed under the LVGL root");
    check(adapter.system_bar()->height == 42, "the system bar keeps its fixed height");
    check(adapter.content()->width == LV_PCT(100), "the content area spans the root width");
    check(root->children.size() == 2, "the root holds exactly the system bar and the content area");

    /* Reconstruir a composicao nao duplica nem destroi a barra de sistema. */
    lv_obj_t *bar = adapter.system_bar();
    lv_obj_t *content = adapter.content();
    check(adapter.init(), "a repeated init is idempotent");
    check(adapter.screen() == root && adapter.system_bar() == bar && adapter.content() == content,
          "a repeated init keeps the same root, system bar and content area");
    check(root->children.size() == 2, "a repeated init does not duplicate the composed areas");

    view_context shell;
    check_status(adapter.policy().create(1, shell), view_status::ok,
                 "the composition registers the shell surface through the adapter policy");
    check(adapter.policy().surface_count() == 1, "the adapter owns exactly one shell surface");
    check(adapter.policy().focused(shell), "the shell surface owns the input after registration");

    adapter.deinit();
    check(!adapter.ready(), "the adapter is not ready after deinit");
    check(adapter.screen() == nullptr && adapter.system_bar() == nullptr &&
              adapter.content() == nullptr,
          "deinit releases the composed areas and forgets the root");
    check(adapter.policy().surface_count() == 0, "deinit invalidates the registered surfaces");
    check_status(adapter.policy().validate(shell), view_status::expired_context,
                 "deinit expires the capabilities held by late callbacks");

    lv_shim_active_screen() = nullptr;
    check(!adapter.init(), "init fails closed when the display has no active screen");
    check(!adapter.ready(), "a failed init leaves the adapter unusable for late callers");
    check(adapter.screen() == nullptr && adapter.system_bar() == nullptr &&
              adapter.content() == nullptr,
          "a failed init publishes no partial root");
    lv_shim_active_screen() = root;
    check(adapter.init(), "init recovers once the display exposes a screen");
    check(adapter.screen() == root && adapter.system_bar() != nullptr && adapter.content() != nullptr,
          "the recovered adapter republishes the composed root");
    adapter.deinit();
    adapter.deinit();
    check(!adapter.ready(), "a repeated deinit stays inert");
    lv_shim_active_screen() = nullptr;
    delete root;
}

/* BSP_LCD_H_RES / BSP_LCD_V_RES: the composed root is modelled with the panel
 * size so the placement below is the one the device performs. */
constexpr int k_display_width = 720;
constexpr int k_display_height = 1280;

/* A vertical slice of the root column, in the coordinates LVGL hands to the two
 * composed areas. */
struct slot {
    int top{};
    int bottom{};
};

/* An absolute rectangle, in the same coordinates. */
struct box {
    int left{};
    int top{};
    int right{};
    int bottom{};

    bool operator==(const box &other) const
    {
        return left == other.left && top == other.top &&
               right == other.right && bottom == other.bottom;
    }
};

/* The height a flex column gives an item: the height production set, or the row
 * left below the preceding items when production left it to flex grow. */
int resolved_height(const lv_obj_t *item, int remaining)
{
    return item->height > 0 ? item->height : remaining;
}

/* Places `item` in the root column restricted to `area`, the parent's content
 * box: it starts exactly where the previous item ends.  A child of the root that
 * production excludes from the layout (the floating virtual keyboard) is not
 * modelled here: the structural contract proves it never takes a row. */
slot column_slot_in(const lv_obj_t *parent, const lv_obj_t *item, box area)
{
    int top = area.top;
    for (const lv_obj_t *sibling : parent->children) {
        if (sibling == item)
            return {top, top + resolved_height(item, area.bottom - top)};
        top += resolved_height(sibling, area.bottom - top);
    }
    return {0, 0};
}

slot column_slot(const lv_obj_t *parent, const lv_obj_t *item)
{
    return column_slot_in(parent, item, {0, 0, parent->width, parent->height});
}

bool intersects(const slot &first, const slot &second)
{
    return first.top < second.bottom && second.top < first.bottom;
}

/* The area inside `object` that holds its children: LVGL 9 offsets the content
 * box by the border width plus the padding (`lv_obj_get_style_space_*_internal`,
 * with the default full border side).  The shim records the chrome production
 * applied, so this reproduces the placement the device performs; a style the
 * shim never saw applied contributes no inset, which is why the zero-chrome
 * claims are asserted on the recorded fields themselves. */
box content_box(const lv_obj_t *object, box area)
{
    const int inset = lv_shim_style_inset(object->border_width);
    const int left = lv_shim_style_inset(object->pad_left);
    const int right = lv_shim_style_inset(object->pad_right);
    const int top = lv_shim_style_inset(object->pad_top);
    const int bottom = lv_shim_style_inset(object->pad_bottom);
    return {area.left + inset + left, area.top + inset + top,
            area.right - inset - right, area.bottom - inset - bottom};
}

/* The absolute area a child of `parent_area` occupies for the size production
 * declared.  LV_PCT(100) is the whole parent row, which is the one percentage the
 * composed areas use. */
box child_box(box parent_area, int32_t width, int32_t height)
{
    const int resolved_width = width == LV_PCT(100) ? parent_area.right - parent_area.left : width;
    const int resolved_height = height == LV_PCT(100) ? parent_area.bottom - parent_area.top : height;
    return {parent_area.left, parent_area.top,
            parent_area.left + resolved_width, parent_area.top + resolved_height};
}

/* TEST-REG-8-BAR (BUG-8-WM-BAR): the persistent bar owns a fixed 42 px row of
 * the root column and the content starts strictly below it.  The shim records
 * the sizes production sets but never runs a layout pass, so the scenario
 * reproduces the column placement to prove the two areas cannot overlap; the
 * flex layout itself is proved structurally by test_window_manager_contract.py.
 * The root padding only offsets both rows by the same amount, so the invariant
 * holds with or without it. */
void bar_layout_scenario()
{
    lv_obj_t root{};
    root.width = k_display_width;
    root.height = k_display_height;
    lv_shim_active_screen() = &root;

    auto &adapter = cyberdeck_window_manager_adapter::global();
    check(adapter.init(), "the composed root is built for the layout regression");
    check(root.height == k_display_height, "the scenario keeps the modelled panel height");

    lv_obj_t *bar = adapter.system_bar();
    lv_obj_t *content = adapter.content();
    check(bar != nullptr && content != nullptr, "the composed root exposes both areas");

    check(bar->height == 42, "the system bar keeps the fixed 42 px row");
    check(bar->width == LV_PCT(100), "the system bar spans the root width");
    check(root.children.size() == 2 && root.children[0] == bar && root.children[1] == content,
          "the bar is composed as the first row, before the content");

    check(content->width == LV_PCT(100), "the content spans the root width");
    check(content->height != LV_PCT(100),
          "the content carries no full-root height next to the system bar");
    check(content->height == 0, "the content height is left to the root column");

    const slot bar_slot = column_slot(&root, bar);
    const slot content_slot = column_slot(&root, content);
    check(bar_slot.top == 0 && bar_slot.bottom == 42,
          "the bar occupies the first 42 px row of the column");
    check(content_slot.top >= bar_slot.bottom, "the content starts below the system bar");
    check(!intersects(bar_slot, content_slot), "the content does not intersect the system bar");
    check(content_slot.bottom == root.height,
          "the growing content fills exactly the row left below the bar");

    /* The detector must reject the pre-fix composition, otherwise the checks
     * above would also pass on the regression they exist for. */
    lv_obj_t legacy_root{};
    legacy_root.width = k_display_width;
    legacy_root.height = k_display_height;
    lv_obj_t legacy_bar{};
    lv_obj_t legacy_content{};
    legacy_bar.width = LV_PCT(100);
    legacy_bar.height = 42;
    legacy_content.width = LV_PCT(100);
    legacy_content.height = legacy_root.height; /* LV_PCT(100) against the root */
    legacy_root.children.push_back(&legacy_bar);
    legacy_root.children.push_back(&legacy_content);
    const slot legacy_bar_slot = column_slot(&legacy_root, &legacy_bar);
    const slot legacy_content_slot = column_slot(&legacy_root, &legacy_content);
    check(legacy_bar_slot.bottom <= legacy_content_slot.top,
          "both compositions place the content below the bar: the row is not the symptom");
    check(legacy_content_slot.bottom > legacy_root.height &&
              content_slot.bottom <= root.height,
          "only the fixed composition keeps the content inside the root");

    adapter.deinit();
    check(!adapter.ready(), "the layout scenario releases the composed root");
    lv_shim_active_screen() = nullptr;
}

/* TEST-REG-8-CHROME (regressao do cromo da barra): o `system_bar` herdava o
 * cromo do cartao do tema LVGL (fundo cinza, borda e padding), entao o header
 * anexado pela UI ficava deslocado para dentro da barra de 42 px em vez de
 * ocupar a linha inteira.  A barra correta e chrome-only: fundo preto, sem
 * borda, sem padding e sem rolagem.
 *
 * Este cenario liga o TU real do header (`cyberdeck_header_view`, o mesmo que a
 * UI anexa a `system_bar()`) contra o TU real do adaptador, entao as checagens
 * observam as sentencas de producao e nao um duplo.  O LVGL 9 nao expoe getter
 * publico de propriedade de estilo, de layout nem de flex: o shim registra o que
 * a producao aplicou, e um campo ainda em -1 significa estilo nunca aplicado --
 * e por isso que as asserts de "cromo zero" abaixo nao podem ser satisfeitas por
 * um default. */
void bar_chrome_scenario()
{
    lv_obj_t root{};
    root.width = k_display_width;
    root.height = k_display_height;
    lv_shim_active_screen() = &root;

    auto &adapter = cyberdeck_window_manager_adapter::global();
    check(adapter.init(), "the composed root is built for the chrome regression");

    lv_obj_t *bar = adapter.system_bar();
    lv_obj_t *content = adapter.content();
    check(bar != nullptr && content != nullptr, "the composed root exposes both areas");

    /* Cromo: fundo preto, sem borda, sem padding, sem gap e sem rolagem. */
    check(bar->color == lv_color_hex(0x000000).value,
          "the system bar paints a black background instead of the inherited theme card");
    check(bar->border_width == 0, "the system bar drops the inherited card border");
    check(bar->pad_left == 0 && bar->pad_right == 0 && bar->pad_top == 0 &&
              bar->pad_bottom == 0,
          "the system bar drops the inherited card padding on all four sides");
    check(bar->pad_row == 0 && bar->pad_column == 0,
          "the system bar declares no internal gap between its children");
    check(lv_obj_get_scroll_dir(bar) == LV_DIR_NONE,
          "the system bar scrolls in no direction");
    check(lv_obj_get_scrollbar_mode(bar) == LV_SCROLLBAR_MODE_OFF,
          "the system bar never shows a scrollbar");
    check(!bar->scroll_chain, "the system bar does not chain scrolling to its siblings");

    /* Geometria: 100% x 42 fixos e sem crescimento. */
    check(bar->width == LV_PCT(100), "the system bar spans the root width");
    check(bar->height == 42, "the system bar keeps its fixed 42 px row");
    check(bar->flex_grow == 0, "the system bar declares flex grow 0 instead of relying on a default");
    check(lv_obj_get_content_width(bar) == lv_obj_get_width(bar),
          "the bar content width is the bar width: no pixel is lost to border or padding");
    check(lv_obj_get_content_height(bar) == lv_obj_get_height(bar),
          "the bar content height is the bar height: no vertical inset");

    /* O padding externo da raiz permanece: e ele, e nao o cromo da barra, que
     * afasta as duas areas das bordas do painel. */
    check(root.pad_top == 12 && root.pad_bottom == 12 && root.pad_left == 12 &&
              root.pad_right == 12,
          "the composed root keeps its external padding on all four sides");
    const box root_area = content_box(&root, {0, 0, root.width, root.height});
    check(root_area.left == 12 && root_area.top == 12 &&
              root_area.right == k_display_width - 12 &&
              root_area.bottom == k_display_height - 12,
          "the root content area is the panel inset by the external padding");

    /* A barra continua sendo a primeira linha e o conteudo continua abaixo dela. */
    const slot bar_row = column_slot_in(&root, bar, root_area);
    const slot content_row = column_slot_in(&root, content, root_area);
    check(bar_row.top == root_area.top && bar_row.bottom == root_area.top + 42,
          "the bar owns the first 42 px row inside the padded root");
    check(content_row.top >= bar_row.bottom,
          "the content still starts below the system bar");
    check(!intersects(bar_row, content_row),
          "the content does not intersect the system bar");
    check(content_row.bottom == root_area.bottom,
          "the content still fills the row left below the bar, inside the padding");

    /* Header: a view real anexada a barra ocupa a area inteira, sem deslocamento
     * interno -- nem pelo cromo da barra, nem pelo cromo proprio do header. */
    cyberdeck_header_view::view header;
    check(header.create(bar), "the header view attaches to the persistent system bar");
    check(bar->children.size() == 1,
          "the persistent bar holds only the header the UI attached to it");
    lv_obj_t *header_row = bar->children.front();
    check(header_row->parent == bar, "the header is composed inside the bar");
    check(header_row->border_width == 0 && header_row->pad_left == 0 &&
              header_row->pad_right == 0 && header_row->pad_top == 0 &&
              header_row->pad_bottom == 0,
          "the header drops its own border and padding too");
    check(header_row->width == LV_PCT(100) && header_row->height == 42,
          "the header claims the whole bar width by the full 42 px height");

    const box bar_area = child_box(root_area, bar->width, bar->height);
    const box bar_content = content_box(bar, bar_area);
    check(bar_content == bar_area,
          "the bar chrome contributes no inset to the area the header lands on");
    const box header_area = child_box(bar_content, header_row->width, header_row->height);
    check(header_area == bar_content,
          "the header occupies the whole bar area with no internal displacement");

    /* O cromo do header desloca o conteudo interno dele, nao o header: e por isso
     * que a grade titulo/relogio/celula direita tem de nascer na origem da linha. */
    const box header_content = content_box(header_row, header_area);
    check(header_content == header_area,
          "the header's own chrome does not displace its title, clock and right cell");
    check(!header_row->children.empty() && header_row->children.front()->parent == header_row,
          "the header composes its grid inside itself");
    const box title_area = child_box(header_content, header_row->children.front()->width,
                                     header_row->children.front()->height);
    check(title_area.left == header_area.left && title_area.top == header_area.top,
          "the title starts on the header row origin, not inside an inset");

    /* O detector precisa reagir a qualquer inset diferente de zero, senao as
     * asserts de "sem deslocamento" acima tambem passariam no cromo que esta
     * regressao removeu. */
    lv_obj_t inset_bar{};
    inset_bar.width = LV_PCT(100);
    inset_bar.height = 42;
    inset_bar.border_width = 2;
    inset_bar.pad_left = 8;
    inset_bar.pad_top = 8;
    const box inset_area = content_box(&inset_bar, bar_area);
    const box inset_header = child_box(inset_area, header_row->width, header_row->height);
    check(!(inset_area == bar_area),
          "a bar with any border or padding has a smaller content area than the bare row");
    check(lv_obj_get_content_width(&inset_bar) < lv_obj_get_width(&inset_bar),
          "the inset bar really loses width to its chrome");
    check(!(inset_header == bar_content),
          "the header is displaced inside a bar that kept its chrome, so the zero "
          "chrome is what makes it fill the row");

    adapter.deinit();
    check(!adapter.ready(), "the chrome scenario releases the composed root");
    lv_shim_active_screen() = nullptr;
}

} // namespace

int main()
{
    std::printf("==> TEST-WM-06/TEST-WM-05: API controlada de view/contexto\n");
    api_shape_scenario();
    std::printf("==> TEST-WM-01: superficie principal por aplicacao\n");
    surfaces_scenario();
    std::printf("==> TEST-WM-02: foco e ownership de input centralizados\n");
    focus_scenario();
    std::printf("==> TEST-WM-07: teardown, quiesciencia e invalidacao\n");
    teardown_scenario();
    std::printf("==> TEST-WM-04: notificacoes e transicoes entre apps\n");
    notifications_scenario();
    std::printf("==> TEST-WM-03: barra de sistema persistente\n");
    adapter_scenario();
    std::printf("==> TEST-REG-8-BAR: barra fixa e content abaixo (BUG-8-WM-BAR)\n");
    bar_layout_scenario();
    std::printf("==> TEST-REG-8-CHROME: cromo preto da barra e header sem deslocamento\n");
    bar_chrome_scenario();

    if (failures == 0) {
        std::printf("PASS: window manager (%d checks)\n", checks);
        return 0;
    }
    std::printf("FAIL: %d of %d window manager checks failed\n", failures, checks);
    return 1;
}