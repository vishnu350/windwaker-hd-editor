/* save_format.c - field access, validation, normalization and checksums. */
#include "save_format.h"

#include <ctype.h>
#include <string.h>

#define NONE 0xFF /* "no item" marker used by most inventory slots */

static const int display_name_offsets[WW_SLOT_COUNT] = {0x2035, 0x2047, 0x2059};
static const int internal_name_offsets[3] = {0x0157, 0x0168, 0x0179};

static const WwValue v_telescope[] = {{NONE, "None"}, {0x20, "Telescope"}};
static const WwValue v_sail[] = {{NONE, "None"}, {0x78, "Normal Sail"}, {0x77, "Swift Sail"}};
static const WwValue v_wind_waker[] = {{NONE, "None"}, {0x22, "Wind Waker"}};
static const WwValue v_hook[] = {{NONE, "None"}, {0x25, "Grappling Hook"}};
static const WwValue v_spoils[] = {{NONE, "None"}, {0x24, "Spoils Bag"}};
static const WwValue v_boomerang[] = {{NONE, "None"}, {0x2D, "Boomerang"}};
static const WwValue v_deku_leaf[] = {{NONE, "None"}, {0x34, "Deku Leaf"}};
static const WwValue v_tingle[] = {{NONE, "None"}, {0x21, "Tingle Bottle"}};
static const WwValue v_pictobox[] = {{NONE, "None"}, {0x23, "Picto Box"}, {0x26, "Deluxe Picto Box"}};
static const WwValue v_iron_boots[] = {{NONE, "None"}, {0x29, "Iron Boots"}};
static const WwValue v_magic_armor[] = {{NONE, "None"}, {0x2A, "Magic Armor"}};
static const WwValue v_bait_bag[] = {{NONE, "None"}, {0x2C, "Bait Bag"}};
static const WwValue v_bow[] = {{NONE, "None"}, {0x27, "Hero's Bow"},
                                {0x35, "Fire & Ice Arrows"}, {0x36, "Light Arrows"}};
static const WwValue v_bombs[] = {{NONE, "None"}, {0x31, "Bombs"}};
static const WwValue v_delivery[] = {{NONE, "None"}, {0x30, "Delivery Bag"}};
static const WwValue v_hookshot[] = {{NONE, "None"}, {0x2F, "Hookshot"}};
static const WwValue v_skull_hammer[] = {{NONE, "None"}, {0x33, "Skull Hammer"}};
static const WwValue v_bottle[] = {{NONE, "No bottle"}, {0x50, "Empty Bottle"},
                                   {0x51, "Red Potion"}, {0x52, "Green Potion"},
                                   {0x53, "Blue Potion"}, {0x54, "Elixir Soup - half"},
                                   {0x55, "Elixir Soup"}, {0x56, "Water"},
                                   {0x57, "Fairy"}, {0x58, "Forest Firefly"},
                                   {0x59, "Forest Water"}};
static const WwValue v_sword[] = {{NONE, "None"}, {0x38, "Hero's Sword"},
                                  {0x39, "Master Sword - powerless"},
                                  {0x3A, "Master Sword - half restored"},
                                  {0x3E, "Master Sword - fully restored"}};
static const WwValue v_shield[] = {{NONE, "None"}, {0x3B, "Hero's Shield"}, {0x3C, "Mirror Shield"}};
static const WwValue v_bracelets[] = {{NONE, "None"}, {0x28, "Power Bracelets"}};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* Item slots live at 0x3C + n; the obtained mask lives at 0x51 + n and acts as a floor: the game
 * raises the item byte to the best variant the mask claims, and never removes an item because of
 * it (measured in-game). So a removal or a downgrade has to lower the mask, or the next in-game
 * save undoes the edit.
 *   get_offset 0        -> mask not used by that slot (bottles hold content with a zero mask)
 *   variant ladder      -> mask is rewritten as (1 << variants) - 1, so downgrades clear top bits
 *   single item slot    -> only bit 0 is touched, because other bits carry unrelated data (the
                           telescope mask is 0xA5 in a normal save) */
const WwField ww_inventory[WW_MAX_FIELDS] = {
    {"telescope",     "Telescope",      0x3C, 0x51, v_telescope,   COUNT(v_telescope)},
    {"sail",          "Sail",           0x3D, 0x52, v_sail,        COUNT(v_sail)},
    {"windWaker",     "Wind Waker",     0x3E, 0x53, v_wind_waker,  COUNT(v_wind_waker)},
    {"grapplingHook", "Grappling Hook", 0x3F, 0x54, v_hook,        COUNT(v_hook)},
    {"spoilsBag",     "Spoils Bag",     0x40, 0x55, v_spoils,      COUNT(v_spoils)},
    {"boomerang",     "Boomerang",      0x41, 0x56, v_boomerang,   COUNT(v_boomerang)},
    {"dekuLeaf",      "Deku Leaf",      0x42, 0x57, v_deku_leaf,   COUNT(v_deku_leaf)},
    {"tingleBottle",  "Tingle Bottle",  0x43, 0x58, v_tingle,      COUNT(v_tingle)},
    {"pictobox",      "Picto Box",      0x44, 0x59, v_pictobox,    COUNT(v_pictobox)},
    {"ironBoots",     "Iron Boots",     0x45, 0x5A, v_iron_boots,  COUNT(v_iron_boots)},
    {"magicArmor",    "Magic Armor",    0x46, 0x5B, v_magic_armor, COUNT(v_magic_armor)},
    {"baitBag",       "Bait Bag",       0x47, 0x5C, v_bait_bag,    COUNT(v_bait_bag)},
    {"bow",           "Bow",            0x48, 0x5D, v_bow,         COUNT(v_bow)},
    {"bombs",         "Bombs",          0x49, 0x5E, v_bombs,       COUNT(v_bombs)},
    {"bottle1",       "Bottle 1",       0x4A, 0x00, v_bottle,      COUNT(v_bottle)},
    {"bottle2",       "Bottle 2",       0x4B, 0x00, v_bottle,      COUNT(v_bottle)},
    {"bottle3",       "Bottle 3",       0x4C, 0x00, v_bottle,      COUNT(v_bottle)},
    {"bottle4",       "Bottle 4",       0x4D, 0x00, v_bottle,      COUNT(v_bottle)},
    {"deliveryBag",   "Delivery Bag",   0x4E, 0x63, v_delivery,    COUNT(v_delivery)},
    {"hookshot",      "Hookshot",       0x4F, 0x64, v_hookshot,    COUNT(v_hookshot)},
    {"skullHammer",   "Skull Hammer",   0x50, 0x65, v_skull_hammer, COUNT(v_skull_hammer)},
};
const int ww_inventory_count = 21;

const WwField ww_equipment[WW_MAX_FIELDS] = {
    {"sword",     "Sword",           0x0E, 0x00, v_sword,     COUNT(v_sword)},
    {"shield",    "Shield",          0x0F, 0x00, v_shield,    COUNT(v_shield)},
    {"bracelets", "Power Bracelets", 0x10, 0x00, v_bracelets, COUNT(v_bracelets)},
};
const int ww_equipment_count = 3;

static int read_u16be(const uint8_t *b, size_t at)
{
    return (b[at] << 8) | b[at + 1];
}

static void write_u16be(uint8_t *b, size_t at, int value)
{
    b[at] = (uint8_t)((value >> 8) & 0xFF);
    b[at + 1] = (uint8_t)(value & 0xFF);
}

static uint32_t read_u32be(const uint8_t *b, size_t at)
{
    return ((uint32_t)b[at] << 24) | ((uint32_t)b[at + 1] << 16)
         | ((uint32_t)b[at + 2] << 8) | (uint32_t)b[at + 3];
}

static void write_u32be(uint8_t *b, size_t at, uint32_t value)
{
    b[at] = (uint8_t)(value >> 24);
    b[at + 1] = (uint8_t)(value >> 16);
    b[at + 2] = (uint8_t)(value >> 8);
    b[at + 3] = (uint8_t)value;
}

static void fail(char *err, size_t size, const char *message)
{
    if (err && size) {
        snprintf(err, size, "%s", message);
    }
}

int ww_wallet_capacity(int level)
{
    static const int capacities[3] = {200, 1000, 5000};
    return (level >= 0 && level <= 2) ? capacities[level] : 200;
}

void ww_default_out_path(const char *in_path, char *buf, size_t size)
{
    const char *slash = strrchr(in_path, '/');
    const char *stem = slash ? slash + 1 : in_path;
    const char *dot = strrchr(stem, '.');
    size_t stem_len = dot ? (size_t)(dot - stem) : strlen(stem);
    size_t head = (size_t)(stem - in_path);

    if (head + stem_len + 20 >= size) {
        snprintf(buf, size, "modified.sav");
        return;
    }
    memcpy(buf, in_path, head);
    memcpy(buf + head, stem, stem_len);
    snprintf(buf + head + stem_len, size - head - stem_len, "-modified.sav");
}

/* Quarter-heart units render as 7 or 3.75, matching the web editor. */
void ww_format_hearts(char *buf, size_t size, int quarters)
{
    static const char *fractions[4] = {"", ".25", ".5", ".75"};

    snprintf(buf, size, "%d%s", quarters / 4, fractions[quarters % 4]);
}

/* Key lookup ignores case and underscores, so windWaker and wind_waker match. */
const WwField *ww_find_field(const WwField *fields, int count, const char *key)
{
    int i;

    for (i = 0; i < count; i++) {
        const char *a = key;
        const char *b = fields[i].key;
        int matched = 1;

        while (*a && *b) {
            char ca = (char)toupper((unsigned char)*a);
            char cb = (char)toupper((unsigned char)*b);
            if (ca == '_') {
                a++;
                continue;
            }
            if (cb == '_') {
                b++;
                continue;
            }
            if (ca != cb) {
                matched = 0;
                break;
            }
            a++;
            b++;
        }
        while (*a == '_') {
            a++;
        }
        while (*b == '_') {
            b++;
        }
        if (matched && *a == '\0' && *b == '\0') {
            return &fields[i];
        }
    }
    return NULL;
}

int ww_value_known(const WwField *field, int value)
{
    int i;

    for (i = 0; i < field->count; i++) {
        if (field->values[i].value == value) {
            return 1;
        }
    }
    return 0;
}

const char *ww_value_label(const WwField *field, int value)
{
    static char unknown[24];
    int i;

    for (i = 0; i < field->count; i++) {
        if (field->values[i].value == value) {
            return field->values[i].label;
        }
    }
    snprintf(unknown, sizeof(unknown), "Unknown 0x%02X", value & 0xFF);
    return unknown;
}

/* Position of a value in the table: 0 = none, 1 = base item, 2 = second variant, and so on. */
static int variant_index(const WwField *field, int value)
{
    int i;

    for (i = 0; i < field->count; i++) {
        if (field->values[i].value == value) {
            return i;
        }
    }
    return -1;
}

/* Owning variant n means every variant at or below it is owned: none 0, base 1, swift 3, light 7. */
static int variant_mask(int index)
{
    return index <= 0 ? 0 : (1 << index) - 1;
}

/* Slot checksum: running sum of the slot body plus the sum of its complement. */
static void slot_checksum(const uint8_t *bytes, int slot, uint32_t *sum, uint32_t *inverse)
{
    size_t base = (size_t)slot * WW_SLOT_SIZE;
    int i;

    *sum = 0;
    *inverse = 0;
    for (i = 0; i < WW_CHECKSUM_OFFSET; i++) {
        uint32_t value = bytes[base + (size_t)i];
        *sum += value;
        *inverse += ~value;
    }
}

int ww_checksum_valid(const uint8_t *bytes, int slot)
{
    uint32_t sum;
    uint32_t inverse;
    size_t base = (size_t)slot * WW_SLOT_SIZE;

    slot_checksum(bytes, slot, &sum, &inverse);
    return read_u32be(bytes, base + WW_CHECKSUM_OFFSET) == sum
        && read_u32be(bytes, base + WW_CHECKSUM_OFFSET + 4) == inverse;
}

void ww_update_checksum(uint8_t *bytes, int slot)
{
    uint32_t sum;
    uint32_t inverse;
    size_t base = (size_t)slot * WW_SLOT_SIZE;

    slot_checksum(bytes, slot, &sum, &inverse);
    write_u32be(bytes, base + WW_CHECKSUM_OFFSET, sum);
    write_u32be(bytes, base + WW_CHECKSUM_OFFSET + 4, inverse);
}

void ww_repair_all(uint8_t *bytes)
{
    int i;

    for (i = 0; i < WW_SLOT_COUNT; i++) {
        ww_update_checksum(bytes, i);
    }
}

static void read_display_name(const uint8_t *bytes, int slot, char *out, size_t size)
{
    size_t offset = (size_t)display_name_offsets[slot];
    size_t used = 0;
    int i;

    for (i = 0; i < 9; i++) {
        int code = bytes[offset + (size_t)i * 2] | (bytes[offset + (size_t)i * 2 + 1] << 8);
        if (code == 0 || used + 1 >= size) {
            break;
        }
        out[used++] = (char)code;
    }
    out[used] = '\0';
}

static void read_ascii(const uint8_t *bytes, size_t offset, size_t span, char *out, size_t size)
{
    size_t used = 0;
    size_t i;

    for (i = 0; i < span && used + 1 < size; i++) {
        if (bytes[offset + i] == 0) {
            break;
        }
        out[used++] = (char)bytes[offset + i];
    }
    out[used] = '\0';
}

void ww_read_slot(const uint8_t *bytes, int slot, WwSlot *out)
{
    size_t base = (size_t)slot * WW_SLOT_SIZE;
    int i;

    memset(out, 0, sizeof(*out));
    out->slot = slot;
    read_display_name(bytes, slot, out->display_name, sizeof(out->display_name));
    for (i = 0; i < 3; i++) {
        read_ascii(bytes, base + (size_t)internal_name_offsets[i], 17,
                   out->internal_name[i], sizeof(out->internal_name[i]));
    }
    out->checksum_valid = ww_checksum_valid(bytes, slot);
    out->max_quarters = read_u16be(bytes, base + 0x00);
    out->current_quarters = read_u16be(bytes, base + 0x02);
    out->rupees = read_u16be(bytes, base + 0x04);
    out->wallet_level = bytes[base + 0x12];
    out->wallet_capacity = ww_wallet_capacity(out->wallet_level);
    out->max_magic = bytes[base + 0x13];
    out->current_magic = bytes[base + 0x14];
    out->arrows = bytes[base + 0x69];
    out->bombs = bytes[base + 0x6A];
    out->max_arrows = bytes[base + 0x6F];
    out->max_bombs = bytes[base + 0x70];
    for (i = 0; i < ww_inventory_count; i++) {
        out->inventory[i] = bytes[base + (size_t)ww_inventory[i].offset];
        out->inventory_get[i] = ww_inventory[i].get_offset
            ? bytes[base + (size_t)ww_inventory[i].get_offset] : -1;
    }
    for (i = 0; i < ww_equipment_count; i++) {
        out->equipment[i] = bytes[base + (size_t)ww_equipment[i].offset];
    }
}

/* Keeps dependent values consistent: current never exceeds max, rupees never exceed wallet. */
void ww_normalize(uint8_t *bytes, int slot)
{
    size_t base = (size_t)slot * WW_SLOT_SIZE;
    int max_quarters = read_u16be(bytes, base + 0x00);
    int wallet = ww_wallet_capacity(bytes[base + 0x12]);

    if (read_u16be(bytes, base + 0x02) > max_quarters) {
        write_u16be(bytes, base + 0x02, max_quarters);
    }
    if (read_u16be(bytes, base + 0x04) > wallet) {
        write_u16be(bytes, base + 0x04, wallet);
    }
    if (bytes[base + 0x14] > bytes[base + 0x13]) {
        bytes[base + 0x14] = bytes[base + 0x13];
    }
    if (bytes[base + 0x69] > bytes[base + 0x6F]) {
        bytes[base + 0x69] = bytes[base + 0x6F];
    }
    if (bytes[base + 0x6A] > bytes[base + 0x70]) {
        bytes[base + 0x6A] = bytes[base + 0x70];
    }
}

/* The name lives in the file header (UTF-16LE-ish) and in three ASCII copies per slot. */
static void write_name(uint8_t *bytes, int slot, const char *name)
{
    size_t display = (size_t)display_name_offsets[slot];
    size_t base = (size_t)slot * WW_SLOT_SIZE;
    size_t len = strlen(name);
    size_t i;
    int copy;

    memset(bytes + display, 0, 18);
    for (i = 0; i < len; i++) {
        bytes[display + i * 2] = (uint8_t)name[i];
    }
    for (copy = 0; copy < 3; copy++) {
        size_t offset = base + (size_t)internal_name_offsets[copy];
        memset(bytes + offset, 0, 17);
        for (i = 0; i < len; i++) {
            bytes[offset + i] = (uint8_t)name[i];
        }
    }
}

static int check_name(const char *name, char *err, size_t err_size)
{
    size_t i;

    if (name[0] == '\0') {
        fail(err, err_size, "Quest-log name cannot be empty.");
        return -1;
    }
    if (strlen(name) > WW_NAME_MAX) {
        snprintf(err, err_size, "Quest-log name is limited to %d characters.", WW_NAME_MAX);
        return -1;
    }
    for (i = 0; name[i]; i++) {
        if (name[i] < 0x20 || name[i] > 0x7E) {
            fail(err, err_size, "Quest-log name must use basic ASCII characters.");
            return -1;
        }
    }
    return 0;
}

static int check_int(int value, int min, int max, const char *label, char *err, size_t err_size)
{
    if (value < min || value > max) {
        snprintf(err, err_size, "%s must be a whole number from %d to %d.", label, min, max);
        return -1;
    }
    return 0;
}

static int apply_choice(uint8_t *bytes, int slot, const WwField *field, int value,
                        char *err, size_t err_size)
{
    size_t base = (size_t)slot * WW_SLOT_SIZE;
    size_t at = base + (size_t)field->offset;
    int index = variant_index(field, value);

    if (check_int(value, 0, 255, field->label, err, err_size) != 0) {
        return -1;
    }
    if (index < 0 && value != bytes[at]) {
        snprintf(err, err_size, "Unsupported value for %s.", field->label);
        return -1;
    }
    bytes[at] = (uint8_t)value;
    if (field->get_offset != 0 && index >= 0) {
        size_t mask_at = base + (size_t)field->get_offset;

        if (field->count > 2) {
            bytes[mask_at] = (uint8_t)variant_mask(index);
        } else {
            bytes[mask_at] = (uint8_t)((bytes[mask_at] & ~0x01u) | (index > 0 ? 1u : 0u));
        }
    }
    return 0;
}

int ww_apply(uint8_t *bytes, int slot, const WwEdits *edits, char *err, size_t err_size)
{
    size_t base = (size_t)slot * WW_SLOT_SIZE;
    char current_name[WW_NAME_MAX + 2];
    int i;

    err[0] = '\0';
    if (slot < 0 || slot >= WW_SLOT_COUNT) {
        fail(err, err_size, "Quest-log index must be 0, 1, or 2.");
        return -1;
    }
    if (edits->present & WW_EDIT_NAME) {
        if (check_name(edits->name, err, err_size) != 0) {
            return -1;
        }
        read_display_name(bytes, slot, current_name, sizeof(current_name));
        if (strcmp(edits->name, current_name) != 0) {
            write_name(bytes, slot, edits->name);
        }
    }
    if (edits->present & WW_EDIT_MAX_HEARTS) {
        if (check_int(edits->max_quarters, 1, 80, "Maximum hearts", err, err_size) != 0) {
            return -1;
        }
        write_u16be(bytes, base + 0x00, edits->max_quarters);
    }
    if (edits->present & WW_EDIT_CUR_HEARTS) {
        if (check_int(edits->current_quarters, 1, 80, "Current hearts", err, err_size) != 0) {
            return -1;
        }
        write_u16be(bytes, base + 0x02, edits->current_quarters);
    }
    if (edits->present & WW_EDIT_RUPEES) {
        if (check_int(edits->rupees, 0, 5000, "Rupees", err, err_size) != 0) {
            return -1;
        }
        write_u16be(bytes, base + 0x04, edits->rupees);
    }
    if (edits->present & WW_EDIT_WALLET) {
        if (check_int(edits->wallet_level, 0, 2, "Wallet tier", err, err_size) != 0) {
            return -1;
        }
        bytes[base + 0x12] = (uint8_t)edits->wallet_level;
    }
    if (edits->present & WW_EDIT_MAX_MAGIC) {
        if (edits->max_magic != 0 && edits->max_magic != 16 && edits->max_magic != 32) {
            fail(err, err_size, "Maximum magic must be 0, 16, or 32.");
            return -1;
        }
        bytes[base + 0x13] = (uint8_t)edits->max_magic;
    }
    if (edits->present & WW_EDIT_CUR_MAGIC) {
        if (check_int(edits->current_magic, 0, 32, "Current magic", err, err_size) != 0) {
            return -1;
        }
        bytes[base + 0x14] = (uint8_t)edits->current_magic;
    }
    if (edits->present & WW_EDIT_ARROWS) {
        if (check_int(edits->arrows, 0, 99, "Arrows", err, err_size) != 0) {
            return -1;
        }
        bytes[base + 0x69] = (uint8_t)edits->arrows;
    }
    if (edits->present & WW_EDIT_BOMBS) {
        if (check_int(edits->bombs, 0, 99, "Bombs", err, err_size) != 0) {
            return -1;
        }
        bytes[base + 0x6A] = (uint8_t)edits->bombs;
    }
    if (edits->present & WW_EDIT_MAX_ARROWS) {
        if (check_int(edits->max_arrows, 0, 99, "Maximum arrows", err, err_size) != 0) {
            return -1;
        }
        bytes[base + 0x6F] = (uint8_t)edits->max_arrows;
    }
    if (edits->present & WW_EDIT_MAX_BOMBS) {
        if (check_int(edits->max_bombs, 0, 99, "Maximum bombs", err, err_size) != 0) {
            return -1;
        }
        bytes[base + 0x70] = (uint8_t)edits->max_bombs;
    }
    if (edits->present & WW_EDIT_INVENTORY) {
        for (i = 0; i < ww_inventory_count; i++) {
            if (edits->inventory[i] >= 0
                && apply_choice(bytes, slot, &ww_inventory[i], edits->inventory[i],
                                err, err_size) != 0) {
                return -1;
            }
        }
    }
    if (edits->present & WW_EDIT_EQUIPMENT) {
        for (i = 0; i < ww_equipment_count; i++) {
            if (edits->equipment[i] >= 0
                && apply_choice(bytes, slot, &ww_equipment[i], edits->equipment[i],
                                err, err_size) != 0) {
                return -1;
            }
        }
    }
    ww_normalize(bytes, slot);
    ww_update_checksum(bytes, slot);
    return 0;
}

int ww_load(const char *path, uint8_t *bytes, char *err, size_t err_size)
{
    FILE *f = fopen(path, "rb");
    size_t got;

    err[0] = '\0';
    if (!f) {
        snprintf(err, err_size, "Cannot open %s.", path);
        return -1;
    }
    got = fread(bytes, 1, WW_SAVE_SIZE, f);
    if (got != WW_SAVE_SIZE || fgetc(f) != EOF) {
        snprintf(err, err_size,
                 "Expected cking.sav to be %d bytes (0x%X); ZIP archives and pictograph files are not accepted.",
                 WW_SAVE_SIZE, WW_SAVE_SIZE);
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int ww_write(const char *path, const uint8_t *bytes, char *err, size_t err_size)
{
    FILE *f = fopen(path, "wb");

    err[0] = '\0';
    if (!f) {
        snprintf(err, err_size, "Cannot write %s.", path);
        return -1;
    }
    if (fwrite(bytes, 1, WW_SAVE_SIZE, f) != WW_SAVE_SIZE) {
        fail(err, err_size, "Write failed; output may be truncated.");
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

/* The game treats the mask as a floor, so a mask above the item byte is a silent edit waiting to
 * be undone. Returns the item the game would restore, or NULL when the two agree. */
static const char *mask_conflict(const WwField *field, int item_value, int mask)
{
    int best = 0;
    int current = variant_index(field, item_value);
    int bit;

    if (field->get_offset == 0 || mask < 0 || current < 0) {
        return NULL;
    }
    for (bit = 0; bit < 8; bit++) {
        if (mask & (1 << bit)) {
            best = bit + 1;
        }
    }
    if (field->count <= 2 && mask != 0 && (mask & ~0x01) != 0) {
        return NULL;  /* other bits carry unrelated data, so the mask says nothing here */
    }
    return best > current ? field->values[best].label : NULL;
}

void ww_print_slots(FILE *out, const uint8_t *bytes)
{
    int slot;
    int i;

    for (slot = 0; slot < WW_SLOT_COUNT; slot++) {
        WwSlot s;
        char max_hearts[16];
        char current_hearts[16];

        ww_read_slot(bytes, slot, &s);
        ww_format_hearts(max_hearts, sizeof(max_hearts), s.max_quarters);
        ww_format_hearts(current_hearts, sizeof(current_hearts), s.current_quarters);

        fprintf(out, "Quest Log %d: %s  [%s]\n", slot + 1,
                s.display_name[0] ? s.display_name : "Empty",
                s.checksum_valid ? "checksum ok" : "checksum needs repair");
        fprintf(out, "  %-16s %s / %s hearts\n", "Health", current_hearts, max_hearts);
        fprintf(out, "  %-16s %d / %d\n", "Rupees", s.rupees, s.wallet_capacity);
        fprintf(out, "  %-16s %d / %d\n", "Magic", s.current_magic, s.max_magic);
        fprintf(out, "  %-16s %d / %d arrows, %d / %d bombs\n", "Ammo",
                s.arrows, s.max_arrows, s.bombs, s.max_bombs);
        for (i = 0; i < ww_equipment_count; i++) {
            fprintf(out, "  %-16s %s\n", ww_equipment[i].label,
                    ww_value_label(&ww_equipment[i], s.equipment[i]));
        }
        for (i = 0; i < ww_inventory_count; i++) {
            const char *conflict;

            if (ww_inventory[i].count > 2 && s.inventory_get[i] >= 0) {
                fprintf(out, "  %-16s %s  (owned mask 0x%02X)\n", ww_inventory[i].label,
                        ww_value_label(&ww_inventory[i], s.inventory[i]), s.inventory_get[i]);
            } else {
                fprintf(out, "  %-16s %s\n", ww_inventory[i].label,
                        ww_value_label(&ww_inventory[i], s.inventory[i]));
            }
            conflict = mask_conflict(&ww_inventory[i], s.inventory[i], s.inventory_get[i]);
            if (conflict) {
                fprintf(out, "  %-16s   ! mask still claims %s; the game restores it\n",
                        "", conflict);
            }
        }
        if (slot + 1 < WW_SLOT_COUNT) {
            fprintf(out, "\n");
        }
    }
}

/* Change review line; only shown when the field actually differs. */
static int report(FILE *out, const char *label, const char *before, const char *after)
{
    if (strcmp(before, after) == 0) {
        return 0;
    }
    fprintf(out, "  %-16s %s  ->  %s\n", label, before, after);
    return 1;
}

void ww_print_diff(FILE *out, const uint8_t *original, const uint8_t *modified)
{
    int slot;
    int i;
    int total = 0;
    size_t byte_changes = 0;

    for (size_t at = 0; at < WW_SAVE_SIZE; at++) {
        if (original[at] != modified[at]) {
            byte_changes++;
        }
    }

    for (slot = 0; slot < WW_SLOT_COUNT; slot++) {
        WwSlot a;
        WwSlot b;
        char before[32];
        char after[32];
        int shown = 0;

        ww_read_slot(original, slot, &a);
        ww_read_slot(modified, slot, &b);
        ww_format_hearts(before, sizeof(before), a.max_quarters);
        ww_format_hearts(after, sizeof(after), b.max_quarters);

        fprintf(out, "Quest Log %d: %s\n", slot + 1,
                b.display_name[0] ? b.display_name : "Empty");
        shown += report(out, "Name", a.display_name, b.display_name);
        shown += report(out, "Maximum hearts", before, after);
        ww_format_hearts(before, sizeof(before), a.current_quarters);
        ww_format_hearts(after, sizeof(after), b.current_quarters);
        shown += report(out, "Current hearts", before, after);
        snprintf(before, sizeof(before), "%d", a.rupees);
        snprintf(after, sizeof(after), "%d", b.rupees);
        shown += report(out, "Rupees", before, after);
        snprintf(before, sizeof(before), "%d", a.wallet_capacity);
        snprintf(after, sizeof(after), "%d", b.wallet_capacity);
        shown += report(out, "Wallet", before, after);
        snprintf(before, sizeof(before), "%d", a.max_magic);
        snprintf(after, sizeof(after), "%d", b.max_magic);
        shown += report(out, "Maximum magic", before, after);
        snprintf(before, sizeof(before), "%d", a.current_magic);
        snprintf(after, sizeof(after), "%d", b.current_magic);
        shown += report(out, "Current magic", before, after);
        snprintf(before, sizeof(before), "%d", a.arrows);
        snprintf(after, sizeof(after), "%d", b.arrows);
        shown += report(out, "Arrows", before, after);
        snprintf(before, sizeof(before), "%d", a.max_arrows);
        snprintf(after, sizeof(after), "%d", b.max_arrows);
        shown += report(out, "Maximum arrows", before, after);
        snprintf(before, sizeof(before), "%d", a.bombs);
        snprintf(after, sizeof(after), "%d", b.bombs);
        shown += report(out, "Bombs", before, after);
        snprintf(before, sizeof(before), "%d", a.max_bombs);
        snprintf(after, sizeof(after), "%d", b.max_bombs);
        shown += report(out, "Maximum bombs", before, after);
        for (i = 0; i < ww_inventory_count; i++) {
            shown += report(out, ww_inventory[i].label,
                            ww_value_label(&ww_inventory[i], a.inventory[i]),
                            ww_value_label(&ww_inventory[i], b.inventory[i]));
            if (a.inventory_get[i] >= 0 && a.inventory_get[i] != b.inventory_get[i]) {
                char label[64];
                snprintf(label, sizeof(label), "%s owned", ww_inventory[i].label);
                snprintf(before, sizeof(before), "0x%02X", a.inventory_get[i]);
                snprintf(after, sizeof(after), "0x%02X", b.inventory_get[i]);
                shown += report(out, label, before, after);
            }
        }
        for (i = 0; i < ww_equipment_count; i++) {
            shown += report(out, ww_equipment[i].label,
                            ww_value_label(&ww_equipment[i], a.equipment[i]),
                            ww_value_label(&ww_equipment[i], b.equipment[i]));
        }
        total += shown;
        if (!shown) {
            fprintf(out, "  (no field changes)\n");
        }
    }

    if (total == 0) {
        fprintf(out, byte_changes ? "  Only checksum bytes have changed.\n"
                                  : "  No changes yet.\n");
    }
    fprintf(out, "%zu byte(s) differ.\n", byte_changes);
}
