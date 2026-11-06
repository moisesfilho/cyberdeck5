// Behavioral tests for the `ssh` console application (pure layer + manifest).

#include <cstdio>
#include <string>

#include "apps/ssh/cyberdeck_ssh_console.h"
#include "apps/ssh/cyberdeck_ssh_console_app.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool condition, const char *what)
{
    ++g_checks;
    if (!condition) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

void check_eq(const std::string &actual, const std::string &expected, const char *what)
{
    ++g_checks;
    if (actual != expected) {
        ++g_failures;
        std::printf("FAIL: %s\n  expected: %s\n  actual:   %s\n", what, expected.c_str(), actual.c_str());
    }
}

struct fake_port final : cyberdeck_ssh_console::port {
    bool accept = true;
    int sessions = 0;
    int errors = 0;
    std::string user;
    std::string host;
    int port = 0;
    std::string last_error;

    bool begin_session(const char *user_value, const char *host_value, int port_value) override
    {
        ++sessions;
        user = user_value != nullptr ? user_value : "";
        host = host_value != nullptr ? host_value : "";
        port = port_value;
        return accept;
    }
    void log_error(const char *message) override
    {
        ++errors;
        last_error = message != nullptr ? message : "";
    }
};

void test_target_parsing()
{
    fake_port port;
    check_eq(cyberdeck_ssh_console::execute(port, " user@10.0.0.9:2222"), "",
             "a valid target produces no local output");
    check(port.sessions == 1, "a valid target starts one session");
    check_eq(port.user, "user", "the login name is parsed");
    check_eq(port.host, "10.0.0.9", "the host is parsed");
    check(port.port == 2222, "the explicit port is parsed");
    check(port.errors == 0, "a started session logs no error");

    fake_port default_port;
    (void)cyberdeck_ssh_console::execute(default_port, " example.com");
    check(default_port.port == 22, "the port defaults to 22");
    check_eq(default_port.user, "root", "an absent login name keeps the legacy root default");

    fake_port ipv6;
    (void)cyberdeck_ssh_console::execute(ipv6, " [2001:db8::1]:2200");
    check_eq(ipv6.host, "2001:db8::1", "a bracketed IPv6 literal is parsed");
    check(ipv6.port == 2200, "the IPv6 port is parsed");
}

void test_usage_errors()
{
    for (const char *invalid : {"", "   ", " host:0", " user@:22", " 2001:db8::1", " host:99999"}) {
        fake_port port;
        check_eq(cyberdeck_ssh_console::execute(port, invalid), "usage: ssh [user@]host[:port]\n",
                 "an invalid target prints the legacy usage line");
        check(port.sessions == 0, "an invalid target never starts a session");
    }
}

void test_start_failure()
{
    fake_port port;
    port.accept = false;
    check_eq(cyberdeck_ssh_console::execute(port, " lab"),
             "[ERROR] unable to start SSH session\n",
             "a refused session prints the legacy error with a single newline");
    check(port.errors == 1, "a refused session logs exactly one error");
    check_eq(port.last_error, "[ERROR] unable to start SSH session",
             "the logged text matches the printed text");
}

void test_manifest_registration_shape()
{
    const cyberdeck_apps::manifest &manifest = cyberdeck_ssh_console_app::global_application().get_manifest();
    check_eq(std::string(manifest.id), "cyberdeck.ssh.shell", "the ssh console owns its own id");
    check(manifest.command_count == 1 && manifest.commands[0] == "ssh", "the manifest declares the ssh command");
    check(!manifest.owns_console, "the ssh console never owns the console");
    check(manifest.dependency_count == 0, "the ssh console depends on no service");
    check(manifest.command_help_entries[0].usage == "ssh [user@]host[:port]", "help keeps the approved usage");
}

} // namespace

int main()
{
    test_target_parsing();
    test_usage_errors();
    test_start_failure();
    test_manifest_registration_shape();

    std::printf("test_ssh_console: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
