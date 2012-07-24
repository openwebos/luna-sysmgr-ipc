# Device makefile
BUILD_TYPE := release
PLATFORM   := arm
TARGET_TYPE := DEVICE

LIBS := \
	-L$(LIB_DIR)

include Makefile.inc

stage:
	echo "nothing to do"

