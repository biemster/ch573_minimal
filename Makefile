TARGET := minimal

TOOLCHAIN_PREFIX := riscv64-elf

C_SRCS := \
	./$(TARGET).c

S_SRCS := \
	./startup_CH573.S

OBJS := \
	$(foreach src,$(C_SRCS),$(subst ./,obj/,$(patsubst %.c,%.o,$(src)))) \
	$(foreach src,$(S_SRCS),$(subst ./,obj/,$(patsubst %.S,%.o,$(src))))

MAKEFILE_DEPS := \
	$(foreach obj,$(OBJS),$(patsubst %.o,%.d,$(obj)))

SECONDARY_FLASH := $(TARGET).hex
SECONDARY_LIST := $(TARGET).lst
SECONDARY_SIZE := $(TARGET).siz
SECONDARY_BIN := $(TARGET).bin

ARCH := rv32imac_zicsr

CFLAGS_COMMON := \
	-march=$(ARCH) \
	-mabi=ilp32 \
	-mcmodel=medany \
	-msmall-data-limit=8 \
	-mno-save-restore \
	-Os \
	-fmessage-length=0 \
	-fsigned-char \
	-ffunction-sections \
	-fdata-sections

.PHONY: all
all: $(TARGET).elf secondary-outputs

.PHONY: clean
clean:
	-rm $(OBJS)
	-rm $(MAKEFILE_DEPS)
	-rm $(SECONDARY_FLASH)
	-rm $(SECONDARY_LIST)
	-rm $(SECONDARY_BIN)
	-rm $(TARGET).elf
	-rm $(TARGET).map
	-rm -r ./obj

.PHONY: secondary-outputs
secondary-outputs: $(SECONDARY_FLASH) $(SECONDARY_LIST) $(SECONDARY_SIZE) $(SECONDARY_BIN)

$(TARGET).elf: $(OBJS)
	${TOOLCHAIN_PREFIX}-gcc \
		$(CFLAGS_COMMON) \
		-T "linker.ld" \
		-nostartfiles \
		-Xlinker --gc-sections \
		-Xlinker --print-memory-usage \
		-Wl,-Map,"$(TARGET).map" \
		-Lobj \
		-o "$(TARGET).elf" \
		$(OBJS)

%.hex: %.elf
	@ ${TOOLCHAIN_PREFIX}-objcopy -O ihex "$<"  "$@"

%.bin: %.elf
	$(TOOLCHAIN_PREFIX)-objcopy -O binary $< "$@"

%.lst: %.elf
	@ ${TOOLCHAIN_PREFIX}-objdump \
		--source \
		--all-headers \
		--demangle \
		--line-numbers \
		--wide "$<" > "$@"

%.siz: %.elf
	@ ${TOOLCHAIN_PREFIX}-size --format=berkeley "$<"

obj/%.o: ./%.c
	@ mkdir --parents $(dir $@)
	@ ${TOOLCHAIN_PREFIX}-gcc \
		$(CFLAGS_COMMON) \
		-I. \
		-std=gnu99 \
		-MMD \
		-MP \
		-MF"$(@:%.o=%.d)" \
		-MT"$(@)" \
		-c \
		-o "$@" "$<"

obj/%.o: ./%.S
	@ mkdir --parents $(dir $@)
	@ ${TOOLCHAIN_PREFIX}-gcc \
		$(CFLAGS_COMMON) \
		-x assembler \
		-MMD \
		-MP \
		-MF"$(@:%.o=%.d)" \
		-MT"$(@)" \
		-c \
		-o "$@" "$<"

flash: 
	chprog ${SECONDARY_BIN}

f: clean all flash
