#pragma once

#include <cerrno>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

enum class cyberdeck_local_shell_status { handled, passthrough, rejected };

struct cyberdeck_local_shell_result {
    cyberdeck_local_shell_status status;
    std::string output;
};

// Bounded, cat-only operation used by the asynchronous worker.  The returned
// std::string owns the file contents on the heap; no shell state is created.
cyberdeck_local_shell_result cyberdeck_local_shell_cat(const char *host_root, const char *cwd, const char *command);
cyberdeck_local_shell_result cyberdeck_local_shell_cat_bounded(const char *host_root, const char *cwd,
                                                               const char *command, std::size_t max_bytes);

// Internal SDK backend. Applications never receive these functions or a VFS
// handle; storage_facade owns the confined transaction.
enum class cyberdeck_storage_stage { write_temp, flush_fsync_file, rename, fsync_directory };
struct cyberdeck_storage_result {
    bool ok = false;
    cyberdeck_storage_stage stage = cyberdeck_storage_stage::write_temp;
    int error = 0;
    cyberdeck_storage_stage rollback_stage = cyberdeck_storage_stage::write_temp;
    int rollback_error = 0;
    bool rollback_attempted = false;
};
cyberdeck_storage_result cyberdeck_local_shell_storage_write_temp(const char *host_root, const char *cwd,
                                                                  std::string_view path, const char *bytes,
                                                                  std::size_t size);
cyberdeck_storage_result cyberdeck_local_shell_storage_flush(const char *host_root, const char *cwd,
                                                             std::string_view path);
cyberdeck_storage_result cyberdeck_local_shell_storage_rename(const char *host_root, const char *cwd,
                                                              std::string_view temp_path, std::string_view path);
cyberdeck_storage_result cyberdeck_local_shell_storage_fsync_directory(const char *host_root, const char *cwd,
                                                                        std::string_view path);
enum class cyberdeck_recovery_action { restored, removed, ambiguous, preserved, failed };
enum class cyberdeck_recovery_stage { validate, enumerate, inspect, rename, remove };
struct cyberdeck_recovery_entry {
    std::string sidecar;
    cyberdeck_recovery_action action;
    cyberdeck_recovery_stage stage;
    int error;
};
struct cyberdeck_recovery_report {
    std::vector<cyberdeck_recovery_entry> entries;
};
cyberdeck_recovery_report recover_save_sidecars(const char *host_root);

class cyberdeck_local_shell {
  public:
    cyberdeck_local_shell(const std::string &host_root, const std::string &virtual_root = "/");
    cyberdeck_local_shell_result execute(const std::string &line);
    std::string cwd() const;

  private:
    std::string host_root_;
    std::string virtual_root_;
    std::string cwd_;
};
