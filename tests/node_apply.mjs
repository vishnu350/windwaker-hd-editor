// Applies one JSON edit object to a save with the reference Node implementation.
// Usage: node node_apply.mjs <in.sav> <out.sav> '<json: {slot?, edits?, repair?}>'
import { readFile, writeFile } from "node:fs/promises";
import { applySlotEdits, repairAllChecksums, updateSlotChecksum } from "./save-format.js";

const [inPath, outPath, json] = process.argv.slice(2);
const bytes = new Uint8Array(await readFile(inPath));
const { slot = 0, edits, repair = false } = JSON.parse(json);

// "edits" is optional so a run can mirror repairAllChecksums on its own.
if (edits) applySlotEdits(bytes, slot, edits);
if (repair) repairAllChecksums(bytes);

// The browser editor does not know about the obtained-variant mask at 0x51 + slot, but the C
// editor maintains it for the three variant ladders. Mirror it here so the parity check keeps
// covering every other byte instead of failing on those three.
// Item slot -> obtained-mask offset. Bottles are absent: they carry contents with a zero mask.
// Slots with more than two accepted values are variant ladders and get the whole mask rewritten;
// single-item slots only have bit 0 touched, since other bits hold unrelated data.
const OWNED_MASK = {
  telescope: { item: 0x3c, mask: 0x51, values: [0xff, 0x20] },
  sail: { item: 0x3d, mask: 0x52, values: [0xff, 0x78, 0x77] },
  windWaker: { item: 0x3e, mask: 0x53, values: [0xff, 0x22] },
  grapplingHook: { item: 0x3f, mask: 0x54, values: [0xff, 0x25] },
  spoilsBag: { item: 0x40, mask: 0x55, values: [0xff, 0x24] },
  boomerang: { item: 0x41, mask: 0x56, values: [0xff, 0x2d] },
  dekuLeaf: { item: 0x42, mask: 0x57, values: [0xff, 0x34] },
  tingleBottle: { item: 0x43, mask: 0x58, values: [0xff, 0x21] },
  pictobox: { item: 0x44, mask: 0x59, values: [0xff, 0x23, 0x26] },
  ironBoots: { item: 0x45, mask: 0x5a, values: [0xff, 0x29] },
  magicArmor: { item: 0x46, mask: 0x5b, values: [0xff, 0x2a] },
  baitBag: { item: 0x47, mask: 0x5c, values: [0xff, 0x2c] },
  bow: { item: 0x48, mask: 0x5d, values: [0xff, 0x27, 0x35, 0x36] },
  bombs: { item: 0x49, mask: 0x5e, values: [0xff, 0x31] },
  deliveryBag: { item: 0x4e, mask: 0x63, values: [0xff, 0x30] },
  hookshot: { item: 0x4f, mask: 0x64, values: [0xff, 0x2f] },
  skullHammer: { item: 0x50, mask: 0x65, values: [0xff, 0x33] },
};

const base = slot * 0xa94;
for (const [key, field] of Object.entries(OWNED_MASK)) {
  const value = edits?.inventory?.[key];
  if (value === undefined) continue;
  const index = field.values.indexOf(value);
  if (index < 0) continue;
  if (field.values.length > 2) {
    bytes[base + field.mask] = index <= 0 ? 0 : (1 << index) - 1;
  } else {
    bytes[base + field.mask] = (bytes[base + field.mask] & ~0x01) | (index > 0 ? 1 : 0);
  }
}

// Masks are written after the reference implementation, so its checksum has to be redone.
if (edits) updateSlotChecksum(bytes, slot);

await writeFile(outPath, bytes);
