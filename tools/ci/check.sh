#!/usr/bin/env bash
set -euo pipefail

source "${IDF_PATH:?IDF_PATH must point to ESP-IDF}/export.sh" >/dev/null
idf.py build
