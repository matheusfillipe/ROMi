# CMake Toolchain File for PS3 PowerPC64 Cross-Compilation
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)

# Cross-compilation tools
set(CMAKE_C_COMPILER powerpc64-ps3-elf-gcc)
set(CMAKE_CXX_COMPILER powerpc64-ps3-elf-g++)
set(CMAKE_AR powerpc64-ps3-elf-ar)
set(CMAKE_RANLIB powerpc64-ps3-elf-ranlib)

# Compiler flags for PS3 Cell processor
set(CMAKE_C_FLAGS_INIT "-mcpu=cell -I/ps3dev/ppu/include")
set(CMAKE_CXX_FLAGS_INIT "-mcpu=cell -I/ps3dev/ppu/include")

# Tell CMake the compiler works
set(CMAKE_C_COMPILER_WORKS TRUE)
set(CMAKE_CXX_COMPILER_WORKS TRUE)

# Search paths for dependencies
set(CMAKE_FIND_ROOT_PATH /ps3dev)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)