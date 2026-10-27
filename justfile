
set default-list := true

import "lab2/lab2.just"

_configure_template preset:
    cmake --preset {{preset}} --fresh

_build_template preset:
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}

configure_debug: (_configure_template "linux-debug")
configure_release: (_configure_template "linux-release")
configure_tsan: (_configure_template "linux-tsan")

build_debug: (_build_template "linux-debug")
build_release: (_build_template "linux-release")
build_tsan: (_build_template "linux-tsan")

configure: configure_debug
build: build_debug

run_lab1: build_release
    out/build/linux-release/lab1/lab1

run_lab2 *args: build_release
    out/build/linux-release/lab2/lab2 {{args}}

run_lab2_tsan *args: build_tsan
    out/build/linux-tsan/lab2/lab2 {{args}}
