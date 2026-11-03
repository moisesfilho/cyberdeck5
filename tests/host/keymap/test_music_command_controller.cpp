#include "platform/display/cyberdeck_music_command_input.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {
int checks = 0;
void expect(bool condition, const char *message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
struct app_probe {
    int executions = 0;
    std::string command;
    std::string response;
    int closes = 0;
};
std::string execute(std::string_view command, void *context) {
    auto &probe = *static_cast<app_probe *>(context);
    ++probe.executions;
    probe.command.assign(command);
    return probe.response;
}
void close_player(void *context) {
    ++static_cast<app_probe *>(context)->closes;
}
using namespace cyberdeck_music_command_input;
} // namespace

int main() {
    controller input;
    app_probe app{};
    input.open();
    input.insert_text("abc");
    expect(input.text() == "abc" && input.cursor() == 3, "physical text enters command buffer");
    expect(input.handle(key::left, execute, &app, close_player, &app) == key_result::edited, "left navigates");
    input.insert_text("X");
    expect(input.text() == "abXc" && input.cursor() == 3, "virtual text inserts at cursor");
    input.handle(key::home, execute, &app, close_player, &app);
    input.handle(key::del, execute, &app, close_player, &app);
    expect(input.text() == "bXc", "delete removes at cursor");
    input.handle(key::backspace, execute, &app, close_player, &app);
    expect(input.text() == "bXc", "backspace at start is safe");
    input.handle(key::end, execute, &app, close_player, &app);
    input.handle(key::backspace, execute, &app, close_player, &app);
    expect(input.text() == "bX", "backspace removes previous character");

    input.open();
    input.insert_text("éx");
    input.handle(key::left, execute, &app, close_player, &app);
    input.handle(key::backspace, execute, &app, close_player, &app);
    expect(input.text() == "x", "backspace removes complete UTF-8 codepoint");
    input.insert_text(std::string(100, 'a'));
    expect(input.text().size() == controller::max_bytes, "command buffer is bounded");

    input.open();
    input.insert_text("music help");
    app.response = "music: unknown command\n";
    expect(input.handle(key::enter, execute, &app, close_player, &app) == key_result::executed,
           "enter dispatches app command");
    expect(app.executions == 1 && app.command == "help", "optional music prefix is stripped once");
    expect(input.feedback() == app.response && !input.is_open(), "app error feedback retained and line closes");
    expect(input.handle(key::enter, execute, &app, close_player, &app) == key_result::ignored && app.executions == 1,
           "second enter cannot repeat dispatch");

    input.open();
    input.insert_text(" ");
    expect(input.handle(key::enter, execute, &app, close_player, &app) == key_result::executed,
           "empty command returns feedback");
    expect(app.executions == 1 && input.feedback().find("enter a subcommand") != std::string_view::npos,
           "empty command does not call app");
    input.open();
    input.insert_text("help");
    expect(input.handle(key::escape, execute, &app, close_player, &app) == key_result::closed_line && app.closes == 0,
           "first escape closes command only");
    expect(input.handle(key::escape, execute, &app, close_player, &app) == key_result::closed_player && app.closes == 1,
           "second escape closes player");
    expect(app.executions == 1, "escape never dispatches player command");

    /* TEST-MUSIC-ESC-07: the close-failure hint is bounded, replaces the
     * previous feedback and never survives reopening the command line. */
    input.set_feedback("music: close failed; press ESC to retry\n");
    expect(input.feedback() == "music: close failed; press ESC to retry\n",
           "close failure feedback is published");
    input.set_feedback(std::string(4 * controller::max_bytes, 'x'));
    expect(input.feedback().size() == controller::max_bytes, "feedback is bounded by the command buffer limit");
    input.set_feedback(std::string(controller::max_bytes - 1, 'a') + "\xc3\xa7");
    expect(input.feedback().size() == controller::max_bytes - 1 &&
               input.feedback().back() == 'a',
           "bounded feedback never splits a UTF-8 codepoint");
    input.open();
    expect(input.feedback().empty(), "reopening the command line clears the feedback");
    input.set_feedback("music: close failed; press ESC to retry\n");
    expect(input.handle(key::escape, execute, &app, close_player, &app) == key_result::closed_line &&
               input.feedback().find("close failed") != std::string_view::npos,
           "closing the command line keeps the close-failure hint");
    expect(input.handle(key::escape, execute, &app, close_player, &app) == key_result::closed_player &&
               input.feedback().find("close failed") != std::string_view::npos,
           "a failed close keeps the hint until the view reopens");
    std::cout << "test_music_command_controller: " << checks << " checks passed\n";
}
