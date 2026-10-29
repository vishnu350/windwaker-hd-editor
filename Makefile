CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wpedantic -std=c99
CXX     ?= g++
CXXFLAGS ?= -O2 -Wall -Wextra -std=c++11
CPPFLAGS += -D_POSIX_C_SOURCE=200809L
LDLIBS  ?= -lm

SRC_DIR := src
OBJ_DIR := build
BIN     := windwaker-hd-editor-cli
GUI_BIN := windwaker-hd-editor
HEADERS := src/save_format.h

# Dear ImGui is a git submodule in imgui/, compiled into the GUI, per the upstream guide.
IMGUI_DIR    := imgui
IMGUI_CXX    := $(IMGUI_DIR)/imgui.cpp $(IMGUI_DIR)/imgui_draw.cpp $(IMGUI_DIR)/imgui_tables.cpp \
                $(IMGUI_DIR)/imgui_widgets.cpp $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
                $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp
IMGUI_OBJS   := $(notdir $(IMGUI_CXX:.cpp=.o))
IMGUI_OBJS   := $(addprefix $(OBJ_DIR)/,$(IMGUI_OBJS))
IMGUI_INC    := -I$(IMGUI_DIR) -I$(IMGUI_DIR)/backends

# GLFW_INCLUDE_NONE stops glfw3.h from including <GL/gl.h>; the OpenGL entry points come from the
# loader vendored with imgui_impl_opengl3, which dlopens the host driver at runtime.
GLFW_NO_GL   := -DGLFW_INCLUDE_NONE

# The GUI needs GLFW and X11 headers only - OpenGL entry points come from the loader vendored with
# the ImGui backend, which dlopens the host driver at runtime, so nothing links or builds against
# Mesa. X11 is read for the Xft.dpi scale and is a GLFW dependency anyway.
GLFW_OK := $(shell pkg-config --exists glfw3 x11 2>/dev/null && echo yes)
GLFW_LIBS := $(shell pkg-config --libs glfw3 2>/dev/null)
X11_CFLAGS := $(shell pkg-config --cflags x11 2>/dev/null)
X11_LIBS := $(shell pkg-config --libs x11 2>/dev/null)

all: $(BIN)
ifeq ($(GLFW_OK),yes)
all: $(GUI_BIN)
endif

$(BIN): $(OBJ_DIR)/main.o $(OBJ_DIR)/save_format.o
	$(CC) $(CFLAGS) -o $@ $(OBJ_DIR)/main.o $(OBJ_DIR)/save_format.o $(LDLIBS)

$(GUI_BIN): $(OBJ_DIR)/gui_main.o $(OBJ_DIR)/save_format.o $(IMGUI_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(GLFW_LIBS) $(X11_LIBS) -ldl $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c $(HEADERS) | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<

$(OBJ_DIR)/gui_main.o: $(SRC_DIR)/gui_main.cpp $(HEADERS) | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(X11_CFLAGS) $(IMGUI_INC) -c -o $@ $<

$(OBJ_DIR)/%.o: $(IMGUI_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(IMGUI_INC) -c -o $@ $<

$(OBJ_DIR)/%.o: $(IMGUI_DIR)/backends/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(GLFW_NO_GL) $(IMGUI_INC) -c -o $@ $<

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

gui:
ifeq ($(GLFW_OK),yes)
gui: $(GUI_BIN)
else
	@echo "Missing libraries for the ImGui frontend. Install them with:"
	@echo "  apt-get install -y libglfw3-dev libx11-dev"
	@exit 1
endif

test: $(BIN)
	sh tests/parity.sh

info: $(BIN)
	./$(BIN) --info cking.sav

help: $(BIN)
	./$(BIN) --help

clean:
	rm -rf $(OBJ_DIR) $(BIN) $(GUI_BIN)
