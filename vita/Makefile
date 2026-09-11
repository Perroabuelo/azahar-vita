VITASDK ?= $(error Set VITASDK to your VitaSDK installation)

PORT_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
ROOT_DIR := $(abspath $(PORT_DIR)/..)
BUILD_DIR := $(ROOT_DIR)/build-vita
TARGET := azahar_vita_probe

CXX := $(VITASDK)/bin/arm-vita-eabi-g++
SIZE := $(VITASDK)/bin/arm-vita-eabi-size
ELF_CREATE := $(VITASDK)/bin/vita-elf-create
MAKE_FSELF := $(VITASDK)/bin/vita-make-fself
MAKE_SFO := $(VITASDK)/bin/vita-mksfoex
PACK_VPK := $(VITASDK)/bin/vita-pack-vpk

CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -O2
LDLIBS := -lSceCtrl_stub -lSceDisplay_stub -lSceIofilemgr_stub \
          -lSceKernelThreadMgr_stub -lSceSysmem_stub

.PHONY: all clean size

all: $(BUILD_DIR)/$(TARGET).vpk

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/$(TARGET).elf: $(PORT_DIR)/src/main.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $< -o $@ $(LDLIBS)

$(BUILD_DIR)/$(TARGET).velf: $(BUILD_DIR)/$(TARGET).elf
	$(ELF_CREATE) $< $@

$(BUILD_DIR)/eboot.bin: $(BUILD_DIR)/$(TARGET).velf
	$(MAKE_FSELF) $< $@

$(BUILD_DIR)/param.sfo: | $(BUILD_DIR)
	$(MAKE_SFO) -s TITLE_ID=AZHV00001 -s APP_VER=00.01 "Azahar Vita Probe" $@

$(BUILD_DIR)/$(TARGET).vpk: $(BUILD_DIR)/eboot.bin $(BUILD_DIR)/param.sfo
	$(PACK_VPK) -s $(BUILD_DIR)/param.sfo -b $(BUILD_DIR)/eboot.bin $@

size: $(BUILD_DIR)/$(TARGET).elf
	$(SIZE) $<

clean:
	rm -f $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).velf
	rm -f $(BUILD_DIR)/eboot.bin $(BUILD_DIR)/param.sfo $(BUILD_DIR)/$(TARGET).vpk
