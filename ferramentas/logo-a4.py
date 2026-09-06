#!/usr/bin/env python3
"""Recorta o C da PNG stacked e gera o A4 do LVGL 9 (const, sem decoder)."""

from __future__ import annotations

from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parents[1]
STACKED = REPO / "assets" / "branding" / "celer" / "celer_logo_stacked_dark.png"
MARK = REPO / "assets" / "branding" / "celer" / "mark.png"
OUT_C = REPO / "firmware" / "ribanense-esp" / "components" / "ui" / "generated" / "logo_c.c"
OUT_H = REPO / "firmware" / "ribanense-esp" / "components" / "ui" / "generated" / "logo_c.h"

# Tela 240 px; 40 px de ar em cada lado.
MARK_W = 160
BG_THR = 18
SPLIT_Y = 365


def _is_bg(px: tuple[int, int, int, int]) -> bool:
    r, g, b, a = px
    if a < 10:
        return True
    return r <= BG_THR and g <= BG_THR and b <= BG_THR


def crop_mark(src: Image.Image) -> Image.Image:
    rgba = src.convert("RGBA")
    w, h = rgba.size
    y1 = min(SPLIT_Y, h)
    xs: list[int] = []
    ys: list[int] = []
    pix = rgba.load()
    for y in range(y1):
        for x in range(w):
            if not _is_bg(pix[x, y]):
                xs.append(x)
                ys.append(y)
    if not xs:
        raise RuntimeError("C nao encontrado na PNG de origem")
    box = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
    return rgba.crop(box)


def pack_a4(img: Image.Image) -> tuple[int, int, int, bytes]:
    img = img.convert("RGBA")
    w, h = img.size
    stride = (w + 1) // 2
    pix = img.load()
    out = bytearray(stride * h)
    for y in range(h):
        row = y * stride
        for x in range(w):
            r, g, b, a = pix[x, y]
            cover = a
            if cover >= 10 and r <= BG_THR and g <= BG_THR and b <= BG_THR:
                cover = 0
            nibble = cover >> 4
            if (x & 1) == 0:
                out[row + (x >> 1)] = nibble << 4
            else:
                out[row + (x >> 1)] |= nibble
    return w, h, stride, bytes(out)


def write_c(w: int, h: int, stride: int, data: bytes) -> None:
    OUT_C.parent.mkdir(parents=True, exist_ok=True)
    OUT_H.write_text(
        "#pragma once\n\n"
        "#include \"lvgl.h\"\n\n"
        "extern const lv_image_dsc_t logo_c;\n",
        encoding="utf-8",
    )
    lines = [
        "/* Gerado por ferramentas/logo-a4.py. Nao editar a mao. */",
        "#include \"logo_c.h\"",
        "",
        "const LV_ATTRIBUTE_MEM_ALIGN uint8_t logo_c_map[] = {",
    ]
    row: list[str] = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02x}")
        if len(row) == 16 or i == len(data) - 1:
            lines.append("    " + ", ".join(row) + ",")
            row = []
    lines.extend(
        [
            "};",
            "",
            "const lv_image_dsc_t logo_c = {",
            "    .header.magic = LV_IMAGE_HEADER_MAGIC,",
            "    .header.cf = LV_COLOR_FORMAT_A4,",
            "    .header.flags = 0,",
            f"    .header.w = {w},",
            f"    .header.h = {h},",
            f"    .header.stride = {stride},",
            "    .data_size = sizeof(logo_c_map),",
            "    .data = logo_c_map,",
            "};",
            "",
        ]
    )
    OUT_C.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    if STACKED.is_file():
        mark = crop_mark(Image.open(STACKED))
    elif MARK.is_file():
        mark = Image.open(MARK).convert("RGBA")
    else:
        raise FileNotFoundError(STACKED)

    MARK.parent.mkdir(parents=True, exist_ok=True)
    mark.save(MARK, format="PNG")

    scaled = mark.resize(
        (MARK_W, max(1, round(mark.height * MARK_W / mark.width))),
        Image.Resampling.LANCZOS,
    )
    w, h, stride, data = pack_a4(scaled)
    write_c(w, h, stride, data)
    print(f"mark {mark.size[0]}x{mark.size[1]} -> A4 {w}x{h} stride={stride} {len(data)} B")
    print(f"escreveu {MARK}")
    print(f"escreveu {OUT_C}")


if __name__ == "__main__":
    main()
