board_runner_args(jlink "--device=nRF52805_xxAA" "--speed=4000")

set(OPENOCD_NRF5_SUBFAMILY "nrf52")
board_runner_args(pyocd "--target=nrf52805" "--frequency=4000000")

include(${ZEPHYR_BASE}/boards/common/nrfjprog.board.cmake)
include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd-nrf5.board.cmake)