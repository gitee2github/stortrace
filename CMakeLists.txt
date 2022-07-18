cmake_minimum_required(VERSION 3.16)
project(examples)
SET(CMAKE_C_COMPILER /usr/bin/clang)
SET(CMAKE_CXX_COMPILER /usr/bin/clang++)
set(BPFOBJECT_BPFTOOL_EXE /usr/sbin/bpftool)

set (git_cmd git)
set (git_arg clone -b libbpf-0.8.1 https://github.com/libbpf/libbpf.git)
message(STATUS "git cmd: ${git_cmd}")
execute_process(COMMAND ${git_cmd} ${git_arg}
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
)
  
message(STATUS "Done clone")

if (BPFOBJECT_BPFTOOL_EXE)
    message(STATUS "Finding bpftool:" ${BPFOBJECT_BPFTOOL_EXE}})
else()
    message(FATAL_ERROR "Failed to find bpftool executable")
endif()

execute_process(COMMAND bpftool btf dump file /sys/kernel/btf/vmlinux format c
                WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                TIMEOUT 3
                OUTPUT_FILE vmlinux.h
)
message(STATUS "Building vmlinux.h at:"  ${PROJECT_SOURCE_DIR}/vmlinux.h)

# Tell cmake where to find BpfObject module
list(APPEND CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)


# Build vendored libbpf

include(ExternalProject)
ExternalProject_Add(libbpf
  PREFIX libbpf
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/libbpf/src
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make
    BUILD_STATIC_ONLY=1
    OBJDIR=${CMAKE_CURRENT_BINARY_DIR}/libbpf/libbpf
    DESTDIR=${CMAKE_CURRENT_BINARY_DIR}/libbpf
    INCLUDEDIR=
    LIBDIR=
    UAPIDIR=
    install
  BUILD_IN_SOURCE TRUE
  INSTALL_COMMAND ""
  STEP_TARGETS build
)

set(LIBBPF_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/libbpf)
set(LIBBPF_LIBRARIES ${CMAKE_CURRENT_BINARY_DIR}/libbpf/libbpf.a)
set(BPFOBJECT_VMLINUX_H ${PROJECT_SOURCE_DIR}/vmlinux.h)
find_package(BpfObject REQUIRED)

# Create an executable for each application
file(GLOB apps *.bpf.c)
foreach(app ${apps})
  get_filename_component(app_stem ${app} NAME_WE)

  # Build object skeleton and depend skeleton on libbpf build
  bpf_object(${app_stem} ${app_stem}.bpf.c)
  add_dependencies(${app_stem}_skel libbpf-build)

  add_executable(${app_stem} ${app_stem}.c)
  target_link_libraries(${app_stem} ${app_stem}_skel)
endforeach()
