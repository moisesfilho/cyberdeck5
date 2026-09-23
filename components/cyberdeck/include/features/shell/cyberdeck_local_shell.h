#pragma once

#include <string>

enum class cyberdeck_local_shell_status { handled, passthrough, rejected };

struct cyberdeck_local_shell_result {
    cyberdeck_local_shell_status status;
    std::string output;
};

// Bounded, cat-only operation used by the asynchronous worker.  The returned
// std::string owns the file contents on the heap; no shell state is created.
cyberdeck_local_shell_result cyberdeck_local_shell_cat(const char *host_root,
                                                       const char *cwd,
                                                       const char *command);

class cyberdeck_local_shell {
public:
    cyberdeck_local_shell(const std::string &host_root,
                          const std::string &virtual_root = "/sdcard");
    cyberdeck_local_shell_result execute(const std::string &line);
    std::string cwd() const;

private:
    std::string host_root_;
    std::string virtual_root_;
    std::string cwd_;
};
