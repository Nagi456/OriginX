# Windows allocation metadata inspection

`inspect_process_memory.cjs` enumerates committed regions using VirtualQueryEx. It reads region/file-mapping metadata, not target memory contents; it does not inject, suspend or write to the target. It accepts an explicit PID and closes its query handle after capture. Reports contain mapped file basenames and allocation addresses, not full mapped paths.

Koffi is a tool dependency installed outside the source/game directory. For this Windows session it was installed (prebuilt package, no install scripts) under `%TEMP%/myclient-memory-2026-11-08/inspector`.

```powershell
npm.cmd install --prefix "$env:TEMP\myclient-memory-2026-11-08\inspector" --ignore-scripts --no-audit --no-fund koffi
$env:KOFFI_MODULE_PATH = "$env:TEMP\myclient-memory-2026-11-08\inspector\node_modules\koffi"
node source/tools/inspect_process_memory.cjs --self "$env:TEMP\memory-self.json"
# Obtain the current Minecraft PID; do not reuse an old PID after the game exits.
node source/tools/inspect_process_memory.cjs <PID> "$env:TEMP\minecraft-memory-regions.json"
```

Results distinguish MEM_PRIVATE / MEM_MAPPED / MEM_IMAGE committed regions. They are **not working-set residency**, and reserved virtual space is excluded. Image/mapped copy-on-write pages keep their region type. A large private allocation alone does not identify its allocating game/module; compare matched baseline/startup/world captures before attributing ownership. Live mappings may change while enumerating. `complete=false` or a terminal error other than the normal address-space end means an incomplete capture.

Validation on 2026-11-08: Node syntax check passed; self-inspection completed across 557 regions / 376 committed regions, positive private and image totals, terminal error 87 at end of address space. This validates the x64 structure and query path, not Minecraft allocation ownership. Minecraft had already exited, so the tool was not used to manufacture a live-game result.

API contracts: [VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex), [Koffi output parameters](https://koffi.dev/output), [Koffi composite types](https://koffi.dev/composites).
