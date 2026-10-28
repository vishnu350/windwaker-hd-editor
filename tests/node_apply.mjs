// Applies one JSON edit object to a save with the reference Node implementation.
// Usage: node node_apply.mjs <in.sav> <out.sav> '<json: {slot?, edits?, repair?}>'
import { readFile, writeFile } from "node:fs/promises";
import { applySlotEdits, repairAllChecksums } from "../vendor/save-format.js";

const [inPath, outPath, json] = process.argv.slice(2);
const bytes = new Uint8Array(await readFile(inPath));
const { slot = 0, edits, repair = false } = JSON.parse(json);

// "edits" is optional so a run can mirror repairAllChecksums on its own.
if (edits) applySlotEdits(bytes, slot, edits);
if (repair) repairAllChecksums(bytes);
await writeFile(outPath, bytes);
