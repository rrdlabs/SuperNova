# SuperNova Phase 2 ESP-IDF project.
#
# Build with a pinned ESP-IDF v5.2.2 environment:
#   source ~/.bashrc && source ~/devenv/env.sh
#   idf.py set-target esp32s3
#   idf.py build
#
# Physical flashing is intentionally not a normal build command. See
# scripts/flash-gate.sh and docs/flash-gate.md.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(phase_2)
