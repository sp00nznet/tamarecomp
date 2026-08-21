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

add_library(tamarecomp STATIC ${GENERATED} src/hw.c src/lcd.c)
target_include_directories(tamarecomp PUBLIC include)

# The generated file is one 40k-line function. MSVC needs to be told that a
# switch with 6144 cases is intentional; GCC and Clang take it in stride.
if(MSVC)
    target_compile_options(tamarecomp PRIVATE /wd4102)
else()
    target_compile_options(tamarecomp PRIVATE -Wall -Wextra)
endif()

# The Tamagotchi itself.
add_executable(tama src/main.c)
target_link_libraries(tama PRIVATE tamarecomp)

enable_testing()

# Boots, runs a minute of Tamagotchi time, never leaves the ROM.
add_executable(smoke tests/smoke.c)
target_link_libraries(smoke PRIVATE tamarecomp)
add_test(NAME smoke COMMAND smoke)

# The screen changes over time, which only happens if the clock timer
# interrupt is really being delivered.
add_executable(lcdprobe tests/lcdprobe.c)
target_link_libraries(lcdprobe PRIVATE tamarecomp)
add_test(NAME lcd COMMAND lcdprobe 20)

# Buttons reach the ROM and change what is on screen.
add_executable(buttons tests/buttons.c)
target_link_libraries(buttons PRIVATE tamarecomp)
add_test(NAME buttons COMMAND buttons)

# Differential test against an independent implementation of the same ISA.
# Needs a second copy of the generated code built with the trace hook live,
# plus an extracted BrickEmuPy checkout:
#
#   ctest --test-dir build            # skips difftest unless BRICKEMU_DIR is set
#   BRICKEMU_DIR=/path/to/BrickEmuPy-main ctest --test-dir build
add_library(tamarecomp_traced STATIC ${GENERATED} src/hw.c src/lcd.c)
target_include_directories(tamarecomp_traced PUBLIC include)
target_compile_definitions(tamarecomp_traced PUBLIC TAMA_TRACING)

add_executable(tracedump tests/tracedump.c)
target_link_libraries(tracedump PRIVATE tamarecomp_traced)

if(DEFINED ENV{BRICKEMU_DIR})
    add_test(NAME difftest COMMAND ${Python3_EXECUTABLE}
             ${CMAKE_CURRENT_SOURCE_DIR}/tools/difftest.py
             ${TAMA_ROM} $<TARGET_FILE:tracedump> 1000000)
endif()
