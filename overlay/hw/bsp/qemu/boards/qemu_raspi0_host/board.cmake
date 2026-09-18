set(CMAKE_SYSTEM_CPU arm1176jzf-s CACHE INTERNAL "System Processor")

function(update_board TARGET)
  target_compile_definitions(${TARGET} PUBLIC
    BCM_VERSION=2835
    QEMU_RASPI0=1
    QEMU_ROLE_HOST=1
    )
endfunction()
