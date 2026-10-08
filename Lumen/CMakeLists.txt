# Lumen UI: portable core + demo apps + u8g2, as one static library.
# Nothing in here depends on the HAL; the board port (port/lumen_port.c) is
# compiled into the firmware target by the top-level CMakeLists.txt.

file(GLOB LUMEN_U8G2_SRC ${CMAKE_CURRENT_LIST_DIR}/u8g2/csrc/*.c)

add_library(lumen_ui STATIC
  ${LUMEN_U8G2_SRC}
  u8g2/u8g2_fonts_subset.c          # only the 6 fonts Lumen uses (full file is 40 MB)
  lumen/src/lm_anim.c lumen/src/lm_math.c lumen/src/lm_gfx.c lumen/src/lm_input.c
  lumen/src/lm_settings.c lumen/src/lm_ui.c lumen/src/lm_widgets.c lumen/src/lm_overlay.c
  lumen/src/lm_menu.c lumen/src/lm_icons.c
  apps/app_common.c apps/app_boot.c apps/app_home.c apps/app_clock.c apps/app_music.c
  apps/app_monitor.c apps/app_settings.c apps/app_about.c)

target_include_directories(lumen_ui PUBLIC lumen/include apps u8g2/csrc)

# Same CPU/ABI flags as the firmware, but optimised even in Debug: the
# rasteriser is the hot path and is rarely stepped through.
set(LUMEN_OPT "-O2" CACHE STRING "Optimisation level for the Lumen library")
target_compile_options(lumen_ui PRIVATE ${CPU_FLAGS} ${LUMEN_OPT} -g -std=gnu11
  -ffunction-sections -fdata-sections --specs=nano.specs -fsingle-precision-constant -fstack-usage)
# warnings on for Lumen, off for third-party u8g2
target_compile_options(lumen_ui PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
set_source_files_properties(${LUMEN_U8G2_SRC} u8g2/u8g2_fonts_subset.c PROPERTIES COMPILE_OPTIONS "-w")
target_link_libraries(lumen_ui PUBLIC m)
