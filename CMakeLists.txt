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

# Lumen UI: portable library (core + apps + u8g2), built with the same CPU flags
add_subdirectory(Lumen)

# Stubs to be able to add compiler flag overrides, e.g. -Os
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  # Add additional source files here
  Lumen/port/lumen_port.c            # board port: SSD1306 on I2C1 + 3 buttons
)

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
  # Add additional include directories here
  Lumen/port
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
target_link_libraries(${CMAKE_PROJECT_NAME} lumen_ui)
