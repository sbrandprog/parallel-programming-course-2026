
set default-list := true

_configure_template preset:
    cmake --preset {{preset}} --fresh

_build_template preset:
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}

configure_debug: (_configure_template "linux-debug")
configure_release: (_configure_template "linux-release")

build_debug: (_build_template "linux-debug")
build_release: (_build_template "linux-release")

configure: configure_debug
build: build_debug

run: build_release
    out/build/linux-release/lab1/lab1
