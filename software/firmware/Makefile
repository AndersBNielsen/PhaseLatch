# Firmware Makefile (SDCC + fx2lib)
# Set FX2LIB to your fx2lib install path (contains include/fx2regs.h etc.)
FX2LIB ?= /Users/abn/install/max2771_fx2lp/FX2LP/fx2lib
SDCC    ?= sdcc

PROJ = streamer
SRC  = src/main.c src/descriptors.c src/setup_callbacks.c
# Prefer project headers over fx2lib to avoid name collisions
INC  = -Iinclude -Isrc -I$(FX2LIB)/include
CFLAGS = -mmcs51 --std-sdcc11 --opt-code-size

BUILD = build
OBJS  = $(BUILD)/main.rel $(BUILD)/descriptors.rel $(BUILD)/setup_callbacks.rel
LIBS  = $(FX2LIB)/lib/fx2.lib
FLASH_DELAY ?= 0.5

all: $(BUILD)/$(PROJ).ihx

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/main.rel: src/main.c | $(BUILD)
	$(SDCC) $(CFLAGS) $(INC) -c $< -o $@

$(BUILD)/descriptors.rel: src/descriptors.c | $(BUILD)
	$(SDCC) $(CFLAGS) $(INC) -c $< -o $@

$(BUILD)/setup_callbacks.rel: src/setup_callbacks.c | $(BUILD)
	$(SDCC) $(CFLAGS) $(INC) -c $< -o $@

$(BUILD)/$(PROJ).ihx: $(OBJS)
	$(SDCC) $(CFLAGS) $(INC) $^ $(LIBS) -o $@

flash: $(BUILD)/$(PROJ).ihx
	cycfx2prog prg:$< run

flash-twice: $(BUILD)/$(PROJ).ihx
	cycfx2prog prg:$< run
	sleep $(FLASH_DELAY)
	cycfx2prog prg:$< run

clean:
	rm -rf $(BUILD)

.PHONY: all clean flash flash-twice
