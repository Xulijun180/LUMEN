# file-format: 1.0.0
cmake_minimum_required(VERSION 3.30)

set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

include(cmake/target.cmake)
include(cmake/${TOOLCHAIN_FILE})

project(LUMEN)
add_executable(LUMEN)
include(cmake/files.cmake)
include(cmake/flags.cmake)
include(cmake/components.cmake)

# Lumen UI library from the repo root, built with the same CPU flags
add_subdirectory(../.. lumen)

# Demo pages for this example, optimised like the library
add_library(lumen_apps STATIC
  apps/app_common.c apps/app_boot.c apps/app_home.c apps/app_clock.c apps/app_music.c
  apps/app_monitor.c apps/app_settings.c apps/app_about.c)
target_include_directories(lumen_apps PUBLIC apps)
target_compile_options(lumen_apps PRIVATE ${CPU_FLAGS} ${LUMEN_OPT} -g -std=gnu11
  -ffunction-sections -fdata-sections --specs=nano.specs -fsingle-precision-constant -fstack-usage
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(lumen_apps PUBLIC lumen_ui)

# Stubs to be able to add compiler flag overrides, e.g. -Os
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  # Add additional source files here
  port/lumen_port.c                  # board port: SSD1306 on I2C1 + 3 buttons
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional include directories here
  port
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional defined symbols here
)

target_compile_options(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional compiler flags here
)

target_link_options(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional linker flags here
)

target_link_directories(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional linker search directories here
)

target_link_libraries(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional linker libraries here
)

# Plain signature on purpose: cmake/components.cmake (generated) uses it,
# and CMake forbids mixing plain and keyword forms on one target.
target_link_libraries(${CMAKE_PROJECT_NAME} lumen_apps)
