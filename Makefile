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

# ---- GLFW compiled from the submodule: the Windows cross build and the static Linux build ----
# GLFW is a git submodule pinned to 3.4 (commit 7b6aead); a plain native build keeps using the
# distro package. The file lists mirror glfw-3.4/src/CMakeLists.txt: the shared sources and the
# always-built null backend (platform.c links against it), plus one platform front end.
# egl/osmesa compile to nothing without _GLFW_EGL/_GLFW_OSMESA.
GLFW_VERSION := 3.4
GLFW_DIR     := libs/glfw
GLFW_LIB     := $(OBJ_DIR)/libglfw3.a
GLFW_COMMON  := context init input monitor platform vulkan window egl_context osmesa_context \
                null_init null_monitor null_window null_joystick
# X11 front end: x11_* and glx_context.c, plus the posix/linux files behind time, threads,
# dlopen, polling and joysticks.
GLFW_X11_SRCS   := $(GLFW_COMMON) x11_init x11_monitor x11_window xkb_unicode glx_context \
                   posix_module posix_thread posix_time posix_poll linux_joystick
GLFW_WIN32_SRCS := $(GLFW_COMMON) win32_init win32_module win32_monitor win32_time win32_thread \
                   win32_window wgl_context win32_joystick
# yes when GLFW is compiled from the submodule instead of taken from the distro package.
GLFW_BUILT   := $(if $(CROSS),yes,$(if $(STATIC),yes,))
ifeq ($(CROSS),)
GLFW_SRCS    := $(GLFW_X11_SRCS)
GLFW_PROBE   := $(GLFW_DIR)/src/x11_init.c
GLFW_CFLAGS  := -O2 -Wall -D_GLFW_X11 -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src
else
GLFW_SRCS    := $(GLFW_WIN32_SRCS)
GLFW_PROBE   := $(GLFW_DIR)/src/win32_init.c
GLFW_CFLAGS  := -O2 -Wall -D_GLFW_WIN32 -D_CRT_SECURE_NO_WARNINGS \
                -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src
endif
GLFW_OBJS    := $(addprefix $(OBJ_DIR)/glfw/,$(addsuffix .o,$(GLFW_SRCS)))

# Dear ImGui is a git submodule in libs/imgui, compiled into the GUI, per the upstream guide.
IMGUI_DIR    := libs/imgui
IMGUI_CXX    := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp \
                $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
                $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS   := $(notdir $(IMGUI_CXX:.cpp=.o))
IMGUI_OBJS   := $(addprefix $(OBJ_DIR)/,$(IMGUI_OBJS))
# The vendored GLFW headers have to match the vendored sources, so they shadow the distro ones
# whenever GLFW_BUILT is set.
IMGUI_INC    := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(if $(GLFW_BUILT),-I$(GLFW_DIR)/include,)
# MinGW's gcc reports false -Warray-bounds on ImGui's mouse-button arrays; the vendored code is
# not ours to patch, so the cross build keeps its output readable.
IMGUI_NOWARN := $(if $(CROSS),-Wno-array-bounds,)

# GLFW_INCLUDE_NONE stops glfw3.h from including <GL/gl.h>; the OpenGL entry points come from the
# loader vendored with imgui_impl_opengl3, which dlopens the host driver at runtime.
GLFW_NO_GL   := -DGLFW_INCLUDE_NONE

# The GUI needs GLFW and X11 headers only - OpenGL entry points come from the loader vendored with
# the ImGui backend, which dlopens the host driver at runtime, so nothing links or builds against
# Mesa. X11 is read for the Xft.dpi scale and is a GLFW dependency anyway.
ifeq ($(CROSS),)
GLFW_OK := $(shell pkg-config --exists glfw3 x11 2>/dev/null && echo yes)
GLFW_LIBS := $(shell pkg-config --libs glfw3 2>/dev/null)
X11_CFLAGS := $(shell pkg-config --cflags x11 2>/dev/null)
X11_LIBS := $(shell pkg-config --libs x11 2>/dev/null)
DL_LIB := -ldl
else
# Windows has no distro GLFW to link against, so the submodule sources are compiled into a static
# archive: the .exe ships no glfw3.dll.
# -static also covers libgcc/libstdc++/winpthread, -mwindows keeps the console window away when
# the editor is double-clicked (stdout still reaches a console it was started from).
GLFW_OK := $(shell test -f $(GLFW_DIR)/src/win32_init.c && echo yes)
# The archive itself reaches the link line as a prerequisite of the GUI; only its dependencies go
# here, otherwise it would be listed twice.
GLFW_LIBS := -lgdi32 -luser32 -lwinmm -luuid -lole32 -lsetupapi
X11_CFLAGS :=
X11_LIBS :=
DL_LIB :=
GUI_LDFLAGS := -static -mwindows
endif

# STATIC=1 (Linux): replace the distro GLFW with the submodule, link libgcc/libstdc++ in, and keep
# X11 dynamic - libX11/libXrandr/libXi/libXcursor/libXinerama/libXext are on every Linux desktop,
# while static X11 archives are not packaged by most distros. glibc stays dynamic as well: Arch,
# the AppImage build container, ships no libc.a.
ifneq ($(STATIC),)
GLFW_OK := $(shell test -f $(GLFW_PROBE) && echo yes)
GLFW_LIBS := -lpthread
# Fall back to plain -l flags if a slim build container is missing one of the .pc files.
X11_LIBS := $(shell pkg-config --libs x11 xrandr xi xcursor xinerama xext 2>/dev/null \
                  || echo -lX11 -lXrandr -lXi -lXcursor -lXinerama -lXext)
GUI_LDFLAGS := -static-libgcc -static-libstdc++
LDFLAGS := -static-libgcc
GLFW_CFLAGS += $(X11_CFLAGS)
endif

all: $(BIN)
ifeq ($(CROSS),)
ifeq ($(GLFW_OK),yes)
all: $(GUI_BIN)
endif
else
# A cross build that cannot link the GUI must fail loudly, not skip it silently.
all: $(GUI_BIN)
endif

$(BIN): $(OBJ_DIR)/main.o $(OBJ_DIR)/save_format.o
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ_DIR)/main.o $(OBJ_DIR)/save_format.o $(LDLIBS)

$(GUI_BIN): $(OBJ_DIR)/gui_main.o $(OBJ_DIR)/save_format.o $(IMGUI_OBJS) $(if $(GLFW_BUILT),$(GLFW_LIB),)
	$(CXX) $(CXXFLAGS) $(GUI_LDFLAGS) -o $@ $^ $(GLFW_LIBS) $(X11_LIBS) $(DL_LIB) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# glfw-sources is order-only: it checks the download without forcing a rebuild on every run.
$(OBJ_DIR)/gui_main.o: $(SRC_DIR)/gui_main.cpp $(HEADERS) | $(OBJ_DIR) $(if $(GLFW_BUILT),glfw-sources,)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(X11_CFLAGS) $(IMGUI_INC) -c -o $@ $<

$(OBJ_DIR)/%.o: $(IMGUI_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(IMGUI_INC) $(IMGUI_NOWARN) -c -o $@ $<

$(OBJ_DIR)/%.o: $(IMGUI_DIR)/backends/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(IMGUI_INC) $(IMGUI_NOWARN) -c -o $@ $<

$(GLFW_LIB): $(GLFW_OBJS)
	$(AR) rcs $@ $^

$(OBJ_DIR)/glfw/%.o: $(GLFW_DIR)/src/%.c | $(OBJ_DIR) glfw-sources
	@mkdir -p $(dir $@)
	$(CC) $(GLFW_CFLAGS) -c -o $@ $<

glfw-sources:
	@test -f $(GLFW_PROBE) || { \
		echo "GLFW $(GLFW_VERSION) sources are missing from $(GLFW_DIR). Check them out with:"; \
		echo "  git submodule update --init $(GLFW_DIR)"; exit 1; }

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Convenience target: build the Windows executables with the default triple.
.PHONY: windows
windows:
	@$(MAKE) CROSS=x86_64-w64-mingw32 all

# Build both Linux binaries with the C++ runtime linked in. The goals are listed explicitly (not
# `all`) so a missing GLFW submodule fails loudly instead of quietly shipping a CLI-only zip.
.PHONY: linux-static
linux-static:
	@$(MAKE) STATIC=1 $(BIN) $(GUI_BIN)

gui:
ifeq ($(GLFW_OK),yes)
gui: $(GUI_BIN)
else
	@echo "Missing libraries for the ImGui frontend."
ifeq ($(CROSS),)
	@echo "Install them with:"
	@echo "  apt-get install -y libglfw3-dev libx11-dev"
else
	@echo "Check out the GLFW submodule with:"
	@echo "  git submodule update --init $(GLFW_DIR)"
endif
	@exit 1
endif

test: $(BIN)
	sh tests/parity.sh

info: $(BIN)
	./$(BIN) --info cking.sav

help: $(BIN)
	./$(BIN) --help

# libs/glfw is a submodule and survives clean; drop the build dirs and binaries only.
clean:
	rm -rf build build-static build-*mingw* windwaker-hd-editor windwaker-hd-editor-cli \
	       windwaker-hd-editor.exe windwaker-hd-editor-cli.exe
