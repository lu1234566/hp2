#!/usr/bin/env bash
set -euo pipefail

probe_root="${1:?probe root required}"
report_root="${2:?report root required}"
mkdir -p "$report_root"

engine_dll="$(find "$probe_root" -type f -iname 'Engine.dll' -print -quit || true)"
if [[ -z "$engine_dll" ]]; then
    printf '{"schema":"hp2-engine-animation-exports-v1","found":false,"exports":[]}\n' \
        >"$report_root/engine-animation-exports.json"
    : >"$report_root/engine-animation-disasm.txt"
    exit 0
fi

python3 - "$engine_dll" "$report_root/engine-animation-exports.json" <<'PY'
import hashlib
import json
import pathlib
import struct
import sys

path = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
data = path.read_bytes()

def u16(off): return struct.unpack_from('<H', data, off)[0]
def u32(off): return struct.unpack_from('<I', data, off)[0]
def u64(off): return struct.unpack_from('<Q', data, off)[0]

def cstr(off):
    end = data.find(b'\0', off)
    if end < 0:
        raise ValueError('unterminated export name')
    return data[off:end].decode('ascii', errors='replace')

if len(data) < 0x40 or data[:2] != b'MZ':
    raise SystemExit('Engine.dll is not a PE image')
pe = u32(0x3c)
if pe + 24 > len(data) or data[pe:pe+4] != b'PE\0\0':
    raise SystemExit('invalid PE signature')
coff = pe + 4
machine = u16(coff)
section_count = u16(coff + 2)
optional_size = u16(coff + 16)
opt = coff + 20
magic = u16(opt)
if magic == 0x10b:
    image_base = u32(opt + 28)
    data_dirs = opt + 96
elif magic == 0x20b:
    image_base = u64(opt + 24)
    data_dirs = opt + 112
else:
    raise SystemExit(f'unsupported PE optional header magic 0x{magic:x}')
export_rva = u32(data_dirs)
export_size = u32(data_dirs + 4)
section_table = opt + optional_size
sections = []
for i in range(section_count):
    s = section_table + i * 40
    name = data[s:s+8].split(b'\0', 1)[0].decode('ascii', errors='replace')
    virtual_size = u32(s + 8)
    virtual_address = u32(s + 12)
    raw_size = u32(s + 16)
    raw_ptr = u32(s + 20)
    sections.append((name, virtual_address, max(virtual_size, raw_size), raw_ptr))

def rva_to_off(rva):
    for _name, va, span, raw in sections:
        if va <= rva < va + span:
            off = raw + (rva - va)
            if off >= len(data):
                break
            return off
    if rva < len(data):
        return rva
    raise ValueError(f'RVA 0x{rva:x} is outside file')

result = {
    'schema': 'hp2-engine-animation-exports-v1',
    'found': True,
    'sha256': hashlib.sha256(data).hexdigest(),
    'machine': machine,
    'image_base': image_base,
    'export_rva': export_rva,
    'export_size': export_size,
    'exports': [],
}
if export_rva:
    exp = rva_to_off(export_rva)
    ordinal_base = u32(exp + 16)
    function_count = u32(exp + 20)
    name_count = u32(exp + 24)
    functions_rva = u32(exp + 28)
    names_rva = u32(exp + 32)
    ordinals_rva = u32(exp + 36)
    funcs_off = rva_to_off(functions_rva)
    names_off = rva_to_off(names_rva)
    ords_off = rva_to_off(ordinals_rva)
    function_rvas = [u32(funcs_off + i * 4) for i in range(function_count)]
    code_rvas = sorted({r for r in function_rvas if r and not (export_rva <= r < export_rva + export_size)})
    needles = ('uanimation', 'umeshanimation', 'analogtrack', 'motionchunk')
    for i in range(name_count):
        name_rva = u32(names_off + i * 4)
        name = cstr(rva_to_off(name_rva))
        if not any(n in name.lower() for n in needles):
            continue
        ordinal_index = u16(ords_off + i * 2)
        if ordinal_index >= len(function_rvas):
            continue
        rva = function_rvas[ordinal_index]
        forwarded = export_rva <= rva < export_rva + export_size
        greater = [candidate for candidate in code_rvas if candidate > rva]
        next_rva = greater[0] if greater else rva + 2048
        stop_rva = min(next_rva, rva + 2048)
        result['exports'].append({
            'name': name,
            'ordinal': ordinal_base + ordinal_index,
            'rva': rva,
            'va': image_base + rva,
            'stop_va': image_base + stop_rva,
            'forwarded': forwarded,
        })
result['exports'].sort(key=lambda item: (item['rva'], item['name']))
out.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
PY

: >"$report_root/engine-animation-disasm.txt"
python3 - "$report_root/engine-animation-exports.json" <<'PY' | \
while IFS=$'\t' read -r start stop name; do
import json
import pathlib
import sys
j = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding='utf-8'))
for item in j.get('exports', []):
    low = item['name'].lower()
    if item.get('forwarded') or 'serialize' not in low or 'animation' not in low:
        continue
    print(f"0x{item['va']:x}\t0x{item['stop_va']:x}\t{item['name']}")
PY
    {
        echo "===== $name [$start,$stop) ====="
        objdump -d -Mintel --start-address="$start" --stop-address="$stop" "$engine_dll" || true
        echo
    } >>"$report_root/engine-animation-disasm.txt"
done

echo "Engine animation export probe complete; original DLL was not copied to the report."
