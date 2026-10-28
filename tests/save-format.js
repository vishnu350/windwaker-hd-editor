export const SAVE_SIZE = 0x2306;
export const SLOT_SIZE = 0x0A94;
export const SLOT_COUNT = 3;
export const CHECKSUM_OFFSET = 0x0A8C;

const DISPLAY_NAME_OFFSETS = [0x2035, 0x2047, 0x2059];
const INTERNAL_NAME_OFFSETS = [0x0157, 0x0168, 0x0179];
const NAME_MAX_LENGTH = 8;

export const INVENTORY = {
  telescope: { offset: 0x3C, label: "Telescope", values: [[0xFF, "None"], [0x20, "Telescope"]] },
  sail: { offset: 0x3D, label: "Sail", values: [[0xFF, "None"], [0x78, "Normal Sail"], [0x77, "Swift Sail"]] },
  windWaker: { offset: 0x3E, label: "Wind Waker", values: [[0xFF, "None"], [0x22, "Wind Waker"]] },
  grapplingHook: { offset: 0x3F, label: "Grappling Hook", values: [[0xFF, "None"], [0x25, "Grappling Hook"]] },
  spoilsBag: { offset: 0x40, label: "Spoils Bag", values: [[0xFF, "None"], [0x24, "Spoils Bag"]] },
  boomerang: { offset: 0x41, label: "Boomerang", values: [[0xFF, "None"], [0x2D, "Boomerang"]] },
  dekuLeaf: { offset: 0x42, label: "Deku Leaf", values: [[0xFF, "None"], [0x34, "Deku Leaf"]] },
  tingleBottle: { offset: 0x43, label: "Tingle Bottle", values: [[0xFF, "None"], [0x21, "Tingle Bottle"]] },
  pictobox: { offset: 0x44, label: "Picto Box", values: [[0xFF, "None"], [0x23, "Picto Box"], [0x26, "Deluxe Picto Box"]] },
  ironBoots: { offset: 0x45, label: "Iron Boots", values: [[0xFF, "None"], [0x29, "Iron Boots"]] },
  magicArmor: { offset: 0x46, label: "Magic Armor", values: [[0xFF, "None"], [0x2A, "Magic Armor"]] },
  baitBag: { offset: 0x47, label: "Bait Bag", values: [[0xFF, "None"], [0x2C, "Bait Bag"]] },
  bow: { offset: 0x48, label: "Bow", values: [[0xFF, "None"], [0x27, "Hero's Bow"], [0x35, "Fire & Ice Arrows"], [0x36, "Light Arrows"]] },
  bombsItem: { offset: 0x49, label: "Bombs", values: [[0xFF, "None"], [0x31, "Bombs"]] },
  bottle1: { offset: 0x4A, label: "Bottle 1", values: bottleValues() },
  bottle2: { offset: 0x4B, label: "Bottle 2", values: bottleValues() },
  bottle3: { offset: 0x4C, label: "Bottle 3", values: bottleValues() },
  bottle4: { offset: 0x4D, label: "Bottle 4", values: bottleValues() },
  deliveryBag: { offset: 0x4E, label: "Delivery Bag", values: [[0xFF, "None"], [0x30, "Delivery Bag"]] },
  hookshot: { offset: 0x4F, label: "Hookshot", values: [[0xFF, "None"], [0x2F, "Hookshot"]] },
  skullHammer: { offset: 0x50, label: "Skull Hammer", values: [[0xFF, "None"], [0x33, "Skull Hammer"]] },
};

export const EQUIPMENT = {
  sword: {
    offset: 0x0E,
    label: "Sword",
    values: [[0xFF, "None"], [0x38, "Hero's Sword"], [0x39, "Master Sword — powerless"], [0x3A, "Master Sword — half restored"], [0x3E, "Master Sword — fully restored"]],
  },
  shield: {
    offset: 0x0F,
    label: "Shield",
    values: [[0xFF, "None"], [0x3B, "Hero's Shield"], [0x3C, "Mirror Shield"]],
  },
  bracelets: {
    offset: 0x10,
    label: "Power Bracelets",
    values: [[0xFF, "None"], [0x28, "Power Bracelets"]],
  },
};

function bottleValues() {
  return [
    [0xFF, "No bottle"],
    [0x50, "Empty Bottle"],
    [0x51, "Red Potion"],
    [0x52, "Green Potion"],
    [0x53, "Blue Potion"],
    [0x54, "Elixir Soup — half"],
    [0x55, "Elixir Soup"],
    [0x56, "Water"],
    [0x57, "Fairy"],
    [0x58, "Forest Firefly"],
    [0x59, "Forest Water"],
  ];
}

export function assertSave(bytes) {
  if (!(bytes instanceof Uint8Array)) {
    throw new TypeError("Save data must be a Uint8Array.");
  }
  if (bytes.length !== SAVE_SIZE) {
    throw new Error(`Expected cking.sav to be ${SAVE_SIZE} bytes (0x${SAVE_SIZE.toString(16).toUpperCase()}), but received ${bytes.length} bytes.`);
  }
}

export function cloneBytes(bytes) {
  assertSave(bytes);
  return new Uint8Array(bytes);
}

export function readSave(bytes) {
  assertSave(bytes);
  return Array.from({ length: SLOT_COUNT }, (_, slotIndex) => readSlot(bytes, slotIndex));
}

export function readSlot(bytes, slotIndex) {
  assertSlotIndex(slotIndex);
  assertSave(bytes);
  const base = slotIndex * SLOT_SIZE;
  const maxQuarterHearts = readU16BE(bytes, base + 0x00);
  const currentQuarterHearts = readU16BE(bytes, base + 0x02);
  const walletLevel = bytes[base + 0x12];
  const maxMagic = bytes[base + 0x13];
  const currentMagic = bytes[base + 0x14];

  return {
    slotIndex,
    displayName: readDisplayName(bytes, slotIndex),
    internalNames: INTERNAL_NAME_OFFSETS.map((offset) => readAscii(bytes, base + offset, 17)),
    checksumValid: validateSlotChecksum(bytes, slotIndex),
    maxHearts: maxQuarterHearts / 4,
    currentHearts: currentQuarterHearts / 4,
    rupees: readU16BE(bytes, base + 0x04),
    walletLevel,
    walletCapacity: walletCapacity(walletLevel),
    maxMagic,
    currentMagic,
    arrows: bytes[base + 0x69],
    bombs: bytes[base + 0x6A],
    maxArrows: bytes[base + 0x6F],
    maxBombs: bytes[base + 0x70],
    inventory: Object.fromEntries(Object.entries(INVENTORY).map(([key, field]) => [key, bytes[base + field.offset]])),
    equipment: Object.fromEntries(Object.entries(EQUIPMENT).map(([key, field]) => [key, bytes[base + field.offset]])),
  };
}

export function applySlotEdits(bytes, slotIndex, edits) {
  assertSave(bytes);
  assertSlotIndex(slotIndex);
  const base = slotIndex * SLOT_SIZE;

  if (Object.hasOwn(edits, "displayName")) {
    const requestedName = String(edits.displayName ?? "");
    const currentName = readDisplayName(bytes, slotIndex);
    if (requestedName !== currentName) writePlayerName(bytes, slotIndex, requestedName);
  }
  if (Object.hasOwn(edits, "maxHearts")) {
    writeU16BE(bytes, base + 0x00, heartsToQuarterUnits(edits.maxHearts, "Maximum hearts"));
  }
  if (Object.hasOwn(edits, "currentHearts")) {
    writeU16BE(bytes, base + 0x02, heartsToQuarterUnits(edits.currentHearts, "Current hearts"));
  }
  if (Object.hasOwn(edits, "rupees")) {
    writeU16BE(bytes, base + 0x04, integerInRange(edits.rupees, 0, 5000, "Rupees"));
  }
  if (Object.hasOwn(edits, "walletLevel")) {
    bytes[base + 0x12] = integerInRange(edits.walletLevel, 0, 2, "Wallet level");
  }
  if (Object.hasOwn(edits, "maxMagic")) {
    const maxMagic = integerInRange(edits.maxMagic, 0, 32, "Maximum magic");
    if (![0, 16, 32].includes(maxMagic)) throw new Error("Maximum magic must be 0, 16, or 32.");
    bytes[base + 0x13] = maxMagic;
  }
  if (Object.hasOwn(edits, "currentMagic")) {
    bytes[base + 0x14] = integerInRange(edits.currentMagic, 0, 32, "Current magic");
  }
  if (Object.hasOwn(edits, "arrows")) {
    bytes[base + 0x69] = integerInRange(edits.arrows, 0, 99, "Arrows");
  }
  if (Object.hasOwn(edits, "bombs")) {
    bytes[base + 0x6A] = integerInRange(edits.bombs, 0, 99, "Bombs");
  }
  if (Object.hasOwn(edits, "maxArrows")) {
    bytes[base + 0x6F] = integerInRange(edits.maxArrows, 0, 99, "Maximum arrows");
  }
  if (Object.hasOwn(edits, "maxBombs")) {
    bytes[base + 0x70] = integerInRange(edits.maxBombs, 0, 99, "Maximum bombs");
  }

  if (edits.inventory) {
    for (const [key, value] of Object.entries(edits.inventory)) {
      const field = INVENTORY[key];
      if (!field) throw new Error(`Unknown inventory field: ${key}`);
      const allowed = field.values.map(([id]) => id);
      const parsed = integerInRange(value, 0, 255, field.label);
      const current = bytes[base + field.offset];
      if (!allowed.includes(parsed) && parsed !== current) throw new Error(`Unsupported value for ${field.label}.`);
      bytes[base + field.offset] = parsed;
    }
  }

  if (edits.equipment) {
    for (const [key, value] of Object.entries(edits.equipment)) {
      const field = EQUIPMENT[key];
      if (!field) throw new Error(`Unknown equipment field: ${key}`);
      const allowed = field.values.map(([id]) => id);
      const parsed = integerInRange(value, 0, 255, field.label);
      const current = bytes[base + field.offset];
      if (!allowed.includes(parsed) && parsed !== current) throw new Error(`Unsupported value for ${field.label}.`);
      bytes[base + field.offset] = parsed;
    }
  }

  normalizeSlot(bytes, slotIndex);
  updateSlotChecksum(bytes, slotIndex);
  return readSlot(bytes, slotIndex);
}

export function normalizeSlot(bytes, slotIndex) {
  const base = slotIndex * SLOT_SIZE;
  const maxHearts = readU16BE(bytes, base + 0x00);
  const currentHearts = readU16BE(bytes, base + 0x02);
  if (currentHearts > maxHearts) writeU16BE(bytes, base + 0x02, maxHearts);

  const walletMax = walletCapacity(bytes[base + 0x12]);
  const rupees = readU16BE(bytes, base + 0x04);
  if (rupees > walletMax) writeU16BE(bytes, base + 0x04, walletMax);

  if (bytes[base + 0x14] > bytes[base + 0x13]) bytes[base + 0x14] = bytes[base + 0x13];
  if (bytes[base + 0x69] > bytes[base + 0x6F]) bytes[base + 0x69] = bytes[base + 0x6F];
  if (bytes[base + 0x6A] > bytes[base + 0x70]) bytes[base + 0x6A] = bytes[base + 0x70];
}

export function repairAllChecksums(bytes) {
  assertSave(bytes);
  for (let i = 0; i < SLOT_COUNT; i += 1) updateSlotChecksum(bytes, i);
  return bytes;
}

export function calculateSlotChecksum(bytes, slotIndex) {
  assertSave(bytes);
  assertSlotIndex(slotIndex);
  const base = slotIndex * SLOT_SIZE;
  let high = 0;
  let low = 0;
  for (let i = 0; i < CHECKSUM_OFFSET; i += 1) {
    const value = bytes[base + i];
    high = (high + value) >>> 0;
    low = (low + ((~value) >>> 0)) >>> 0;
  }
  return { high, low };
}

export function validateSlotChecksum(bytes, slotIndex) {
  const base = slotIndex * SLOT_SIZE;
  const { high, low } = calculateSlotChecksum(bytes, slotIndex);
  return readU32BE(bytes, base + CHECKSUM_OFFSET) === high && readU32BE(bytes, base + CHECKSUM_OFFSET + 4) === low;
}

export function updateSlotChecksum(bytes, slotIndex) {
  const base = slotIndex * SLOT_SIZE;
  const { high, low } = calculateSlotChecksum(bytes, slotIndex);
  writeU32BE(bytes, base + CHECKSUM_OFFSET, high);
  writeU32BE(bytes, base + CHECKSUM_OFFSET + 4, low);
}

export function diffSave(original, modified) {
  assertSave(original);
  assertSave(modified);
  const changes = [];
  for (let i = 0; i < SLOT_COUNT; i += 1) {
    const before = readSlot(original, i);
    const after = readSlot(modified, i);
    const slotChanges = [];
    compare(slotChanges, "Name", before.displayName, after.displayName);
    compare(slotChanges, "Maximum hearts", before.maxHearts, after.maxHearts);
    compare(slotChanges, "Current hearts", before.currentHearts, after.currentHearts);
    compare(slotChanges, "Rupees", before.rupees, after.rupees);
    compare(slotChanges, "Wallet", before.walletCapacity, after.walletCapacity);
    compare(slotChanges, "Maximum magic", before.maxMagic, after.maxMagic);
    compare(slotChanges, "Current magic", before.currentMagic, after.currentMagic);
    compare(slotChanges, "Arrows", before.arrows, after.arrows);
    compare(slotChanges, "Maximum arrows", before.maxArrows, after.maxArrows);
    compare(slotChanges, "Bombs", before.bombs, after.bombs);
    compare(slotChanges, "Maximum bombs", before.maxBombs, after.maxBombs);
    for (const [key, field] of Object.entries(INVENTORY)) {
      compare(slotChanges, field.label, labelFor(field.values, before.inventory[key]), labelFor(field.values, after.inventory[key]));
    }
    for (const [key, field] of Object.entries(EQUIPMENT)) {
      compare(slotChanges, field.label, labelFor(field.values, before.equipment[key]), labelFor(field.values, after.equipment[key]));
    }
    if (slotChanges.length) changes.push({ slotIndex: i, name: after.displayName || `Quest Log ${i + 1}`, changes: slotChanges });
  }
  return changes;
}

export function walletCapacity(level) {
  return [200, 1000, 5000][level] ?? 200;
}

function writePlayerName(bytes, slotIndex, value) {
  const name = sanitizeName(value);
  const displayOffset = DISPLAY_NAME_OFFSETS[slotIndex];
  bytes.fill(0, displayOffset, displayOffset + 18);
  for (let i = 0; i < name.length; i += 1) {
    bytes[displayOffset + i * 2] = name.charCodeAt(i);
    bytes[displayOffset + i * 2 + 1] = 0;
  }

  const base = slotIndex * SLOT_SIZE;
  for (const relativeOffset of INTERNAL_NAME_OFFSETS) {
    const offset = base + relativeOffset;
    bytes.fill(0, offset, offset + 17);
    for (let i = 0; i < name.length; i += 1) bytes[offset + i] = name.charCodeAt(i);
  }
}

function readDisplayName(bytes, slotIndex) {
  const offset = DISPLAY_NAME_OFFSETS[slotIndex];
  let output = "";
  for (let i = 0; i < 9; i += 1) {
    const code = bytes[offset + i * 2] | (bytes[offset + i * 2 + 1] << 8);
    if (code === 0) break;
    output += String.fromCharCode(code);
  }
  return output;
}

function sanitizeName(value) {
  const name = String(value ?? "").trim();
  if (!name) throw new Error("Player name cannot be empty.");
  if (name.length > NAME_MAX_LENGTH) throw new Error(`Player name is limited to ${NAME_MAX_LENGTH} characters.`);
  if (!/^[\x20-\x7E]+$/.test(name)) throw new Error("Player name must use basic ASCII characters.");
  return name;
}

function readAscii(bytes, offset, length) {
  let output = "";
  for (let i = 0; i < length; i += 1) {
    const value = bytes[offset + i];
    if (value === 0) break;
    output += String.fromCharCode(value);
  }
  return output;
}

function heartsToQuarterUnits(value, label) {
  const parsed = Number(value);
  if (!Number.isFinite(parsed) || parsed < 0.25 || parsed > 20 || Math.round(parsed * 4) !== parsed * 4) {
    throw new Error(`${label} must be between 0.25 and 20 in quarter-heart increments.`);
  }
  return parsed * 4;
}

function integerInRange(value, min, max, label) {
  const parsed = Number(value);
  if (!Number.isInteger(parsed) || parsed < min || parsed > max) {
    throw new Error(`${label} must be a whole number from ${min} to ${max}.`);
  }
  return parsed;
}

function compare(changes, label, before, after) {
  if (before !== after) changes.push({ label, before, after });
}

function labelFor(values, id) {
  return values.find(([value]) => value === id)?.[1] ?? `Unknown 0x${id.toString(16).toUpperCase().padStart(2, "0")}`;
}

function assertSlotIndex(slotIndex) {
  if (!Number.isInteger(slotIndex) || slotIndex < 0 || slotIndex >= SLOT_COUNT) {
    throw new RangeError("Quest-log index must be 0, 1, or 2.");
  }
}

function readU16BE(bytes, offset) {
  return (bytes[offset] << 8) | bytes[offset + 1];
}

function writeU16BE(bytes, offset, value) {
  bytes[offset] = (value >>> 8) & 0xFF;
  bytes[offset + 1] = value & 0xFF;
}

function readU32BE(bytes, offset) {
  return ((bytes[offset] * 0x1000000) + (bytes[offset + 1] << 16) + (bytes[offset + 2] << 8) + bytes[offset + 3]) >>> 0;
}

function writeU32BE(bytes, offset, value) {
  bytes[offset] = (value >>> 24) & 0xFF;
  bytes[offset + 1] = (value >>> 16) & 0xFF;
  bytes[offset + 2] = (value >>> 8) & 0xFF;
  bytes[offset + 3] = value & 0xFF;
}
