// Windows x64 allocation metadata, not a heap/content dump. No injection or target writes.
// Usage: KOFFI_MODULE_PATH=<installed koffi directory> node inspect_process_memory.cjs <pid|--self> [report.json]
// Koffi is a diagnostic-only dependency, installed outside the game/source tree.
const fs = require('node:fs');
const path = require('node:path');
const koffi = require(process.env.KOFFI_MODULE_PATH || 'koffi');
if (process.platform !== 'win32' || process.arch !== 'x64') throw new Error('Windows x64 required');
const target = process.argv[2] === '--self' ? process.pid : Number(process.argv[2]);
if (!Number.isSafeInteger(target) || target <= 0 || target > 0xffffffff) throw new Error('Expected a process ID');

const kernel = koffi.load('kernel32.dll');
const infoType = koffi.struct('MBI64', {
    base: 'uint64_t', allocationBase: 'uint64_t', allocationProtect: 'uint32_t',
    partition: 'uint16_t', padding: 'uint16_t', bytes: 'uint64_t',
    state: 'uint32_t', protect: 'uint32_t', type: 'uint32_t'
});
if (koffi.sizeof(infoType) !== 48) throw new Error('Unexpected Windows x64 memory-info layout');
const open = kernel.func('void* __stdcall OpenProcess(uint32_t access, int inherit, uint32_t pid)');
const close = kernel.func('int __stdcall CloseHandle(void* handle)');
const error = kernel.func('uint32_t __stdcall GetLastError()');
const query = kernel.func('size_t __stdcall VirtualQueryEx(void* process, uintptr_t address, _Out_ MBI64* info, size_t length)');
const mappedName = kernel.func('uint32_t __stdcall K32GetMappedFileNameW(void* process, uintptr_t address, _Out_ uint8_t* buffer, uint32_t chars)');
const handle = open(0x400 | 0x10, 0, target); // query information + mapped-file metadata
if (!handle) throw new Error(`OpenProcess failed (${error()}); process may have exited or denied access`);
const types = {0x20000: 'private', 0x40000: 'mapped', 0x1000000: 'image'};
const totals = {private: 0, mapped: 0, image: 0, other: 0};
const privateAllocations = new Map(), mappedAllocations = new Map(), privateProtection = {};
let address = 0n, regions = 0, committedRegions = 0, terminalError = 0;
try {
    while (address < 0x800000000000n && regions < 1000000) {
        const info = {};
        if (!query(handle, address, info, 48)) { terminalError = error(); break; }
        const size = BigInt(info.bytes), base = BigInt(info.base);
        if (size <= 0n || base + size <= address) throw new Error('Non-advancing memory region');
        regions++;
        if (info.state === 0x1000) { // only MEM_COMMIT; reserved/free virtual space is not committed RAM
            committedRegions++;
            const bytes = Number(size), type = types[info.type] || 'other';
            totals[type] += bytes;
            const allocation = BigInt(info.allocationBase).toString(16);
            if (type === 'private') {
                const item = privateAllocations.get(allocation) || {base: '0x' + allocation, bytes: 0, regions: 0};
                item.bytes += bytes; item.regions++;
                privateAllocations.set(allocation, item);
                const protection = '0x' + info.protect.toString(16);
                privateProtection[protection] = (privateProtection[protection] || 0) + bytes;
            } else if (type === 'mapped' || type === 'image') {
                let item = mappedAllocations.get(allocation);
                if (!item) {
                    const buffer = Buffer.alloc(2048);
                    const chars = mappedName(handle, base, buffer, buffer.length / 2);
                    item = {base: '0x' + allocation, type, file: chars ? path.win32.basename(buffer.toString('utf16le', 0, chars * 2)) : '(unknown)', bytes: 0};
                    mappedAllocations.set(allocation, item);
                }
                item.bytes += bytes;
            }
        }
        address = base + size;
    }
} finally { close(handle); }
const report = {
    at: new Date().toISOString(), pid: target, regions, committedRegions, terminalError,
    complete: terminalError === 87 || address >= 0x800000000000n,
    committedBytesByType: totals, privateProtection,
    largestPrivateAllocations: [...privateAllocations.values()].sort((a,b) => b.bytes-a.bytes).slice(0,30),
    largestMappedAllocations: [...mappedAllocations.values()].sort((a,b) => b.bytes-a.bytes).slice(0,30),
    limits: 'Committed region metadata, not resident RAM. Copy-on-write image/mapped pages keep their region type. Private allocation addresses do not identify the allocating module. Live mappings may change during capture.'
};
if (process.argv[3]) fs.writeFileSync(process.argv[3], JSON.stringify(report, null, 2));
console.log(JSON.stringify(report));
