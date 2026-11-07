# Lumen UI library: core + widgets + fonts + bundled u8g2 (fonts only).
# Hardware-free: a board supplies an lm_port_t (see core/lm_ui.h) and adds
# this directory with add_subdirectory(). Examples live under examples/.
cmake_minimum_required(VERSION 3.20)

file(GLOB LUMEN_U8G2_SRC ${CMAKE_CURRENT_LIST_DIR}/third_party/u8g2/csrc/*.c)

add_library(lumen_ui STATIC
  ${LUMEN_U8G2_SRC}
  fonts/u8g2_fonts_subset.c          # only the 6 fonts Lumen uses (full file is 40 MB)
  core/lm_anim.c core/lm_math.c core/lm_gfx.c core/lm_input.c
  core/lm_settings.c core/lm_ui.c core/lm_overlay.c
  widget/lm_widgets.c widget/lm_menu.c widget/lm_icons.c)

target_include_directories(lumen_ui PUBLIC core widget fonts third_party/u8g2/csrc)

# Same CPU/ABI flags as the firmware (CPU_FLAGS comes from the including
# project), but optimised even in Debug: the rasteriser is the hot path and
# is rarely stepped through.
set(LUMEN_OPT "-O2" CACHE STRING "Optimisation level for the Lumen library")
target_compile_options(lumen_ui PRIVATE ${CPU_FLAGS} ${LUMEN_OPT} -g -std=gnu11
  -ffunction-sections -fdata-sections --specs=nano.specs -fsingle-precision-constant -fstack-usage)
# warnings on for Lumen, off for third-party u8g2
target_compile_options(lumen_ui PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
set_source_files_properties(${LUMEN_U8G2_SRC} fonts/u8g2_fonts_subset.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(lumen_ui PUBLIC m)
