set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR mips)

set(TC /toolchains/mips32el--musl--stable-2024.02-1)
set(CMAKE_C_COMPILER   ${TC}/bin/mipsel-linux-gcc)
set(CMAKE_CXX_COMPILER ${TC}/bin/mipsel-linux-g++)

# Fully static so the binary runs on the glibc KE regardless of system libs
# (same approach guppyscreen uses with this musl toolchain).
set(CMAKE_C_FLAGS_INIT   "-Os")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
