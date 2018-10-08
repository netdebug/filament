cmake_minimum_required(VERSION 3.1)

project(filamentjs)

# The emcc options are not documented well, the best place to find them is the source:
# https://github.com/kripken/emscripten/blob/master/src/settings.js

set(EMCC_LOPTS "-s ALLOW_MEMORY_GROWTH=1 --bind -s MODULARIZE_INSTANCE=1 -s EXPORT_NAME=Filament")
set(EMCC_COPTS "-DEMSCRIPTEN_HAS_UNBOUND_TYPE_NAMES=0")

if (CMAKE_BUILD_TYPE MATCHES Debug)
    set(EMCC_COPTS "${EMCC_COPTS} -s ASSERTIONS=2")
    set(EMCC_LOPTS "${EMCC_LOPTS} -s DEMANGLE_SUPPORT=1")
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${EMCC_LOPTS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${EMCC_COPTS}")

add_executable(filamentjs jsbindings.cpp)
set_target_properties(filamentjs PROPERTIES OUTPUT_NAME filament)
target_link_libraries(filamentjs PRIVATE filament filagui image math utils)
