
set(EARLY_BOOT_SCRIPT ${PROJECT_BINARY_DIR}/bootloader/early_boot_script.ld)
set(STAGE3_SCRIPT ${PROJECT_BINARY_DIR}/bootloader/elf_stage3_script.ld)

set(BL_ST2_DATA_SEG 0x2000)
set(STAGES_1_AND_2_SIZE 4096)
set(STAGE3_ENTRY_OFF 4096)

hex2dec(${BL_ST2_DATA_SEG} BL_ST2_DATA_SEG_DEC)

math(EXPR BL_BASE_ADDR
   "${BL_ST2_DATA_SEG_DEC} * 16 + ${STAGES_1_AND_2_SIZE} + ${STAGE3_ENTRY_OFF}")

configure_file(
   ${PROJECT_SOURCE_DIR}/include/common/generated_config_template.h
   ${PROJECT_BINARY_DIR}/generated_config.h
)

configure_file(
   ${PROJECT_SOURCE_DIR}/bootloader/early_boot_script.ld
   ${EARLY_BOOT_SCRIPT}
)

configure_file(
   ${PROJECT_SOURCE_DIR}/bootloader/elf_stage3_script.ld
   ${STAGE3_SCRIPT}
)


include_directories(${CMAKE_BINARY_DIR})
