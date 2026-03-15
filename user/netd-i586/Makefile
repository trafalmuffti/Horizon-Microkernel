# user/netd-i586/Makefile
#
# Standalone build for the Horizon network daemon (netd).
#
# This Makefile is invoked directly by build-user.sh when it detects a
# target-specific Makefile in the target directory.  It compiles:
#
#   • ne2000.c      – NE2000 ISA NIC driver
#   • main.c        – network daemon / wolfIP integration
#   • wolfip.c      – wolfIP TCP/IP stack (unity build, from submodule)
#
# The wolfIP source file is compiled with a separate, less-strict set of
# flags (no -Werror) to avoid failing on warnings inside the third-party code.
#
# Usage (from project root, via build-user.sh):
#   ./script/build.sh -t user -b netd -a i586

ifndef ARCH
ARCH = i586
endif

PROJECT_ROOT = ../..
ARCH_ROOT    = ../arch/$(ARCH)
LIB_ROOT     = $(PROJECT_ROOT)/lib

BINARY = build/netd-$(ARCH)/netd.elf

CROSS_CHAIN = /usr/local/cross/bin/$(ARCH)-elf
CC  = $(CROSS_CHAIN)-gcc
LD  = $(CROSS_CHAIN)-ld
AR  = $(CROSS_CHAIN)-ar

# ── Compiler flags shared by all sources ─────────────────────────────────

COMMON_CFLAGS =            \
	-O2                    \
	-std=c11               \
	-fno-builtin           \
	-ffreestanding         \
	-nostdlib              \
	-nostartfiles          \
	-nodefaultlibs

# Strict flags applied only to our own driver sources
DRIVER_CFLAGS = $(COMMON_CFLAGS) -Wall -Werror -Wno-unused-function

# Relaxed flags for the third-party wolfIP source (no -Werror)
WOLFIP_CFLAGS = $(COMMON_CFLAGS) -Wno-unused-function -Wno-unused-variable \
                -Wno-sign-compare -Wno-implicit-fallthrough

LDFLAGS = -T $(ARCH_ROOT)/link.ld

LIBS =                            \
	-L $(LIB_ROOT)/build/libh    \
	-l h-$(ARCH)

# ── Include paths ─────────────────────────────────────────────────────────
#
# For wolfIP sources the driver directory comes first so that:
#   "wolfip.h"  resolves to our wrapper (which silences LOG)
#   "config.h"  resolves to our Horizon-specific configuration
# before the originals in lib/wolfip/.
#
# The stubs/ sub-directory provides <stdlib.h> and <unistd.h> to satisfy
# wolfIP's unconditional includes without linking POSIX libc.

DRIVER_INCLUDES =                               \
	-I .                                        \
	-I $(LIB_ROOT)/wolfip                       \
	-I $(LIB_ROOT)/libh/include                 \
	-I $(LIB_ROOT)/libh/arch/$(ARCH)

WOLFIP_INCLUDES =                               \
	-I .                                        \
	-I ./stubs                                  \
	-I $(LIB_ROOT)/wolfip                       \
	-I $(LIB_ROOT)/libh/include                 \
	-I $(LIB_ROOT)/libh/arch/$(ARCH)

# ── Object files ─────────────────────────────────────────────────────────

OBJ_DIR  = build/netd-$(ARCH)/obj

DRIVER_SRCS = ne2000.c main.c
DRIVER_OBJS = $(patsubst %.c, $(OBJ_DIR)/%.o, $(DRIVER_SRCS))

WOLFIP_OBJ  = $(OBJ_DIR)/wolfip.o
WOLFIP_SRC  = $(LIB_ROOT)/wolfip/src/wolfip.c

CRT0_OBJ    = $(ARCH_ROOT)/build/crt0_min.o

ALL_OBJS = $(CRT0_OBJ) $(DRIVER_OBJS) $(WOLFIP_OBJ)

# ── Rules ─────────────────────────────────────────────────────────────────

.PHONY: all clean checkdirs

all: $(BINARY)

checkdirs: $(OBJ_DIR) $(ARCH_ROOT)/build

$(OBJ_DIR):
	@mkdir -p $@

$(ARCH_ROOT)/build:
	@mkdir -p $@

# Build crt0 assembly stub
$(CRT0_OBJ): $(ARCH_ROOT)/crt0_min.nasm | $(ARCH_ROOT)/build
	nasm -f elf $< -o $@

# Build driver sources (strict flags)
$(OBJ_DIR)/%.o: %.c | checkdirs
	$(CC) $(DRIVER_CFLAGS) $(DRIVER_INCLUDES) -c $< -o $@

# Build wolfIP (relaxed flags, custom include path to intercept headers)
$(WOLFIP_OBJ): $(WOLFIP_SRC) | checkdirs
	$(CC) $(WOLFIP_CFLAGS) $(WOLFIP_INCLUDES) -c $< -o $@

$(BINARY): $(ALL_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@ $(LIBS)

clean:
	@rm -rf build/netd-$(ARCH)
