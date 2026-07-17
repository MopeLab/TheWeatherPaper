# tools/make_font_map.py

from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

GLYPH_TEXT_FILE = PROJECT_ROOT / "assets" / "fonts" / "gb2312.txt"
OUTPUT_MAP_FILE = PROJECT_ROOT / "assets" / "fonts" / "gb2312.map"


def main() -> None:
    if not GLYPH_TEXT_FILE.exists():
        raise FileNotFoundError(
            f"找不到字符文件：{GLYPH_TEXT_FILE}"
        )

    text = GLYPH_TEXT_FILE.read_text(encoding="utf-8")

    # 排除仅用于排版 glyphs.txt 的控制字符。
    codepoints = {
        ord(character)
        for character in text
        if character not in {"\r", "\n", "\t"}
    }

    # 默认加入可打印 ASCII，保证英文、数字和常用符号可用。
    ascii_start = 32
    ascii_end = 126

    extra_codepoints = sorted(
        codepoint
        for codepoint in codepoints
        if not ascii_start <= codepoint <= ascii_end
    )

    tokens = [f"{ascii_start}-{ascii_end}"]
    tokens.extend(
        f"${codepoint:X}"
        for codepoint in extra_codepoints
    )

    OUTPUT_MAP_FILE.write_text(
        ", ".join(tokens) + ",\n",
        encoding="ascii"
    )

    print(f"读取字符数：{len(text)}")
    print(f"唯一字形数：{ascii_end - ascii_start + 1 + len(extra_codepoints)}")
    print(f"已生成：{OUTPUT_MAP_FILE}")


if __name__ == "__main__":
    main()
