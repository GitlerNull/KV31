import json
import struct
from pathlib import Path

files = [
    Path("app/src/main/assets/models/FIZI4KA.glb"),
    Path("app/src/main/assets/models/FIZI4KA_throw.glb"),
]

for path in files:
    print("\n" + "=" * 60)
    print(path)
    print("=" * 60)

    with path.open("rb") as f:
        header = f.read(12)
        magic, version, length = struct.unpack("<4sII", header)

        if magic != b"glTF":
            print("ОШИБКА: это не GLB")
            continue

        while f.tell() < length:
            chunk_length, chunk_type = struct.unpack("<II", f.read(8))
            chunk = f.read(chunk_length)

            if chunk_type == 0x4E4F534A:
                gltf = json.loads(chunk.decode("utf-8"))
                break

    nodes = gltf.get("nodes", [])
    skins = gltf.get("skins", [])
    animations = gltf.get("animations", [])
    accessors = gltf.get("accessors", [])

    parents = {}
    for i, node in enumerate(nodes):
        for child in node.get("children", []):
            parents[child] = i

    print("Nodes:", len(nodes))
    print("Skins:", len(skins))
    print("Animations:", len(animations))

    for i, skin in enumerate(skins):
        joints = skin.get("joints", [])
        print(f"\nSKIN {i}: bones={len(joints)}")
        for index in joints:
            node = nodes[index]
            parent = parents.get(index, -1)
            parent_name = nodes[parent].get("name", "?") if parent >= 0 else "ROOT"
            print(f"  {index:3} {node.get('name', '?')} | parent={parent_name}")

    for i, anim in enumerate(animations):
        print(f"\nANIMATION {i}: {anim.get('name', '?')}")
        print("  channels:", len(anim.get("channels", [])))
        print("  samplers:", len(anim.get("samplers", [])))

        for sampler in anim.get("samplers", []):
            accessor = accessors[sampler["input"]]
            print(
                "  time:",
                accessor.get("min"),
                "->",
                accessor.get("max")
            )

    print("\nFile size:", path.stat().st_size)
