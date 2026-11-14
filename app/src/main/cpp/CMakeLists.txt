cmake_minimum_required(VERSION 3.22.1)
project(game C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 99)

# --- Raylib 5.5: берём локальную копию app/src/main/cpp/raylib (если есть), иначе скачиваем при сборке ---
include(FetchContent)
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/raylib/src/raylib.h)
  set(RAYLIB_SRC ${CMAKE_CURRENT_SOURCE_DIR}/raylib/src)
else()
  FetchContent_Declare(raylib_src URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz)
  FetchContent_GetProperties(raylib_src)
  if(NOT raylib_src_POPULATED)
    FetchContent_Populate(raylib_src)
  endif()
  set(RAYLIB_SRC ${raylib_src_SOURCE_DIR}/src)
endif()

# NativeActivity glue из NDK (на нём построен Android-бэкенд raylib)
if(DEFINED ANDROID_NDK)
  set(NDK_DIR ${ANDROID_NDK})
else()
  set(NDK_DIR ${CMAKE_ANDROID_NDK})
endif()
set(GLUE_DIR ${NDK_DIR}/sources/android/native_app_glue)

add_library(game SHARED
  main.cpp game.cpp player.cpp map_gen.cpp entities.cpp procgen.cpp jni_bridge.cpp
  ${RAYLIB_SRC}/rcore.c ${RAYLIB_SRC}/rshapes.c ${RAYLIB_SRC}/rtextures.c
  ${RAYLIB_SRC}/rtext.c ${RAYLIB_SRC}/rmodels.c ${RAYLIB_SRC}/raudio.c ${RAYLIB_SRC}/utils.c
  ${GLUE_DIR}/android_native_app_glue.c)

target_include_directories(game PRIVATE ${RAYLIB_SRC} ${RAYLIB_SRC}/external ${GLUE_DIR})
target_compile_definitions(game PRIVATE PLATFORM_ANDROID GRAPHICS_API_OPENGL_ES2)
target_compile_options(game PRIVATE -Wno-everything)
target_link_options(game PRIVATE -u ANativeActivity_onCreate)
target_link_libraries(game log android EGL GLESv2 OpenSLES dl m)
