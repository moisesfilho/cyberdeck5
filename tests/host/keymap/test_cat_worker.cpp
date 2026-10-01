#include "apps/shell/cyberdeck_cat_worker.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "cyberdeck-cat-worker-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "nested");
    std::ofstream(root / "hello.txt") << "hello\nworld\n";
    std::ofstream(root / "nested/value.txt") << "nested\n";

    const auto read = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat hello.txt");
    assert(read.status == cyberdeck_local_shell_status::handled);
    assert(read.output == "hello\nworld\n");
    const auto nested = cyberdeck_cat_worker_process_request(root.c_str(), "/nested", "cat value.txt");
    assert(nested.status == cyberdeck_local_shell_status::handled);
    assert(nested.output == "nested\n");
    const auto rejected = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat ../hello.txt");
    assert(rejected.status != cyberdeck_local_shell_status::handled);
    const auto missing = cyberdeck_cat_worker_process_request(root.c_str(), "/", "cat missing.txt");
    assert(missing.status != cyberdeck_local_shell_status::handled);
    std::filesystem::remove_all(root);
    std::cout << "cat worker seam tests passed\n";
}
