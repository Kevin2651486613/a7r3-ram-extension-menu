#!/usr/bin/env python3
"""Generate 4-bit anti-aliased Chinese/English alpha-mask labels for MD3 demo UI."""
import os
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

TOOLS_DIR = Path(__file__).resolve().parent
SDK_DIR = TOOLS_DIR.parent
OUT_H = SDK_DIR / "src" / "md3_demo_labels.h"

if str(TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(TOOLS_DIR))
from env_config import load_local_env  # noqa: E402

load_local_env(SDK_DIR)

BOLD_CANDIDATES = [
    r"C:\Windows\Fonts\msyhbd.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    r"C:\Windows\Fonts\msyh.ttc",
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/STHeiti Medium.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
]

REG_CANDIDATES = [
    r"C:\Windows\Fonts\msyh.ttc",
    r"C:\Windows\Fonts\simsun.ttc",
    r"C:\Windows\Fonts\simhei.ttf",
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/STHeiti Light.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
]


def resolve_font(env_name: str, candidates: list[str]) -> str:
    env_val = os.environ.get(env_name, "").strip()
    if env_val and Path(env_val).is_file():
        return env_val
    for cand in candidates:
        if Path(cand).is_file():
            return cand
    raise FileNotFoundError(
        f"No CJK font found for {env_name}. Set {env_name} in .env.local or environment."
    )


def get_specs(font_bold: str, font_reg: str) -> list[tuple[str, str, str, int]]:
    return [
        ("LBL_TITLE", "拓展菜单", font_bold, 22),
        ("LBL_SUBTITLE", "Material Design 3 · 纯内存预览", font_reg, 13),
        ("LBL_BADGE_RAM", "纯内存", font_bold, 12),
        ("LBL_ROW0_T", "屏蔽曝光补偿波轮", font_bold, 16),
        ("LBL_ROW0_S", "将曝光补偿拨盘的输入始终固定为0", font_reg, 12),
        ("LBL_ROW1_T", "水平仪与直方图同显", font_bold, 16),
        ("LBL_ROW1_S", "水平仪模式下同步显示实时直方图", font_reg, 12),
        ("LBL_ROW2_T", "相册显示焦点位置", font_bold, 16),
        ("LBL_ROW2_S", "回放照片时在画面上标注对焦点", font_reg, 12),
        ("LBL_ROW3_T", "自定义快门音效", font_bold, 16),
        ("LBL_ROW3_S", "仅在电子快门模式下有效 · 点击进入设置", font_reg, 12),
        ("LBL_STATE_ON", "已开启", font_bold, 12),
        ("LBL_STATE_OFF", "已关闭", font_bold, 12),
        ("LBL_SFX_TITLE", "自定义快门音效", font_bold, 20),
        ("LBL_SFX_SUB", "仅在电子快门(静音拍摄)模式下有效", font_reg, 12),
        ("LBL_SFX_R0_T", "启用自定义快门音效", font_bold, 16),
        ("LBL_SFX_R0_S", "仅在电子快门(静音拍摄)模式下触发", font_reg, 12),
        ("LBL_SFX_R1_T", "音效选择 (/SHUTTER_SFX/)", font_bold, 16),
        ("LBL_SFX_R1_S", "左右切换内置或内存卡 WAV/BIN 音效", font_reg, 12),
        ("LBL_SFX_R2_T", "快门音效音量", font_bold, 16),
        ("LBL_SFX_R2_S", "调节机身扬声器播放音量", font_reg, 12),
        ("LBL_SFX_R3_T", "试听当前音效", font_bold, 16),
        ("LBL_SFX_R3_S", "点击或按确认立即通过机身扬声器试听", font_reg, 12),
        ("LBL_SFX_P0", "内置: 10FPS快门", font_bold, 12),
        ("LBL_SFX_P1", "内置: 连拍清脆", font_bold, 12),
        ("LBL_SFX_P2", "内置: 机械Ka", font_bold, 12),
        ("LBL_VOL_0", "低", font_bold, 13),
        ("LBL_VOL_1", "中", font_bold, 13),
        ("LBL_VOL_2", "高", font_bold, 13),
        ("LBL_BTN_PREVIEW", "试听", font_bold, 13),
        ("LBL_BTN_RESCAN", "刷新SD", font_bold, 13),
        ("LBL_BTN_BACK", "返回上级", font_bold, 14),
        ("LBL_ESHUT_ON", "电子快门:开", font_bold, 12),
        ("LBL_ESHUT_OFF", "电子快门:关", font_bold, 12),
        ("LBL_THEME_PREFIX", "主题:", font_bold, 13),
        ("LBL_CHIP0", "靛蓝", font_bold, 13),
        ("LBL_CHIP1", "琥珀", font_bold, 13),
        ("LBL_CHIP2", "青绿", font_bold, 13),
        ("LBL_PAGE_1_2", "1 / 2", font_bold, 14),
        ("LBL_PAGE_2_2", "2 / 2", font_bold, 14),
        ("LBL_PAGE2_T", "预留拓展页", font_bold, 17),
        ("LBL_PAGE2_S", "后续新增拓展功能将自动排布于此页", font_reg, 12),
        ("LBL_BTN_RESET", "恢复默认", font_bold, 14),
        ("LBL_BTN_DONE", "完成", font_bold, 14),
    ]


def rasterize_alpha4(text: str, font_path: str, size: int) -> tuple[int, int, list[int]]:
    scale = 2
    font = ImageFont.truetype(font_path, size * scale)
    probe = Image.new("L", (16, 16), 0)
    d = ImageDraw.Draw(probe)
    bbox = d.textbbox((0, 0), text, font=font)
    tw = max(2, bbox[2] - bbox[0] + 6)
    th = max(2, bbox[3] - bbox[1] + 6)
    rw = (tw + scale - 1) // scale
    rh = (th + scale - 1) // scale

    img = Image.new("L", (rw * scale, rh * scale), 0)
    draw = ImageDraw.Draw(img)
    draw.text((3 - bbox[0], 3 - bbox[1]), text, fill=255, font=font)
    small = img.resize((rw, rh), Image.Resampling.LANCZOS)

    alphas = [((v * 15 + 127) // 255) for v in small.getdata()]
    min_x, max_x = rw, -1
    min_y, max_y = rh, -1
    for y in range(rh):
        for x in range(rw):
            if alphas[y * rw + x] > 0:
                if x < min_x: min_x = x
                if x > max_x: max_x = x
                if y < min_y: min_y = y
                if y > max_y: max_y = y
    if max_x < min_x:
        return 1, 1, [0]

    w = max_x - min_x + 1
    h = max_y - min_y + 1
    cropped = []
    for y in range(min_y, max_y + 1):
        row_off = y * rw
        for x in range(min_x, max_x + 1):
            cropped.append(alphas[row_off + x])
    return w, h, cropped


def main() -> int:
    font_bold = resolve_font("A7R3_FONT_BOLD", BOLD_CANDIDATES)
    font_reg = resolve_font("A7R3_FONT_REG", REG_CANDIDATES)
    specs = get_specs(font_bold, font_reg)

    lines = [
        "#ifndef MD3_DEMO_LABELS_H",
        "#define MD3_DEMO_LABELS_H",
        "",
        "#include <stdint.h>",
        "",
        "struct md3_label {",
        "    uint16_t w;",
        "    uint16_t h;",
        "    const uint8_t *alpha;",
        "};",
        "",
    ]
    total_bytes = 0
    for name, text, fpath, fsize in specs:
        w, h, alphas = rasterize_alpha4(text, fpath, fsize)
        arr_name = f"g_{name.lower()}_alpha"
        total_bytes += len(alphas)
        lines.append(f"/* {text} ({w}x{h}) */")
        lines.append(f"static const uint8_t {arr_name}[{len(alphas)}] = {{")
        for i in range(0, len(alphas), 24):
            chunk = ",".join(f"{v:2d}" for v in alphas[i:i + 24])
            lines.append(f"    {chunk},")
        lines.append("};")
        lines.append(f"static const struct md3_label {name} = {{ {w}u, {h}u, {arr_name} }};")
        lines.append("")

    lines.append("#endif /* MD3_DEMO_LABELS_H */")
    OUT_H.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"[+] Generated {OUT_H} ({len(specs)} labels, {total_bytes} alpha bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
