// gui_main.cpp - Dear ImGui frontend for the cking.sav editor (ImGui 1.93 + GLFW + OpenGL3).
#include "save_format.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>

#ifndef GLFW_INCLUDE_NONE  // the Makefile sets this too: glfw3.h must not include <GL/gl.h>, the
#define GLFW_INCLUDE_NONE  // GL entry points come from the loader vendored with the ImGui backend
#endif
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <backends/imgui_impl_opengl3_loader.h>  // glClear/glViewport/gl3wInit, dlopened at runtime
#endif
#if !defined(__APPLE__) && !defined(_WIN32)
#include <X11/Xlib.h>
#include <X11/Xatom.h>      // XA_RESOURCE_MANAGER
#include <X11/Xresource.h>  // Xft.dpi lookup: the Xrm* resource-database functions
#define WWHD_HAVE_X11 1
#endif

namespace {

const int k_max_magic[] = {0, 16, 32};
const float k_window_width = 1120.0f;
const float k_window_height = 760.0f;

// UI scale: the 13px default is tiny on a HiDPI panel and XWayland does not scale us at all, so
// start from the desktop factor and let the toolbar override it.
const float k_ui_scales[] = { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f };
const char *k_ui_scale_labels[] = { "100%", "125%", "150%", "175%", "200%", "250%" };
const int k_ui_scale_count = (int)(sizeof(k_ui_scales) / sizeof(k_ui_scales[0]));
const float k_auto_scale_max = 1.5f;  // detected scaling is capped; the combo and env are not

// Combo options for one field; unknown stored bytes get a "keep" entry so they survive.
struct Choices {
    std::vector<int> values;
    std::vector<std::string> labels;
    std::vector<const char*> items;

    void rebuild(const WwField *field, int current);
    int index_of(int value) const;
    int value_at(int index) const;
};

void Choices::rebuild(const WwField *field, int current)
{
    values.clear();
    labels.clear();
    items.clear();

    if (!ww_value_known(field, current)) {
        char text[48];
        snprintf(text, sizeof(text), "Keep unknown 0x%02X", current & 0xFF);
        values.push_back(current);
        labels.push_back(text);
    }
    for (int i = 0; i < field->count; i++) {
        values.push_back(field->values[i].value);
        labels.push_back(field->values[i].label);
    }
    for (size_t i = 0; i < labels.size(); i++) {
        items.push_back(labels[i].c_str());
    }
}

int Choices::index_of(int value) const
{
    for (size_t i = 0; i < values.size(); i++) {
        if (values[i] == value) {
            return (int)i;
        }
    }
    return 0;
}

int Choices::value_at(int index) const
{
    if (index < 0 || index >= (int)values.size()) {
        return values.empty() ? 0xFF : values[0];
    }
    return values[(size_t)index];
}

// Widget state for one quest log; heart values are decimal, stored as quarter units.
struct UiSlot {
    char name[64];
    float max_hearts;
    float current_hearts;
    int rupees;
    int wallet_level;
    int current_magic;
    int arrows;
    int max_arrows;
    int bombs;
    int max_bombs;
    int inventory[WW_MAX_FIELDS];
    int equipment[WW_MAX_FIELDS];
};

GLFWwindow *g_window = NULL;  // set once the window exists, so widgets can close it
uint8_t g_original[WW_SAVE_SIZE];
uint8_t g_working[WW_SAVE_SIZE];
uint8_t g_preview[WW_SAVE_SIZE];
UiSlot g_ui[WW_SLOT_COUNT];
Choices g_inv_choices[WW_SLOT_COUNT][WW_MAX_FIELDS];
Choices g_eq_choices[WW_SLOT_COUNT][WW_MAX_FIELDS];
int g_max_magic_index[WW_SLOT_COUNT];
bool g_loaded = false;
bool g_preview_valid = false;
int g_tab = 0;
ImGuiStyle g_base_style;   // fresh style, re-scaled whenever the UI scale changes
float g_ui_scale = 1.0f;
const char *g_scale_source = "default";  // where the starting scale came from, for the log line
int g_scale_index = 0;
char g_path[512] = "cking.sav";
char g_out[512] = "";
char g_status[256] = "Load a cking.sav file to begin.";
bool g_status_bad = false;

const char *env_or_dash(const char *name)
{
    const char *value = getenv(name);

    return (value != NULL && value[0] != '\0') ? value : "-";
}

// Xft.dpi is the value a desktop publishes for X11 and XWayland clients: GNOME writes 96 times
// its text-scaling-factor there, and KDE and XFCE publish their scaling the same way. Returns 0
// when nothing was published, so the caller keeps the previous value.
float xft_scale()
{
#if defined(WWHD_HAVE_X11)
    if (getenv("DISPLAY") == NULL) {
        return 0.0f;  // XOpenDisplay would only complain on stderr
    }
    Display *display = XOpenDisplay(NULL);
    Atom type = None;
    int format = 0;
    unsigned long items = 0;
    unsigned long bytes_left = 0;
    unsigned char *data = NULL;
    float scale = 0.0f;

    // RESOURCE_MANAGER on the root window, read directly instead of through
    // XResourceManagerString() so there is no per-display caching in the way.
    if (display != NULL &&
        XGetWindowProperty(display, RootWindow(display, DefaultScreen(display)),
                           XA_RESOURCE_MANAGER, 0, (~0L), False, AnyPropertyType, &type, &format,
                           &items, &bytes_left, &data) == Success && data != NULL) {
        XrmInitialize();
        XrmDatabase database = XrmGetStringDatabase((const char *)data);

        if (database != NULL) {
            char *value_type = NULL;
            XrmValue value;

            if (XrmGetResource(database, "Xft.dpi", "Xft.Dpi", &value_type, &value) &&
                value.addr != NULL) {
                float dpi = (float)atof(value.addr);

                if (dpi > 0.0f) {
                    scale = dpi / 96.0f;  // 96 dpi is the unscaled baseline, so this is the factor
                }
            }
            XrmDestroyDatabase(database);
        }
        XFree(data);
    }
    if (display != NULL) {
        XCloseDisplay(display);
    }
    return scale;
#else
    return 0.0f;
#endif
}

// Scaling factor to start with: WWHD_GUI_SCALE wins, then the Qt/GDK variables, then Xft.dpi, and
// only the detected value is capped at k_auto_scale_max - an explicit request is taken as given.
float detect_ui_scale()
{
    static const char *const k_env_names[] = {"WWHD_GUI_SCALE", "QT_SCALE_FACTOR", "GDK_SCALE"};
    const char *text = NULL;
    const char *source = "default";

    for (int i = 0; i < 3 && text == NULL; i++) {
        text = getenv(k_env_names[i]);
        if (text != NULL) {
            source = k_env_names[i];
        }
    }
    float scale = (text != NULL) ? (float)atof(text) : 0.0f;

    if (scale <= 0.0f) {
        scale = xft_scale();
        if (scale > k_auto_scale_max) {
            scale = k_auto_scale_max;
        }
        if (scale > 0.0f) {
            source = "Xft.dpi";
        }
    }
    if (scale < 1.0f) {
        scale = 1.0f;
    }
    if (scale > k_ui_scales[k_ui_scale_count - 1]) {
        scale = k_ui_scales[k_ui_scale_count - 1];
    }
    g_scale_source = source;
    return scale;
}

int scale_index_for(float scale)
{
    int best = 0;

    for (int i = 0; i < k_ui_scale_count; i++) {
        if (fabsf(k_ui_scales[i] - scale) < fabsf(k_ui_scales[best] - scale)) {
            best = i;
        }
    }
    return best;
}

// Applies a scale to a fresh copy of the default style: FontScaleMain resizes the glyphs (the
// embedded default font is scalable, so they stay sharp) and ScaleAllSizes() the spacing, which
// rounds and must therefore never be applied twice.
void apply_ui_scale(float scale)
{
    ImGuiStyle &style = ImGui::GetStyle();

    style = g_base_style;
    style.FontScaleMain = scale;
    style.ScaleAllSizes(scale);
    g_ui_scale = scale;
}

#if defined(GLFW_PLATFORM_X11)
// The front end GLFW actually connected to; glfwGetPlatform only exists from GLFW 3.4.
const char *glfw_platform_name()
{
    switch (glfwGetPlatform())
    {
        case GLFW_PLATFORM_WAYLAND:
            return "wayland";
        case GLFW_PLATFORM_X11:
            return "x11";
        default:
            return "other";
    }
}
#endif

void set_status(bool bad, const char *fmt, ...)
{
    va_list args;

    g_status_bad = bad;
    va_start(args, fmt);
    vsnprintf(g_status, sizeof(g_status), fmt, args);
    va_end(args);
}

// Copies one quest log into the widget state and rebuilds every combo.
void ui_fill(int slot, const WwSlot &s)
{
    UiSlot &u = g_ui[slot];

    memset(&u, 0, sizeof(u));
    snprintf(u.name, sizeof(u.name), "%s", s.display_name);
    u.max_hearts = (float)s.max_quarters / 4.0f;
    u.current_hearts = (float)s.current_quarters / 4.0f;
    u.rupees = s.rupees;
    u.wallet_level = s.wallet_level < 0 || s.wallet_level > 2 ? 0 : s.wallet_level;
    u.current_magic = s.current_magic;
    u.arrows = s.arrows;
    u.max_arrows = s.max_arrows;
    u.bombs = s.bombs;
    u.max_bombs = s.max_bombs;

    int magic_index = 0;
    for (int i = 0; i < 3; i++) {
        if (k_max_magic[i] == s.max_magic) {
            magic_index = i;
        }
    }
    g_max_magic_index[slot] = magic_index;

    for (int i = 0; i < ww_inventory_count; i++) {
        g_inv_choices[slot][i].rebuild(&ww_inventory[i], s.inventory[i]);
        u.inventory[i] = g_inv_choices[slot][i].index_of(s.inventory[i]);
    }
    for (int i = 0; i < ww_equipment_count; i++) {
        g_eq_choices[slot][i].rebuild(&ww_equipment[i], s.equipment[i]);
        u.equipment[i] = g_eq_choices[slot][i].index_of(s.equipment[i]);
    }
}

void ui_from_working()
{
    for (int slot = 0; slot < WW_SLOT_COUNT; slot++) {
        WwSlot s;

        ww_read_slot(g_working, slot, &s);
        ui_fill(slot, s);
    }
}

// Blank quest logs so the full layout still renders, greyed, before a file is loaded; the first
// entry of every value table is its "none" option.
void ui_reset()
{
    for (int slot = 0; slot < WW_SLOT_COUNT; slot++) {
        WwSlot s;

        memset(&s, 0, sizeof(s));
        s.slot = slot;
        s.max_quarters = 12;
        s.current_quarters = 12;
        for (int i = 0; i < ww_inventory_count; i++) {
            s.inventory[i] = ww_inventory[i].values[0].value;
        }
        for (int i = 0; i < ww_equipment_count; i++) {
            s.equipment[i] = ww_equipment[i].values[0].value;
        }
        ui_fill(slot, s);
    }
}

bool load_file()
{
    char err[256];

    if (ww_load(g_path, g_original, err, sizeof(err)) != 0) {
        set_status(true, "%s", err);
        return false;
    }
    memcpy(g_working, g_original, WW_SAVE_SIZE);
    ui_from_working();
    g_loaded = true;
    ww_default_out_path(g_path, g_out, sizeof(g_out));
    set_status(false, "Loaded %s", g_path);
    return true;
}

// Pushes the widget state onto a copy of the working buffer; clamping happens in the core.
bool apply_into(uint8_t *bytes, char *err, size_t err_size)
{
    for (int slot = 0; slot < WW_SLOT_COUNT; slot++) {
        const UiSlot &u = g_ui[slot];
        WwEdits e;

        memset(&e, 0, sizeof(e));
        for (int i = 0; i < WW_MAX_FIELDS; i++) {
            e.inventory[i] = -1;
            e.equipment[i] = -1;
        }
        if (u.name[0] != '\0') {
            size_t name_length = strlen(u.name);

            if (name_length > WW_NAME_MAX) {
                snprintf(err, err_size, "Quest-log name is limited to %d characters.", WW_NAME_MAX);
                return false;
            }
            memcpy(e.name, u.name, name_length);
            e.name[name_length] = '\0';
            e.present |= WW_EDIT_NAME;
        }
        e.present |= WW_EDIT_MAX_HEARTS | WW_EDIT_CUR_HEARTS | WW_EDIT_RUPEES | WW_EDIT_WALLET
                   | WW_EDIT_MAX_MAGIC | WW_EDIT_CUR_MAGIC | WW_EDIT_ARROWS | WW_EDIT_MAX_ARROWS
                   | WW_EDIT_BOMBS | WW_EDIT_MAX_BOMBS | WW_EDIT_INVENTORY | WW_EDIT_EQUIPMENT;
        e.max_quarters = (int)lrintf(u.max_hearts * 4.0f);
        e.current_quarters = (int)lrintf(u.current_hearts * 4.0f);
        e.rupees = u.rupees;
        e.wallet_level = u.wallet_level;
        e.max_magic = k_max_magic[g_max_magic_index[slot]];
        e.current_magic = u.current_magic;
        e.arrows = u.arrows;
        e.max_arrows = u.max_arrows;
        e.bombs = u.bombs;
        e.max_bombs = u.max_bombs;
        for (int i = 0; i < ww_inventory_count; i++) {
            e.inventory[i] = g_inv_choices[slot][i].value_at(u.inventory[i]);
        }
        for (int i = 0; i < ww_equipment_count; i++) {
            e.equipment[i] = g_eq_choices[slot][i].value_at(u.equipment[i]);
        }
        if (ww_apply(bytes, slot, &e, err, err_size) != 0) {
            return false;
        }
    }
    return true;
}

// Pushes the widget state to a file; the caller picks the path.
bool save_to(const char *target)
{
    char err[256];
    uint8_t staged[WW_SAVE_SIZE];

    if (strcmp(target, g_path) == 0) {
        set_status(true, "Refusing to overwrite the input file; pick another Save as path.");
        return false;
    }
    memcpy(staged, g_working, WW_SAVE_SIZE);
    if (!apply_into(staged, err, sizeof(err))) {
        set_status(true, "%s", err);
        return false;
    }
    if (ww_write(target, staged, err, sizeof(err)) != 0) {
        set_status(true, "%s", err);
        return false;
    }
    memcpy(g_working, staged, WW_SAVE_SIZE);
    ui_from_working();
    set_status(false, "Wrote %s - keep your original backup until it loads in-game.", target);
    return true;
}

void save_file(void)
{
    char fallback[512];
    const char *target = g_out;

    if (target[0] == '\0') {
        ww_default_out_path(g_path, fallback, sizeof(fallback));
        target = fallback;
    }
    save_to(target);
}

// Keeps a toolbar item on the current line, or lets it wrap when the UI scale made the row too
// wide for the window.
void toolbar_same_line(const char *label)
{
    const ImGuiStyle &style = ImGui::GetStyle();
    float next_x = ImGui::GetItemRectMax().x + style.ItemSpacing.x;  // where SameLine would land
    float item_width = ImGui::CalcTextSize(label).x + 2.0f * style.FramePadding.x;

    if (next_x + item_width <= ImGui::GetWindowWidth() - style.WindowPadding.x) {
        ImGui::SameLine();
    }
}

void draw_fields(int slot)
{
    UiSlot &u = g_ui[slot];
    WwSlot stored;
    char max_hearts[16];
    char current_hearts[16];
    const float label_gap = 175.0f * g_ui_scale;  // room for the label drawn after each field

    if (g_loaded) {
        ww_read_slot(g_working, slot, &stored);
        ww_format_hearts(max_hearts, sizeof(max_hearts), stored.max_quarters);
        ww_format_hearts(current_hearts, sizeof(current_hearts), stored.current_quarters);

        ImGui::Text("Stored: %s / %s hearts, %d / %d rupees", current_hearts, max_hearts,
                    stored.rupees, stored.wallet_capacity);
        if (stored.checksum_valid) {
            ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Checksum ok");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "Checksum needs repair");
        }
    } else {
        memset(&stored, 0, sizeof(stored));  // nothing stored yet, so no masks to show either
        ImGui::Text("Stored: nothing loaded yet");
    }
    ImGui::Separator();

    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::InputText("Name", u.name, sizeof(u.name));
    if (u.name[0] == '\0') {
        ImGui::TextDisabled("(empty name keeps the stored value)");
    }

    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::DragFloat("Max hearts", &u.max_hearts, 0.25f, 0.25f, 20.0f, "%.2f");
    u.max_hearts = rintf(u.max_hearts * 4.0f) / 4.0f;
    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::DragFloat("Current hearts", &u.current_hearts, 0.25f, 0.25f, 20.0f, "%.2f");
    u.current_hearts = rintf(u.current_hearts * 4.0f) / 4.0f;

    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::InputInt("Rupees", &u.rupees);
    const char *wallets[3] = {"200", "1000", "5000"};
    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::Combo("Wallet", &u.wallet_level, wallets, 3);

    const char *magics[3] = {"0", "16", "32"};
    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::Combo("Max magic", &g_max_magic_index[slot], magics, 3);
    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::InputInt("Current magic", &u.current_magic);

    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::InputInt("Arrows", &u.arrows);
    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::InputInt("Max arrows", &u.max_arrows);
    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::InputInt("Bombs##count", &u.bombs);  // ## keeps the ID distinct from the Bombs inventory combo
    ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
    ImGui::InputInt("Max bombs", &u.max_bombs);

    ImGui::Separator();
    ImGui::TextUnformatted("Equipment");
    for (int i = 0; i < ww_equipment_count; i++) {
        ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
        ImGui::Combo(ww_equipment[i].label, &u.equipment[i],
                     g_eq_choices[slot][i].items.data(), (int)g_eq_choices[slot][i].items.size());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Inventory");
    for (int i = 0; i < ww_inventory_count; i++) {
        ImGui::SetNextItemWidth(-label_gap);  // leave room for the label
        ImGui::Combo(ww_inventory[i].label, &u.inventory[i],
                     g_inv_choices[slot][i].items.data(), (int)g_inv_choices[slot][i].items.size());
        if (g_loaded && ww_inventory[i].count > 2 && stored.inventory_get[i] >= 0) {
            ImGui::TextDisabled("stored obtained mask 0x%02X", stored.inventory_get[i]);
        }
    }
}

void draw_changes()
{
    char *text = NULL;
    size_t length = 0;
    FILE *stream = open_memstream(&text, &length);

    if (stream) {
        ww_print_diff(stream, g_original, g_preview);
        fclose(stream);
        ImGui::TextUnformatted(text, text + length);
        free(text);
    }
}

void draw_ui()
{
    // Fill the parent window: the main viewport is the GLFW client area, so this tracks resizes.
    ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
    ImGui::Begin("Wind Waker HD save editor", NULL, flags);

    float path_width = 360.0f * g_ui_scale;  // the path rows never take more than half the window

    if (path_width > ImGui::GetWindowWidth() * 0.5f) {
        path_width = ImGui::GetWindowWidth() * 0.5f;
    }
    ImGui::SetNextItemWidth(path_width);
    ImGui::InputText("File", g_path, sizeof(g_path));
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        load_file();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(84.0f * g_ui_scale);
    if (ImGui::Combo("Scale", &g_scale_index, k_ui_scale_labels, k_ui_scale_count)) {
        apply_ui_scale(k_ui_scales[g_scale_index]);
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 46.0f * g_ui_scale);  // quit in the top-right corner
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 0.60f));      // destructive
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.86f, 0.28f, 0.28f, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.00f, 0.36f, 0.36f, 0.95f));
    if (ImGui::Button("Quit")) {
        glfwSetWindowShouldClose(g_window, GLFW_TRUE);
    }
    ImGui::PopStyleColor(3);

    ImGui::BeginDisabled(!g_loaded);  // the save row greys out until a file is open
    ImGui::SetNextItemWidth(360.0f * g_ui_scale);
    ImGui::SetNextItemWidth(path_width);
    ImGui::InputText("Save as", g_out, sizeof(g_out));
    toolbar_same_line("Save modified copy");
    if (ImGui::Button("Save modified copy")) {
        save_file();
    }
    toolbar_same_line("Repair all checksums");
    if (ImGui::Button("Repair all checksums")) {
        ww_repair_all(g_working);
        set_status(false, "All three quest-log checksums were recalculated; save to write them.");
    }
    toolbar_same_line("Discard edits");
    if (ImGui::Button("Discard edits")) {
        memcpy(g_working, g_original, WW_SAVE_SIZE);
        ui_from_working();
        set_status(false, "Discarded; the working copy matches the loaded file.");
    }
    ImGui::EndDisabled();

    if (g_status_bad) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", g_status);
    } else {
        ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "%s", g_status);
    }

    // Rebuild the preview every frame so the change list always matches what a save would write.
    char err[256] = "";
    if (g_loaded) {
        memcpy(g_preview, g_working, WW_SAVE_SIZE);
        g_preview_valid = apply_into(g_preview, err, sizeof(err));
    }

    // The edit area is always laid out, greyed out while no save is loaded.
    ImGui::BeginDisabled(!g_loaded);
    if (ImGui::BeginTabBar("QuestLogs")) {
        for (int slot = 0; slot < WW_SLOT_COUNT; slot++) {
            WwSlot s;
            char label[64];

            ww_read_slot(g_working, slot, &s);
            snprintf(label, sizeof(label), "Quest Log %d: %s##tab%d", slot + 1,
                     s.display_name[0] ? s.display_name : "Empty", slot);
            if (ImGui::BeginTabItem(label)) {
                g_tab = slot;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();

        ImGui::BeginChild("Fields", ImVec2(430.0f * g_ui_scale, 0), ImGuiChildFlags_Borders);
        draw_fields(g_tab);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("Changes", ImVec2(0, 0), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted("Pending changes");
        ImGui::Separator();
        if (!g_loaded) {
            ImGui::Text("Load a save file to compare against it.");
        } else {
            if (!g_preview_valid) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", err);
            }
            draw_changes();
        }
        ImGui::EndChild();
    }
    ImGui::EndDisabled();

    ImGui::End();
}

}  // namespace

int main(int argc, char **argv)
{
    if (argc > 1) {
        snprintf(g_path, sizeof(g_path), "%s", argv[1]);
    }

    // A Wayland surface has no client-side decorations, so GLFW's Wayland front end comes up
    // without a title bar. Ask for X11/XWayland, where the window manager decorates the window;
    // GLFW picks Wayland by itself whenever XDG_SESSION_TYPE=wayland, and it only honours this
    // hint through glfwInitHint (there is no GLFW_PLATFORM environment variable).
    const char *platform = "x11";
#if defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
    if (strcmp(env_or_dash("WWHD_GUI_PLATFORM"), "wayland") == 0) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
        platform = "wayland";
    } else {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
#else
    platform = "any";  // GLFW 3.3 has no Wayland front end on Linux
#endif

    bool ok = glfwInit();
#if defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
    // No X11 front end at all (no XWayland): fall back to GLFW's own pick so the app still starts.
    if (!ok && strcmp(platform, "x11") == 0) {
        glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
        platform = "any";
        ok = glfwInit();
    }
#endif
    if (!ok) {
        fprintf(stderr, "Could not initialise GLFW; an X11 or Wayland display is required.\n");
        return 1;
    }
    // Diagnostics: the AppImage packaging is judged by these two lines.
    fprintf(stderr, "GLFW %s (requested %s; XDG_SESSION_TYPE=%s DISPLAY=%s WAYLAND_DISPLAY=%s)\n",
            glfwGetVersionString(), platform, env_or_dash("XDG_SESSION_TYPE"),
            env_or_dash("DISPLAY"), env_or_dash("WAYLAND_DISPLAY"));
#if defined(GLFW_PLATFORM_X11)
    fprintf(stderr, "GLFW chose %s\n", glfw_platform_name());
#endif
    // Needs the connected display for Xft.dpi, so it runs after glfwInit and before the window.
    g_ui_scale = detect_ui_scale();
    fprintf(stderr, "UI scale %.2f (%s)\n", g_ui_scale, g_scale_source);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);  // the ImGui window is borderless and fills the
                                                // client area, so the title bar is the WM's job
    glfwWindowHintString(GLFW_X11_CLASS_NAME, "wwhd-editor");    // stable WM_CLASS: inside an
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, "wwhd-editor"); // AppImage argv[0] is a mount path
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    int window_width = (int)(k_window_width * g_ui_scale);
    int window_height = (int)(k_window_height * g_ui_scale);
    const GLFWvidmode *mode = glfwGetVideoMode(glfwGetPrimaryMonitor());

    if (mode != NULL) {  // a scaled window must not end up larger than the screen
        if (window_width > mode->width) {
            window_width = mode->width;
        }
        if (window_height > mode->height) {
            window_height = mode->height;
        }
    }
    GLFWwindow *window = glfwCreateWindow(window_width, window_height,
                                          "Wind Waker HD Save Editor", NULL, NULL);
    if (!window) {
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_ANY_PROFILE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 1);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
        window = glfwCreateWindow(window_width, window_height,
                                  "Wind Waker HD Save Editor", NULL, NULL);
    }
    if (!window) {
        fprintf(stderr, "Could not create an OpenGL context.\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    g_window = window;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = NULL;  // no imgui.ini in whatever directory the tool was started from
    ImGui::StyleColorsDark();
    g_base_style = ImGui::GetStyle();
    g_scale_index = scale_index_for(g_ui_scale);
    apply_ui_scale(k_ui_scales[g_scale_index]);
    glfwShowWindow(window);
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    if (!ImGui_ImplOpenGL3_Init("#version 150")) {
        fprintf(stderr, "Could not initialise the ImGui OpenGL3 backend.\n");
        return 1;
    }

    ui_reset();  // blank widget state, so the editor is fully drawn even with no file loaded
    if (argc > 1) {
        load_file();
    }

    // WWHD_GUI_FRAMES renders a fixed number of frames and quits; used for headless smoke tests.
    const char *frame_limit_text = getenv("WWHD_GUI_FRAMES");
    int frame_limit = frame_limit_text ? atoi(frame_limit_text) : 0;
    // WWHD_GUI_AUTOSAVE writes the widget state to a file on the second frame, so the headless
    // test can compare what the GUI would save against what the command line writes.
    const char *autosave_path = getenv("WWHD_GUI_AUTOSAVE");
    int frames = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        draw_ui();
        ImGui::Render();  // must run before GetDrawData()

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        glViewport(0, 0, width, height);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);

        if (autosave_path && frames == 1 && g_loaded) {
            printf("autosave %s: %s\n", autosave_path, save_to(autosave_path) ? "ok" : "failed");
        }
        frames++;
        if (frame_limit > 0 && frames >= frame_limit) {
            printf("Rendered %d frame(s) at %dx%d; saved=%d\n", frames, width, height,
                   g_loaded ? 1 : 0);
            break;
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
