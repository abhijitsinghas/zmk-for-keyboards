#!/usr/bin/env python3
from pathlib import Path

OUT = Path("build/oled-mockups")
OUT.mkdir(parents=True, exist_ok=True)

W, H = 128, 32
SCALE = 4

BT = [
    "000100000",
    "000110000",
    "000101000",
    "100100100",
    "010101000",
    "001110000",
    "000100000",
    "001110000",
    "010101000",
    "100100100",
    "000101000",
    "000110000",
    "000100000",
]
CURSOR = [
    "10000000000",
    "11000000000",
    "10100000000",
    "10010000000",
    "10001000000",
    "10000100000",
    "10000010000",
    "10011111000",
    "11001000000",
    "01001000000",
    "00100100000",
    "00100100000",
    "00011000000",
]
USB = [
    "0001000",
    "0001000",
    "0111110",
    "0001000",
    "0001000",
    "0101010",
    "1001001",
    "0001000",
    "0001000",
]
GEAR = [
    "0010100",
    "1111111",
    "0101010",
    "1111111",
    "0011100",
    "0110110",
    "0011100",
]
HOME = [
    "0001000",
    "0011100",
    "0111110",
    "1111111",
    "0111110",
    "0110110",
    "0110110",
    "0111110",
]

def px_icon(rows, x, y, scale=1):
    parts = []
    for yy, row in enumerate(rows):
        for xx, bit in enumerate(row):
            if bit == "1":
                parts.append(f'<rect x="{x+xx*scale}" y="{y+yy*scale}" width="{scale}" height="{scale}" fill="white"/>')
    return "\n".join(parts)

def text(s, x, y, size=9, anchor="start"):
    return f'<text x="{x}" y="{y}" fill="white" font-family="monospace" font-size="{size}" text-anchor="{anchor}" dominant-baseline="hanging">{s}</text>'

def battery(percent="85%", charging=False):
    x, y, bw, bh, capw, caph = 88, 0, 36, 13, 3, 7
    label = ("⚡" if charging else "") + percent
    return f'''
<rect x="{x}" y="{y}" width="{bw}" height="{bh}" rx="2" ry="2" fill="none" stroke="white" stroke-width="1"/>
<rect x="{x+bw+1}" y="{y+3}" width="{capw}" height="{caph}" rx="1" fill="white"/>
<text x="{x+bw/2}" y="{y+2}" fill="white" font-family="monospace" font-size="8" text-anchor="middle" dominant-baseline="hanging">{label}</text>
'''

def layer_icon(kind):
    if kind == "home":
        return px_icon(HOME, 60, 21)
    if kind == "num":
        return text("123", 64, 21, 9, "middle")
    if kind == "sym":
        return text("{}", 64, 21, 9, "middle")
    if kind == "mouse":
        return px_icon(CURSOR, 59, 18)
    if kind == "cfg":
        return px_icon(GEAR, 61, 21)
    return ""

def frame(content):
    return f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W*SCALE}" height="{H*SCALE}" viewBox="0 0 {W} {H}">
<rect width="{W}" height="{H}" fill="black"/>
{content}
</svg>
'''

def save(name, content):
    path = OUT / f"{name}.svg"
    path.write_text(frame(content))
    return path

# Left central, BLE profile connected
save("left-ble-base", "\n".join([
    px_icon(BT, 0, 0),
    text("Mac ✓", 12, 1, 9),
    battery("85%", charging=False),
    layer_icon("home"),
]))

# Left central, USB active + charging
save("left-usb-charging-num", "\n".join([
    px_icon(USB, 1, 1),
    battery("85%", charging=True),
    layer_icon("num"),
]))

# Right peripheral, link connected + local charging battery. No layer is shown on the
# peripheral because we are intentionally not syncing extra layer state to the right side.
save("right-connected", "\n".join([
    px_icon(BT, 0, 0),
    text("✓", 12, 1, 9),
    battery("72%", charging=True),
]))

# Layer icon contact sheet for visual check
icons = []
labels = [("home", "BASE"), ("num", "123"), ("sym", "{}"), ("mouse", "MOUSE"), ("cfg", "CFG")]
for i, (kind, label) in enumerate(labels):
    x = i * 25
    icons.append(f'<g transform="translate({x},0)"><rect x="0" y="0" width="24" height="31" fill="black" stroke="white" stroke-width="0.3"/>')
    if kind == "home": icons.append(px_icon(HOME, x+8, 5))
    if kind == "num": icons.append(text("123", x+12, 6, 7, "middle"))
    if kind == "sym": icons.append(text("{}", x+12, 6, 8, "middle"))
    if kind == "mouse": icons.append(px_icon(CURSOR, x+7, 3))
    if kind == "cfg": icons.append(px_icon(GEAR, x+9, 6))
    icons.append(text(label, x+12, 22, 4, "middle"))
    icons.append('</g>')
save("layer-icons", "\n".join(icons))

print(f"Wrote SVG mockups to {OUT}")
