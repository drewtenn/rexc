#!/usr/bin/env python3
"""Generate the Rexy cover image used by the PDF and EPUB build."""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


WIDTH = 1024
HEIGHT = 1536


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    candidates = [
        "/System/Library/Fonts/Supplemental/Arial Bold.ttf" if bold else "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/System/Library/Fonts/Supplemental/Helvetica.ttc",
        "/Library/Fonts/Arial.ttf",
    ]
    for candidate in candidates:
        path = Path(candidate)
        if path.exists():
            return ImageFont.truetype(str(path), size=size)
    return ImageFont.load_default()


def centered(draw: ImageDraw.ImageDraw, y: int, text: str, font_obj: ImageFont.ImageFont) -> int:
    box = draw.textbbox((0, 0), text, font=font_obj)
    width = box[2] - box[0]
    draw.text(((WIDTH - width) // 2, y), text, font=font_obj, fill=(18, 24, 32))
    return y + (box[3] - box[1])


def centered_at(draw: ImageDraw.ImageDraw, y: int, text: str, font_obj: ImageFont.ImageFont) -> None:
    box = draw.textbbox((0, 0), text, font=font_obj)
    width = box[2] - box[0]
    height = box[3] - box[1]
    draw.text(((WIDTH - width) // 2, y - height // 2), text, font=font_obj, fill=(18, 24, 32))


def connector(draw: ImageDraw.ImageDraw, x: int, y0: int, y1: int) -> None:
    draw.line((x, y0, x, y1 - 8), fill=(18, 24, 32), width=2)
    draw.polygon(((x - 6, y1 - 8), (x + 6, y1 - 8), (x, y1)), fill=(18, 24, 32))


def main() -> int:
    output = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("docs/cover-art.png")
    output.parent.mkdir(parents=True, exist_ok=True)

    image = Image.new("RGB", (WIDTH, HEIGHT), (246, 246, 242))
    draw = ImageDraw.Draw(image)

    title = font(132, bold=True)
    subtitle = font(42)
    body = font(30)
    mono = font(28)
    small = font(24)

    draw.line((145, 190, WIDTH - 145, 190), fill=(18, 24, 32), width=3)
    y = centered(draw, 260, "Rexy", title)
    y = centered(draw, y + 38, "Building a Systems Language Compiler", subtitle)
    y = centered(draw, y + 10, "for Drunix", subtitle)
    draw.line((145, y + 62, WIDTH - 145, y + 62), fill=(18, 24, 32), width=3)

    panel_x0 = 205
    panel_y0 = y + 165
    panel_x1 = WIDTH - 205
    panel_y1 = panel_y0 + 450
    draw.rounded_rectangle((panel_x0, panel_y0, panel_x1, panel_y1), radius=18, outline=(18, 24, 32), width=3)

    pipeline = [
        "source.rx",
        "tokens",
        "AST",
        "typed IR",
        "x86 assembly",
        "Drunix ELF",
    ]
    step_gap = 66
    first_step_y = panel_y0 + 62
    step_centers = [first_step_y + index * step_gap for index in range(len(pipeline))]
    for index, label in enumerate(pipeline):
        centered_at(draw, step_centers[index], label, mono)
        if index != len(pipeline) - 1:
            connector(draw, WIDTH // 2, step_centers[index] + 24, step_centers[index + 1] - 24)

    centered(draw, panel_y1 + 95, "My journey in writing a compiler for my OS", body)
    centered(draw, panel_y1 + 150, "A companion to the Drunix book", body)

    draw.line((200, HEIGHT - 210, WIDTH - 200, HEIGHT - 210), fill=(18, 24, 32), width=2)
    centered(draw, HEIGHT - 165, "Drew Tennenbaum", body)
    centered(draw, HEIGHT - 110, "2026", small)

    image.save(output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
