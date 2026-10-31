# CROSS=<triple> cross-compiles Windows binaries with MinGW-w64, e.g. CROSS=x86_64-w64-mingw32.
# Leave it empty for a native Linux build; `make windows` is the shortcut for the 64-bit triple.
CROSS   ?=
# STATIC=1 builds Linux binaries that carry their own GLFW and C++ runtime; X11 and glibc stay
# dynamic. `make linux-static` is the shortcut.
STATIC  ?=

ifeq ($(CROSS),)
CC      ?= cc
CXX     ?= g++
AR      ?= ar
else
# Plain := here, because ?= would not replace make's built-in cc/g++/ar defaults. A CC=... given
# on the command line still wins over these.
CC      := $(CROSS)-gcc
CXX     := $(CROSS)-g++
AR      := $(CROSS)-ar
endif
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic -std=c99
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++11
# _POSIX_C_SOURCE is only there for glibc's open_memstream(); on Windows mingw would also declare
# localtime_r from that define and clash with the shim inside imgui.cpp.
CPPFLAGS += $(if $(CROSS),,-D_POSIX_C_SOURCE=200809L)
LDLIBS  ?= -lm
EXE     := $(if $(CROSS),.exe,)

SRC_DIR := src
# A separate object directory per target, so cross objects never mix with native ones.
OBJ_DIR := build$(if $(CROSS),-$(CROSS),$(if $(STATIC),-static,))
BIN     := windwaker-hd-editor-cli$(EXE)
GUI_BIN := windwaker-hd-editor$(EXE)
HEADERS := src/save_format.h

# ---- GLFW: distro package for a plain native build, submodule sources otherwise --------------
# GLFW is a git submodule pinned to 3.4 (commit 7b6aead). The file lists mirror
# glfw-3.4/src/CMakeLists.txt: the shared sources and the always-built null backend (platform.c
# links against it), plus the front end of the target platform. egl/osmesa compile to nothing
# without _GLFW_EGL/_GLFW_OSMESA.
GLFW_DIR    := libs/glfw
GLFW_LIB    := $(OBJ_DIR)/libglfw3.a
GLFW_COMMON := context init input monitor platform vulkan window egl_context osmesa_context \
               null_init null_monitor null_window null_joystick
ifeq ($(CROSS),)
# The X11 front end, plus the posix/linux files behind time, threads, dlopen, polling, joysticks.
GLFW_SRCS   := $(GLFW_COMMON) x11_init x11_monitor x11_window xkb_unicode glx_context \
               posix_module posix_thread posix_time posix_poll linux_joystick
GLFW_CFLAGS := -D_GLFW_X11
else
GLFW_SRCS   := $(GLFW_COMMON) win32_init win32_module win32_monitor win32_time win32_thread \
               win32_window wgl_context win32_joystick
GLFW_CFLAGS := -D_GLFW_WIN32 -D_CRT_SECURE_NO_WARNINGS
endif
GLFW_CFLAGS += -O2 -Wall -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src
GLFW_OBJS   := $(addprefix $(OBJ_DIR)/glfw/,$(addsuffix .o,$(GLFW_SRCS)))
# yes when GLFW is compiled from the submodule instead of taken from the distro package.
GLFW_BUILT  := $(if $(CROSS),yes,$(if $(STATIC),yes,))

# Dear ImGui is a git submodule in libs/imgui, compiled into the GUI, per the upstream guide.
IMGUI_DIR    := libs/imgui
IMGUI_CXX    := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp \
                $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
                $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS   := $(addprefix $(OBJ_DIR)/,$(notdir $(IMGUI_CXX:.cpp=.o)))
# The vendored GLFW headers have to match the vendored sources, so they shadow the distro ones.
IMGUI_INC    := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(if $(GLFW_BUILT),-I$(GLFW_DIR)/include,)
# MinGW's gcc reports false -Warray-bounds on ImGui's mouse-button arrays; the vendored code is
# not ours to patch, so the cross build keeps its output readable.
IMGUI_NOWARN := $(if $(CROSS),-Wno-array-bounds,)

# GLFW_INCLUDE_NONE stops glfw3.h from including <GL/gl.h>; the OpenGL entry points come from the
# loader vendored with imgui_impl_opengl3, which dlopens the host driver at runtime.
GLFW_NO_GL   := -DGLFW_INCLUDE_NONE

# The GUI needs GLFW and X11 headers only - OpenGL entry points come from the loader vendored with
# the ImGui backend, so nothing links or builds against Mesa. X11 is read for the Xft.dpi scale
# and is a GLFW dependency anyway.
ifeq ($(CROSS),)
DL_LIB       := -ldl
X11_CFLAGS   := $(shell pkg-config --cflags x11 2>/dev/null)
ifneq ($(STATIC),)
# The static Linux build takes GLFW from the submodule and links libgcc/libstdc++ in. X11 and
# glibc stay dynamic: the .so files are on every desktop, static X11 archives and a static libc
# are not packaged by most distros, Arch (the AppImage build container) included.
GLFW_LIBS   := -lpthread
X11_LIBS    := -lX11 -lXrandr -lXi -lXcursor -lXinerama -lXext
GUI_LDFLAGS := -static-libgcc -static-libstdc++
LDFLAGS     := -static-libgcc
else
GLFW_LIBS   := $(shell pkg-config --libs glfw3 2>/dev/null)
X11_LIBS    := $(shell pkg-config --libs x11 2>/dev/null)
endif
else
# Windows links the GLFW archive built from the submodule, so the .exe ships no glfw3.dll. Only
# the archive's own dependencies go here: the archive itself reaches the link line as a
# prerequisite of the GUI. -static also covers libgcc/libstdc++/winpthread, and -mwindows keeps
# the console window away when the editor is double-clicked (stdout still reaches a console it was
# started from).
GLFW_LIBS   := -lgdi32 -luser32 -lwinmm -luuid -lole32 -lsetupapi
GUI_LDFLAGS := -static -mwindows
endif

# Default goal: both binaries of the current configuration.
.DEFAULT_GOAL := linux
.PHONY: linux windows linux-static test info help clean

linux: $(BIN) $(GUI_BIN)

$(BIN): $(OBJ_DIR)/main.o $(OBJ_DIR)/save_format.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(GUI_BIN): $(OBJ_DIR)/gui_main.o $(OBJ_DIR)/save_format.o $(IMGUI_OBJS) $(if $(GLFW_BUILT),$(GLFW_LIB),)
	$(CXX) $(CXXFLAGS) $(GUI_LDFLAGS) -o $@ $^ $(GLFW_LIBS) $(X11_LIBS) $(DL_LIB) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJ_DIR)/gui_main.o: $(SRC_DIR)/gui_main.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(X11_CFLAGS) $(IMGUI_INC) -c -o $@ $<

$(OBJ_DIR)/%.o: $(IMGUI_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(IMGUI_INC) $(IMGUI_NOWARN) -c -o $@ $<

$(OBJ_DIR)/%.o: $(IMGUI_DIR)/backends/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(IMGUI_INC) $(IMGUI_NOWARN) -c -o $@ $<

$(GLFW_LIB): $(GLFW_OBJS)
	$(AR) rcs $@ $^

$(OBJ_DIR)/glfw/%.o: $(GLFW_DIR)/src/%.c | $(OBJ_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(GLFW_CFLAGS) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# A sub-make per configuration; the goals are spelled out because the sub-make is what appends
# .exe. `linux-static` is what the release zip ships.
windows:
	@$(MAKE) CROSS=x86_64-w64-mingw32 windwaker-hd-editor-cli.exe windwaker-hd-editor.exe

linux-static:
	@$(MAKE) STATIC=1 windwaker-hd-editor-cli windwaker-hd-editor

test: $(BIN)
	sh tests/parity.sh

# libs/glfw is a submodule and survives clean; drop the build dirs and binaries only.
clean:
	rm -rf build build-static build-*mingw* windwaker-hd-editor windwaker-hd-editor-cli \
	       windwaker-hd-editor.exe windwaker-hd-editor-cli.exe
