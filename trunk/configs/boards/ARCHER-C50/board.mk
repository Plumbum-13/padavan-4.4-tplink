##################################################################
# Board PID # Board Name       # PRODUCT # Note
##################################################################
# TP-Link # Archer C50v4  # MT7628  #
##################################################################

CFLAGS += -DBOARD_C50_V4 -DVENDOR_TPLINK
BOARD_NUM_USB_PORTS=0
CONFIG_BOARD_RAM_SIZE=64

### TP-LINK firmware description ###
TPLINK_HWID=0x001D589B
TPLINK_HWREV=0x93
TPLINK_HWREVADD=0x2
TPLINK_FLASHLAYOUT=16MSUmtk
TPLINK_HVERSION=3
