"""Print a summary of a MyClient .mcrec recording (packet count, length, which packet ids occur).

Usage: python tools/replay_info.py path/to/replay-YYYYMMDD-HHMMSS.mcrec [--first N]
Send me the output (and, if you like, the first few MB of the file) to design the playback stage.
"""
import struct
import sys
from collections import Counter

HEADER = struct.Struct("<8sIIQ16s")  # magic, version, durationSec, startUnixMs, game
NAMES = {  # well-known Bedrock packet ids (good for orientation; unknown ids are shown as numbers)
    2: "PlayStatus", 3: "ServerToClientHandshake", 5: "Disconnect", 6: "ResourcePacksInfo",
    7: "ResourcePackStack", 11: "StartGame", 12: "AddPlayer", 13: "AddActor", 14: "RemoveActor",
    18: "MoveActor", 19: "MovePlayer", 21: "UpdateBlock", 58: "LevelChunk", 69: "NetworkSettings",
    174: "SubChunk", 143: "NetworkSettings", 144: "PlayerAuthInput", 40: "SetActorMotion",
    9: "Text", 10: "SetTime", 39: "SetActorData", 31: "MobEquipment", 85: "Transfer",
}


def varint(data, pos=0):
    value = shift = 0
    while pos < len(data):
        b = data[pos]
        pos += 1
        value |= (b & 0x7F) << shift
        if not b & 0x80:
            return value, pos
        shift += 7
    return None, pos


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    first = 12
    if "--first" in sys.argv:
        first = int(sys.argv[sys.argv.index("--first") + 1])
    with open(sys.argv[1], "rb") as f:
        raw = f.read(HEADER.size)
        magic, version, duration, start, game = HEADER.unpack(raw)
        if magic != b"MCREC001":
            print("Not a MyClient recording (bad magic).")
            return 1
        print(f"format v{version}, stored length {duration}s (0 = not closed cleanly), "
              f"game '{game.rstrip(bytes(1)).decode(errors='replace')}'")
        counts, sizes = Counter(), Counter()  # key: (direction, id); direction "in" = from server, "out" = sent
        total_ms = n = 0
        mark_ms = None
        listing = []
        while True:
            head = f.read(8)
            if len(head) < 8:
                break
            size, delta = struct.unpack("<II", head)
            sent = bool(size & 0x80000000)  # v3: bit 31 = packet the client sent
            size &= 0x7FFFFFFF
            if size == 0:  # v2: the player pressed record here (earlier packets = buffer since joining)
                total_ms += delta
                mark_ms = total_ms
                continue
            data = f.read(size)
            if len(data) < size:
                print("(file ends in the middle of a packet)")
                break
            pid, _ = varint(data)
            pid = (pid & 0x3FF) if pid is not None else -1  # low 10 bits = packet id
            total_ms += delta
            n += 1
            key = ("out" if sent else "in", pid)
            counts[key] += 1
            sizes[key] += size
            if len(listing) < first:
                listing.append((total_ms, key[0], pid, size))
    print(f"{n} packets over {total_ms / 1000:.1f}s")
    if mark_ms is not None:
        print(f"recording starts at {mark_ms / 1000:.1f}s (earlier packets: world data kept since joining)")
    for d, label in (("in", "received"), ("out", "sent")):
        n_d = sum(c for k, c in counts.items() if k[0] == d)
        b_d = sum(b for k, b in sizes.items() if k[0] == d)
        print(f"  {label}: {n_d} packets, {b_d} bytes")
    print("\nfirst packets (ms, dir, id, name, bytes):")
    for ms, d, pid, size in listing:
        print(f"  {ms:8d}  {d:3s}  {pid:4d}  {NAMES.get(pid, '?'):24s} {size}")
    print("\npacket ids by total bytes (dir, id, name, count, bytes):")
    for (d, pid), total in sizes.most_common(25):
        print(f"  {d:3s}  {pid:4d}  {NAMES.get(pid, '?'):24s} {counts[(d, pid)]:7d}  {total}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
