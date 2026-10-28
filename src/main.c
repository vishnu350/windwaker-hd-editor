/* main.c - command line front end for the cking.sav editor. */
#include "save_format.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    const char *path;
    const char *out_path;
    int slot;         /* quest log, 0-based */
    int repair;
    int dry_run;
    int force;
    int wants_info;
    WwEdits edits;
} Options;

static void usage(const char *argv0)
{
    printf("Usage: %s [options] [cking.sav]\n\n", argv0);
    printf("  -i, --info              print the three quest logs and exit\n");
    printf("  -s, --slot N            quest log to edit, 1-3 (default 1)\n");
    printf("  -n, --name NAME         quest-log name, 1-8 ASCII characters\n");
    printf("      --hearts N          maximum heart containers, e.g. 11.75\n");
    printf("      --health N          current hearts\n");
    printf("      --rupees N          rupee count, clamped to the wallet capacity\n");
    printf("      --wallet N          wallet capacity: 200, 1000 or 5000\n");
    printf("      --magic-max N       maximum magic meter (0, 16 or 32)\n");
    printf("      --magic N           current magic\n");
    printf("      --arrows N          arrows held\n");
    printf("      --arrows-max N      arrow capacity\n");
    printf("      --bombs N           bombs held\n");
    printf("      --bombs-max N       bomb capacity\n");
    printf("      --item KEY=VALUE    inventory slot, e.g. --item sail=0x77\n");
    printf("      --equip KEY=VALUE   equipment, e.g. --equip sword=0x3E\n");
    printf("      --repair            recalculate all three quest-log checksums\n");
    printf("      --out PATH          output file (default <name>-modified.sav)\n");
    printf("      --dry-run           review the changes without writing a file\n");
    printf("      --yes               allow overwriting an existing output file\n");
    printf("  -L, --list-items        print item keys and accepted values\n");
    printf("  -h, --help              this text\n");
    printf("\nThe input file is never modified; edits land in the output file.\n");
}

static void list_items(void)
{
    int i;

    printf("Equipment (--equip KEY=VALUE)\n");
    for (i = 0; i < ww_equipment_count; i++) {
        printf("  %-14s %s\n", ww_equipment[i].key, ww_equipment[i].label);
        for (int v = 0; v < ww_equipment[i].count; v++) {
            printf("      0x%02X  %s\n", ww_equipment[i].values[v].value & 0xFF,
                   ww_equipment[i].values[v].label);
        }
    }
    printf("\nInventory (--item KEY=VALUE)\n");
    for (i = 0; i < ww_inventory_count; i++) {
        printf("  %-14s %s\n", ww_inventory[i].key, ww_inventory[i].label);
        for (int v = 0; v < ww_inventory[i].count; v++) {
            printf("      0x%02X  %s\n", ww_inventory[i].values[v].value & 0xFF,
                   ww_inventory[i].values[v].label);
        }
    }
}

static int parse_int(const char *text, int *out)
{
    char *end;
    long value = strtol(text, &end, 0);

    if (*end != '\0' || end == text || value < 0 || value > 0x7FFFFFFF) {
        return -1;
    }
    *out = (int)value;
    return 0;
}

/* Heart values are stored as quarter units; accepts 3, 3.5, 3.75. */
static int parse_hearts(const char *text, int *quarters)
{
    char *end;
    double hearts = strtod(text, &end);
    double units;

    if (*end != '\0' || end == text) {
        return -1;
    }
    units = hearts * 4.0;
    if (hearts < 0.25 || hearts > 20.0 || fabs(units - rint(units)) > 1e-9) {
        return -1;
    }
    *quarters = (int)rint(units);
    return 0;
}

static int parse_wallet(const char *text, int *level)
{
    int capacity;

    if (parse_int(text, &capacity) != 0) {
        return -1;
    }
    if (capacity == 200) {
        *level = 0;
    } else if (capacity == 1000) {
        *level = 1;
    } else if (capacity == 5000) {
        *level = 2;
    } else {
        return -1;
    }
    return 0;
}

/* Splits KEY=VALUE and stores the value against the matching table row. */
static int add_choice(const char *arg, WwEdits *edits, unsigned flag, const WwField *table,
                      int count, int slots[], char *err, size_t err_size)
{
    const WwField *field;
    char key[64];
    const char *value = strchr(arg, '=');
    int parsed;
    int i;

    if (!value) {
        snprintf(err, err_size, "Expected KEY=VALUE, got '%s'.", arg);
        return -1;
    }
    if ((size_t)(value - arg) >= sizeof(key)) {
        snprintf(err, err_size, "Item key is too long.");
        return -1;
    }
    memcpy(key, arg, (size_t)(value - arg));
    key[value - arg] = '\0';
    value++;

    field = ww_find_field(table, count, key);
    if (!field) {
        snprintf(err, err_size, "Unknown key '%s'; use --list-items.", key);
        return -1;
    }
    if (parse_int(value, &parsed) != 0) {
        snprintf(err, err_size, "Value for %s must be a number such as 0x77.", key);
        return -1;
    }
    for (i = 0; i < count; i++) {
        if (field == &table[i]) {
            slots[i] = parsed;
            break;
        }
    }
    edits->present |= flag;
    return 0;
}

static int same_file(const char *a, const char *b)
{
    return strcmp(a, b) == 0;
}

int main(int argc, char **argv)
{
    Options o;
    static uint8_t original[WW_SAVE_SIZE];
    static uint8_t working[WW_SAVE_SIZE];
    char err[256];
    char out_path[512];
    int i;

    memset(&o, 0, sizeof(o));
    o.path = "cking.sav";
    o.slot = 0;
    for (i = 0; i < WW_MAX_FIELDS; i++) {
        o.edits.inventory[i] = -1;
        o.edits.equipment[i] = -1;
    }

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *next;

        if (arg[0] != '-' || arg[1] == '\0') {
            o.path = arg;
            continue;
        }
        next = (i + 1 < argc) ? argv[i + 1] : NULL;
#define NEED_VALUE()                                                        \
        do {                                                                \
            if (!next) {                                                    \
                fprintf(stderr, "%s needs a value.\n", arg);                \
                return 1;                                                   \
            }                                                               \
            i++;                                                            \
        } while (0)

        if (!strcmp(arg, "-h") || !strcmp(arg, "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(arg, "-L") || !strcmp(arg, "--list-items")) {
            list_items();
            return 0;
        } else if (!strcmp(arg, "-i") || !strcmp(arg, "--info")) {
            o.wants_info = 1;
        } else if (!strcmp(arg, "--repair")) {
            o.repair = 1;
        } else if (!strcmp(arg, "--dry-run")) {
            o.dry_run = 1;
        } else if (!strcmp(arg, "-y") || !strcmp(arg, "--yes")) {
            o.force = 1;
        } else if (!strcmp(arg, "-s") || !strcmp(arg, "--slot")) {
            int slot;
            NEED_VALUE();
            if (parse_int(next, &slot) != 0 || slot < 1 || slot > WW_SLOT_COUNT) {
                fprintf(stderr, "Quest log must be 1, 2 or 3.\n");
                return 1;
            }
            o.slot = slot - 1;
        } else if (!strcmp(arg, "-n") || !strcmp(arg, "--name")) {
            NEED_VALUE();
            snprintf(o.edits.name, sizeof(o.edits.name), "%s", next);
            o.edits.present |= WW_EDIT_NAME;
        } else if (!strcmp(arg, "--hearts")) {
            NEED_VALUE();
            if (parse_hearts(next, &o.edits.max_quarters) != 0) {
                fprintf(stderr, "Maximum hearts must be between 0.25 and 20 in quarter hearts.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_MAX_HEARTS;
        } else if (!strcmp(arg, "--health")) {
            NEED_VALUE();
            if (parse_hearts(next, &o.edits.current_quarters) != 0) {
                fprintf(stderr, "Current hearts must be between 0.25 and 20 in quarter hearts.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_CUR_HEARTS;
        } else if (!strcmp(arg, "--rupees")) {
            NEED_VALUE();
            if (parse_int(next, &o.edits.rupees) != 0) {
                fprintf(stderr, "Rupees must be a whole number.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_RUPEES;
        } else if (!strcmp(arg, "--wallet")) {
            NEED_VALUE();
            if (parse_wallet(next, &o.edits.wallet_level) != 0) {
                fprintf(stderr, "Wallet capacity must be 200, 1000 or 5000.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_WALLET;
        } else if (!strcmp(arg, "--magic-max")) {
            NEED_VALUE();
            if (parse_int(next, &o.edits.max_magic) != 0) {
                fprintf(stderr, "Maximum magic must be a whole number.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_MAX_MAGIC;
        } else if (!strcmp(arg, "--magic")) {
            NEED_VALUE();
            if (parse_int(next, &o.edits.current_magic) != 0) {
                fprintf(stderr, "Current magic must be a whole number.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_CUR_MAGIC;
        } else if (!strcmp(arg, "--arrows")) {
            NEED_VALUE();
            if (parse_int(next, &o.edits.arrows) != 0) {
                fprintf(stderr, "Arrows must be a whole number.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_ARROWS;
        } else if (!strcmp(arg, "--arrows-max")) {
            NEED_VALUE();
            if (parse_int(next, &o.edits.max_arrows) != 0) {
                fprintf(stderr, "Maximum arrows must be a whole number.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_MAX_ARROWS;
        } else if (!strcmp(arg, "--bombs")) {
            NEED_VALUE();
            if (parse_int(next, &o.edits.bombs) != 0) {
                fprintf(stderr, "Bombs must be a whole number.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_BOMBS;
        } else if (!strcmp(arg, "--bombs-max")) {
            NEED_VALUE();
            if (parse_int(next, &o.edits.max_bombs) != 0) {
                fprintf(stderr, "Maximum bombs must be a whole number.\n");
                return 1;
            }
            o.edits.present |= WW_EDIT_MAX_BOMBS;
        } else if (!strcmp(arg, "--out") || !strcmp(arg, "-o")) {
            NEED_VALUE();
            o.out_path = next;
        } else if (!strcmp(arg, "--item")) {
            NEED_VALUE();
            if (add_choice(next, &o.edits, WW_EDIT_INVENTORY, ww_inventory,
                           ww_inventory_count, o.edits.inventory, err, sizeof(err)) != 0) {
                fprintf(stderr, "%s\n", err);
                return 1;
            }
        } else if (!strcmp(arg, "--equip")) {
            NEED_VALUE();
            if (add_choice(next, &o.edits, WW_EDIT_EQUIPMENT, ww_equipment,
                           ww_equipment_count, o.edits.equipment, err, sizeof(err)) != 0) {
                fprintf(stderr, "%s\n", err);
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown option '%s'. Try --help.\n", arg);
            return 1;
        }
    }

    if (ww_load(o.path, original, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    memcpy(working, original, WW_SAVE_SIZE);

    if (o.edits.present == 0 && !o.repair) {
        ww_print_slots(stdout, working);
        return 0;
    }
    if (o.edits.present && ww_apply(working, o.slot, &o.edits, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    if (o.repair) {
        ww_repair_all(working);
    }

    printf("Changes for %s\n", o.path);
    ww_print_diff(stdout, original, working);

    if (o.dry_run) {
        printf("\nDry run: nothing written.\n");
        return 0;
    }

    if (!o.out_path) {
        ww_default_out_path(o.path, out_path, sizeof(out_path));
        o.out_path = out_path;
    }
    if (same_file(o.out_path, o.path) && !o.force) {
        fprintf(stderr, "Refusing to overwrite the input file; pass --yes to allow it.\n");
        return 1;
    }
    if (access(o.out_path, F_OK) == 0 && !o.force) {
        fprintf(stderr, "%s already exists; pass --yes to overwrite it.\n", o.out_path);
        return 1;
    }
    if (ww_write(o.out_path, working, err, sizeof(err)) != 0) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    printf("\nWrote %s\n", o.out_path);

    if (o.wants_info) {
        printf("\n");
        ww_print_slots(stdout, working);
    }
    return 0;
}
