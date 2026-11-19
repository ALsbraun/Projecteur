#!/usr/bin/env bash
set -euo pipefail
exec "$(dirname -- "${BASH_SOURCE[0]}")/../rpm/build-rpm.sh" opensuse "$@"
