# Copyright (c) 2026 Christiaan (chris@boreddev.nl)
# BoredOS TinyGL & GLGears Makefile

CC = x86_64-boredos-gcc
AR = x86_64-boredos-ar

ifneq ($(BOREDOS_SDK),)
  SDK_PATH = $(BOREDOS_SDK)
else
  SDK_PATH = $(abspath ../../build/sdk)
endif

DESTDIR ?= $(abspath ../../build/dist)

CFLAGS  = -Wall -Wextra -std=gnu11 -ffreestanding -O2 -fno-stack-protector \
          -fno-stack-check -fno-lto -fno-pie -m64 -march=x86-64 -mno-red-zone \
          -I$(SDK_PATH)/include -Iinclude -Isrc

LDFLAGS = -static -no-pie -Wl,-Ttext=0x40000000 \
          -Wl,--no-dynamic-linker -Wl,-z,text -Wl,-z,max-page-size=0x1000 \
          -L$(SDK_PATH)/lib

LIB_SRCS = $(wildcard src/*.c)
LIB_OBJS = $(LIB_SRCS:src/%.c=obj/%.o)
LIB      = libtinygl.a

APP_OBJS = obj/glgears.o
APP      = glgears.elf

.PHONY: all bootstrap-sdk install bup clean

all: bootstrap-sdk $(LIB) $(APP)

bootstrap-sdk:
	@if [ ! -f "$(SDK_PATH)/include/novaproto.h" ]; then \
		if [ -d "../nova" ]; then \
			echo "Exporting Nova SDK components..."; \
			$(MAKE) -C ../nova BOREDOS_SDK=$(SDK_PATH) export-sdk; \
		fi \
	fi

obj/%.o: src/%.c | bootstrap-sdk
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

obj/glgears.o: glgears.c | bootstrap-sdk
	@mkdir -p obj
	$(CC) $(CFLAGS) -c $< -o $@

$(LIB): $(LIB_OBJS)
	@mkdir -p obj
	$(AR) rcs $@ $(LIB_OBJS)

$(APP): $(APP_OBJS) $(LIB)
	$(CC) $(APP_OBJS) $(LIB) -lnovaproto $(LDFLAGS) -o $@

install: all
	mkdir -p $(DESTDIR)/bin
	cp $(APP) $(DESTDIR)/bin/glgears
	mkdir -p $(DESTDIR)/usr/lib
	cp $(LIB) $(DESTDIR)/usr/lib/
	mkdir -p $(DESTDIR)/usr/include/TGL
	cp include/TGL/*.h $(DESTDIR)/usr/include/TGL/
	cp include/*.h $(DESTDIR)/usr/include/
	mkdir -p $(DESTDIR)/Library/AppData/org.boredos.tinygl
	@if [ -f glgears.desktop ]; then \
		cp glgears.desktop $(DESTDIR)/Library/AppData/org.boredos.tinygl/; \
	fi
	@if [ -f assets/tgl_minimal.png ]; then \
		cp assets/tgl_minimal.png $(DESTDIR)/Library/AppData/org.boredos.tinygl/glgears.png; \
	fi

bup: all
	rm -rf build/package
	mkdir -p build/package/bin build/package/assets build/package/lib build/package/include/TGL
	cp $(APP) build/package/bin/glgears
	cp $(LIB) build/package/lib/
	cp include/TGL/*.h build/package/include/TGL/
	cp include/*.h build/package/include/
	@if [ -f glgears.desktop ]; then \
		cp glgears.desktop build/package/assets/; \
	fi
	@if [ -f assets/tgl_minimal.png ]; then \
		cp assets/tgl_minimal.png build/package/assets/glgears.png; \
	fi
	@if [ -f MANIFEST.toml ]; then \
		cp MANIFEST.toml build/package/MANIFEST.toml; \
	else \
		@echo 'name = "tinygl"' > build/package/MANIFEST.toml; \
		@echo 'version = "1.0.0"' >> build/package/MANIFEST.toml; \
		@echo '[install]' >> build/package/MANIFEST.toml; \
		@echo 'bin = "/bin"' >> build/package/MANIFEST.toml; \
		@echo 'assets = "/Library/AppData/org.boredos.tinygl"' >> build/package/MANIFEST.toml; \
		@echo 'lib = "/usr/lib"' >> build/package/MANIFEST.toml; \
	fi
	x86_64-boredos-strip --strip-unneeded build/package/bin/glgears 2>/dev/null || true
	mkdir -p build
	tar -cf build/tinygl.tar -C build/package MANIFEST.toml bin assets lib include
	lz4 -f build/tinygl.tar build/tinygl.bup
	rm -f build/tinygl.tar
	rm -rf build/package

clean:
	rm -rf obj $(LIB) $(APP) build
