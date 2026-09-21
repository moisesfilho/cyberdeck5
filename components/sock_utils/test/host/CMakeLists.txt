cmake_minimum_required(VERSION 3.16)

set(COMPONENTS main)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Projeto de host tests ESP-IDF (target linux) para o componente local
# components/sock_utils (overlay do espressif/sock_utils com getnameinfo
# dual-stack). Segue fielmente o padrao oficial do componente sock_utils
# (managed_components/espressif__sock_utils/test/host/CMakeLists.txt).
project(sock_utils_host_test)