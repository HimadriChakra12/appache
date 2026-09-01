CC_BASE  ?= clang
CXX_BASE ?= clang++
HAS_CCACHE := $(shell command -v ccache >/dev/null 2>&1 && echo 1)

ifeq ($(HAS_CCACHE),1)
  CC  := ccache $(CC_BASE)
  CXX := ccache $(CXX_BASE)
else
  CC  := $(CC_BASE)
  CXX := $(CXX_BASE)
endif

NPROC       := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
MAKEFLAGS   += -j$(NPROC)

CFLAGS_SPEED   := -pipe
CXXFLAGS_SPEED := -pipe

HAS_MOLD := $(shell command -v mold >/dev/null 2>&1 && echo 1)
HAS_LLD  := $(shell command -v ld.lld >/dev/null 2>&1 && echo 1)

ifeq ($(HAS_MOLD),1)
  LDFLAGS += -fuse-ld=mold
else ifeq ($(HAS_LLD),1)
  LDFLAGS += -fuse-ld=lld
endif

DEPFLAGS = -MMD -MP
%.o: CFLAGS += $(DEPFLAGS)
DEPS := $(OBJ:.o=.d)
-include $(DEPS)

CFLAGS   += $(CFLAGS_SPEED)
CXXFLAGS += $(CXXFLAGS_SPEED)

.SUFFIXES:
MAKEFLAGS += --no-builtin-rules --no-builtin-variables
