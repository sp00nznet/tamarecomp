cmake_minimum_required(VERSION 3.16)
project(tamarecomp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# No ROM ships with this repo. Point at your own dump:
#   cmake -B build -DTAMA_ROM=/path/to/tama.b
set(TAMA_ROM "" CACHE FILEPATH "Path to a Tamagotchi P1 ROM (tama.b, 12288 bytes)")
if(NOT TAMA_ROM)
    message(FATAL_ERROR
        "TAMA_ROM is not set. Configure with -DTAMA_ROM=/path/to/tama.b")
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated/tama_rom.c)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/generated)

add_custom_command(
    OUTPUT ${GENERATED}
    COMMAND ${CMAKE_COMMAND} -E env
            PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/tools
            ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/emit.py
            ${TAMA_ROM} ${GENERATED}
    DEPENDS ${TAMA_ROM}
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/emit.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/e0c6200.py
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/cycles.py
    COMMENT "Recompiling ${TAMA_ROM} to C"
    VERBATIM)

add_library(tamarecomp STATIC ${GENERATED} src/mem.c)
target_include_directories(tamarecomp PUBLIC include)

# The generated file is one 40k-line function. MSVC needs to be told that a
# switch with 6144 cases is intentional; GCC and Clang take it in stride.
if(MSVC)
    target_compile_options(tamarecomp PRIVATE /wd4102)
else()
    target_compile_options(tamarecomp PRIVATE -Wall -Wextra)
endif()

enable_testing()
add_executable(smoke tests/smoke.c)
target_link_libraries(smoke PRIVATE tamarecomp)
add_test(NAME smoke COMMAND smoke)
