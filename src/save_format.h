/* save_format.h - raw cking.sav (Wind Waker HD, Wii U) access layer. */
#ifndef WWHD_SAVE_FORMAT_H
#define WWHD_SAVE_FORMAT_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define WW_SAVE_SIZE       0x2306 /* raw cking.sav length, 8966 bytes */
#define WW_SLOT_SIZE       0x0A94 /* one quest log */
#define WW_SLOT_COUNT      3
#define WW_CHECKSUM_OFFSET 0x0A8C /* u32 sum, then u32 inverted sum */
#define WW_NAME_MAX        8      /* quest-log name length limit */
#define WW_MAX_FIELDS      32     /* capacity for the field tables below */

typedef struct {
    int value;
    const char *label;
} WwValue;

typedef struct {
    const char *key;      /* CLI token, matched case/underscore-insensitively */
    const char *label;    /* human-readable name */
    int offset;           /* offset inside a quest log */
    const WwValue *values;
    int count;
} WwField;

extern const WwField ww_inventory[WW_MAX_FIELDS];
extern const int ww_inventory_count;
extern const WwField ww_equipment[WW_MAX_FIELDS];
extern const int ww_equipment_count;

typedef struct {
    int slot;
    char display_name[WW_NAME_MAX + 2];
    char internal_name[3][18];
    int checksum_valid;
    int max_quarters;     /* heart containers in quarter-heart units */
    int current_quarters;
    int rupees;
    int wallet_level;
    int wallet_capacity;
    int max_magic;
    int current_magic;
    int arrows;
    int max_arrows;
    int bombs;
    int max_bombs;
    int inventory[WW_MAX_FIELDS];  /* parallel to ww_inventory */
    int equipment[WW_MAX_FIELDS];  /* parallel to ww_equipment */
} WwSlot;

/* Edit mask, so only the fields the caller set are written. */
#define WW_EDIT_NAME        (1u << 0)
#define WW_EDIT_MAX_HEARTS  (1u << 1)
#define WW_EDIT_CUR_HEARTS  (1u << 2)
#define WW_EDIT_RUPEES      (1u << 3)
#define WW_EDIT_WALLET      (1u << 4)
#define WW_EDIT_MAX_MAGIC   (1u << 5)
#define WW_EDIT_CUR_MAGIC   (1u << 6)
#define WW_EDIT_ARROWS      (1u << 7)
#define WW_EDIT_MAX_ARROWS  (1u << 8)
#define WW_EDIT_BOMBS       (1u << 9)
#define WW_EDIT_MAX_BOMBS   (1u << 10)
#define WW_EDIT_INVENTORY   (1u << 11)
#define WW_EDIT_EQUIPMENT   (1u << 12)

typedef struct {
    unsigned present;
    char name[WW_NAME_MAX + 2];
    int max_quarters;
    int current_quarters;
    int rupees;
    int wallet_level;
    int max_magic;
    int current_magic;
    int arrows;
    int max_arrows;
    int bombs;
    int max_bombs;
    int inventory[WW_MAX_FIELDS];  /* parallel to ww_inventory, -1 = keep */
    int equipment[WW_MAX_FIELDS];  /* parallel to ww_equipment, -1 = keep */
} WwEdits;

/* Loads WW_SAVE_SIZE bytes; any other size is rejected. Returns 0 on success. */
int ww_load(const char *path, uint8_t *bytes, char *err, size_t err_size);
int ww_write(const char *path, const uint8_t *bytes, char *err, size_t err_size);

void ww_read_slot(const uint8_t *bytes, int slot, WwSlot *out);

/* Applies edits, normalizes dependent values, then refreshes that slot checksum. */
int ww_apply(uint8_t *bytes, int slot, const WwEdits *edits, char *err, size_t err_size);

void ww_normalize(uint8_t *bytes, int slot);
int ww_checksum_valid(const uint8_t *bytes, int slot);
void ww_update_checksum(uint8_t *bytes, int slot);
void ww_repair_all(uint8_t *bytes);

void ww_print_slots(FILE *out, const uint8_t *bytes);
void ww_print_diff(FILE *out, const uint8_t *original, const uint8_t *modified);

const WwField *ww_find_field(const WwField *fields, int count, const char *key);
int ww_value_known(const WwField *field, int value);
const char *ww_value_label(const WwField *field, int value);
int ww_wallet_capacity(int level);
void ww_format_hearts(char *buf, size_t size, int quarters);

#endif /* WWHD_SAVE_FORMAT_H */
