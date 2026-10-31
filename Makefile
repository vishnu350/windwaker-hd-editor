# CROSS=<triple> cross-compiles Windows binaries with MinGW-w64, e.g. CROSS=x86_64-w64-mingw32.
# Leave it empty for a native Linux build; `make windows` is the shortcut for the 64-bit triple.
CROSS   ?=

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
OBJ_DIR := build$(if $(CROSS),-$(CROSS))
BIN     := windwaker-hd-editor-cli$(EXE)
GUI_BIN := windwaker-hd-editor$(EXE)
HEADERS := src/save_format.h

# ---- Windows cross build: GLFW compiled from source and linked statically -----------------
# GLFW is a git submodule pinned to 3.4 (commit 7b6aead); the native build keeps using the distro
# package. The file list mirrors glfw-3.4/src/CMakeLists.txt for a Win32-only build: the shared
# sources, the always-built null backend (platform.c links against it), the win32 front end and
# wgl_context.c. egl/osmesa compile to nothing without _GLFW_EGL/_GLFW_OSMESA.
GLFW_VERSION := 3.4
GLFW_DIR     := libs/glfw
GLFW_LIB     := $(OBJ_DIR)/libglfw3.a
GLFW_SRCS    := context init input monitor platform vulkan window egl_context osmesa_context \
                null_init null_monitor null_window null_joystick \
                win32_init win32_module win32_monitor win32_time win32_thread win32_window \
                wgl_context win32_joystick
GLFW_OBJS    := $(addprefix $(OBJ_DIR)/glfw/,$(addsuffix .o,$(GLFW_SRCS)))
GLFW_CFLAGS  := -O2 -Wall -D_GLFW_WIN32 -D_CRT_SECURE_NO_WARNINGS \
                -I$(GLFW_DIR)/include -I$(GLFW_DIR)/src

# Dear ImGui is a git submodule in imgui/, compiled into the GUI, per the upstream guide.
IMGUI_DIR    := imgui
IMGUI_CXX    := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp \
                $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
                $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS   := $(notdir $(IMGUI_CXX:.cpp=.o))
IMGUI_OBJS   := $(addprefix $(OBJ_DIR)/,$(IMGUI_OBJS))
IMGUI_INC    := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends $(if $(CROSS),-I$(GLFW_DIR)/include,)
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
	$(CC) $(CFLAGS) -o $@ $(OBJ_DIR)/main.o $(OBJ_DIR)/save_format.o $(LDLIBS)

$(GUI_BIN): $(OBJ_DIR)/gui_main.o $(OBJ_DIR)/save_format.o $(IMGUI_OBJS) $(if $(CROSS),$(GLFW_LIB),)
	$(CXX) $(CXXFLAGS) $(GUI_LDFLAGS) -o $@ $^ $(GLFW_LIBS) $(X11_LIBS) $(DL_LIB) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

# glfw-sources is order-only: it checks the download without forcing a rebuild on every run.
$(OBJ_DIR)/gui_main.o: $(SRC_DIR)/gui_main.cpp $(HEADERS) | $(OBJ_DIR) $(if $(CROSS),glfw-sources,)
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
	@test -f $(GLFW_DIR)/src/win32_init.c || { \
		echo "GLFW $(GLFW_VERSION) sources are missing from $(GLFW_DIR). Check them out with:"; \
		echo "  git submodule update --init $(GLFW_DIR)"; exit 1; }

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

# Convenience target: build the Windows executables with the default triple.
.PHONY: windows
windows:
	@$(MAKE) CROSS=x86_64-w64-mingw32 all

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
	rm -rf build build-*mingw* windwaker-hd-editor windwaker-hd-editor-cli \
	       windwaker-hd-editor.exe windwaker-hd-editor-cli.exe
