# tfcc_mm - Linux build (metamod-p plugin) -> tfcc_mm_i386.so
#
#   make METAMOD_SDK=/path/to/metamod-p
#
# METAMOD_SDK is a checkout of https://github.com/Bots-United/metamod-p
# (the folder that holds metamod/ and hlsdk/). CI clones it automatically.

METAMOD_SDK ?= ../../build/metamod-p
CXX         ?= g++
TARGET       = tfcc_mm_i386.so

INCLUDES = -I$(METAMOD_SDK)/metamod -I$(METAMOD_SDK)/hlsdk/dlls -I$(METAMOD_SDK)/hlsdk/engine \
           -I$(METAMOD_SDK)/hlsdk/common -I$(METAMOD_SDK)/hlsdk/pm_shared
CXXFLAGS = -m32 -march=i686 -O2 -fPIC -fvisibility=hidden -fno-exceptions -fno-rtti \
           -fno-stack-protector -U_FORTIFY_SOURCE -Dlinux -D__linux__ -Wno-write-strings
LDFLAGS  = -m32 -shared -static-libgcc -Wl,--no-undefined

$(TARGET): tfcc_mm.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $< -lm
	strip --strip-unneeded $@

clean:
	rm -f $(TARGET)

.PHONY: clean
