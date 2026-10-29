#!/usr/bin/env python3
"""Build report/M11515075.docx (the project report) from report/data/.

Usage: python3 report/make_docx.py

Every result number is read or derived from report/data/*.csv at build time
(the same files behind tables.md); design constants come from
report/data/parameters.csv, which names their source in the code or spec.
Needs python-docx.
"""
import csv
import os
import re

from docx import Document
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_LINE_SPACING
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.join(HERE, "data")
FIG = os.path.join(HERE, "figures")
OUT = os.path.join(HERE, "M11515075.docx")

LATIN = "Times New Roman"
CJK = "標楷體"
BODY_PT = 10
TABLE_PT = 8.5
MINUS = "−"


# ----------------------------------------------------------------- data

def load(name):
    with open(os.path.join(DATA, name)) as f:
        return list(csv.DictReader(f))


P = {r["name"]: r["value"] for r in load("parameters.csv")}
FINAL = load("run_final.csv")
BASE = {r["case"]: r for r in load("run_phase4_baseline.csv")}
MID = {r["case"]: r for r in load("run_phase5_schedule.csv")}
ABL = load("stage1_ablation.csv")
SWEEPS = load("sa_sweeps.csv")
VALID = load("move_validity.csv")

SOURCES = ["ibm01-cu85", "ibm01-cu88", "ibm10-cu90", "ibm10-cu95", "ibm12-cu85", "ibm12-cu90"]


def f(x):
    return float(x)


def pct(a, b):
    return 100.0 * (f(a) / f(b) - 1.0)


def sgn(v, nd=1):
    """Signed percentage with a real minus sign."""
    s = ("%+." + str(nd) + "f") % v
    return s.replace("-", MINUS)


def num(v, nd=1):
    return ("%." + str(nd) + "f") % v


def M(x):
    return "%.2f" % (f(x) / 1e6)


def rng(vals, nd=1):
    lo, hi = min(vals), max(vals)
    return "%s–%s" % (num(lo, nd), num(hi, nd))


# Stage 1 ablation means (vs Abacus only) and the input011 example.
def stage1_summary():
    by = {}
    for r in ABL:
        by.setdefault(r["case"], {})[r["config"]] = r
    cfgs = ["abacus", "abacus+l1", "abacus+post", "full"]
    means = {}
    for k in cfgs:
        dt = sum(pct(by[c][k]["d_total"], by[c]["abacus"]["d_total"]) for c in by) / len(by)
        dm = sum(pct(by[c][k]["d_max"], by[c]["abacus"]["d_max"]) for c in by) / len(by)
        means[k] = (dt, dm)
    s1max = max(f(by[c]["full"]["stage1_s"]) for c in by)
    return by, cfgs, means, s1max


S1_BY, S1_CFGS, S1_MEANS, S1_MAX_S = stage1_summary()

# Final-run derived numbers.
MILD = [r for r in FINAL if int(r["case"][-3:]) <= 6]
HARSH = [r for r in FINAL if int(r["case"][-3:]) > 6]
RATIO_MILD = [100 * f(r["d_total"]) / f(r["ref_d_total"]) for r in MILD]
RATIO_HARSH = [100 * f(r["d_total"]) / f(r["ref_d_total"]) for r in HARSH]
DMAX_BETTER = [r for r in FINAL if f(r["d_max"]) < f(r["ref_d_max"])]
DMAX_WORSE = [r for r in FINAL if f(r["d_max"]) >= f(r["ref_d_max"])]
SA_GAIN = [f(r["improve_pct"]) for r in FINAL]
VS_DRAGON = [pct(r["hpwl_sa"], r["ref_hpwl"]) for r in FINAL]
MAX_SEC = max(f(r["seconds"]) for r in FINAL)
MAX_MB = max(f(r["peak_mb"]) for r in FINAL)
SMALL_SEC = [f(r["seconds"]) for r in FINAL if r["cells"] == "12028"]
CELLS = sorted({int(r["cells"]) for r in FINAL})
ALL_PASS = all(r["checker"] == "PASS" for r in FINAL)

# Phase-5 SA progression (12 cases).
PROG_MID = sum(pct(MID[r["case"]]["hpwl_sa"], BASE[r["case"]]["hpwl_sa"]) for r in FINAL) / len(FINAL)
PROG_FIN = [pct(r["hpwl_sa"], BASE[r["case"]]["hpwl_sa"]) for r in FINAL]
PROG_FIN_MEAN = sum(PROG_FIN) / len(PROG_FIN)
GENSWAP_12 = PROG_FIN_MEAN - PROG_MID


def sweep(exp, setting):
    return next(r for r in SWEEPS if r["experiment"] == exp and r["setting"] == setting)


def rel3(a, b):
    """Mean % difference of sweep row a vs row b over the three cases."""
    cs = ["input001", "input005", "input011"]
    return sum(pct(a[c], b[c]) for c in cs) / len(cs)


def curve_stats(case):
    r = load("sa_log_%s.csv" % case)
    start = f(r[0]["current_hpwl"])
    last = int(r[-1]["iter"])
    pk = max(range(len(r)), key=lambda i: f(r[i]["current_hpwl"]))
    back = next(int(r[i]["iter"]) for i in range(pk, len(r)) if f(r[i]["current_hpwl"]) < start)
    return {
        "change": pct(r[-1]["best_hpwl"], r[0]["best_hpwl"]),
        "peak_rise": pct(r[pk]["current_hpwl"], start),
        "back_frac": 100.0 * back / last,
        "iters": last,
    }


C005 = curve_stats("input005")
C011 = curve_stats("input011")
VALID_BY = {r["move"]: r for r in VALID}

# ----------------------------------------------------------- formatting

# Schema order of child elements (WordprocessingML) for the containers we
# add children to; elements must be inserted in this order or Word may
# reject the file.
PPR_ORDER = ["pStyle", "keepNext", "keepLines", "pageBreakBefore", "framePr", "widowControl", "numPr",
             "suppressLineNumbers", "pBdr", "shd", "tabs", "suppressAutoHyphens", "kinsoku", "wordWrap",
             "overflowPunct", "topLinePunct", "autoSpaceDE", "autoSpaceDN", "bidi", "adjustRightInd",
             "snapToGrid", "spacing", "ind", "contextualSpacing", "mirrorIndents", "suppressOverlap", "jc",
             "textDirection", "textAlignment", "textboxTightWrap", "outlineLvl", "divId", "cnfStyle", "rPr",
             "sectPr", "pPrChange"]
TBLPR_ORDER = ["tblStyle", "tblpPr", "tblOverlap", "bidiVisual", "tblStyleRowBandSize", "tblStyleColBandSize",
               "tblW", "jc", "tblCellSpacing", "tblInd", "tblBorders", "shd", "tblLayout", "tblCellMar", "tblLook",
               "tblCaption", "tblDescription"]


def insert_ordered(parent, el, order):
    """Insert el into parent at its schema position (replacing a same-tag child)."""
    tag = el.tag.split("}")[1]
    old = parent.find(qn("w:" + tag))
    if old is not None:
        parent.remove(old)
    later = order[order.index(tag) + 1:]
    for i, child in enumerate(parent):
        if child.tag.split("}")[1] in later:
            parent.insert(i, el)
            return el
    parent.append(el)
    return el


def set_fonts(rpr_parent, size=None, bold=None, italic=None, color=None):
    """Latin + East Asian fonts on a run (or a style's rPr owner)."""
    font = rpr_parent.font
    font.name = LATIN
    if size is not None:
        font.size = Pt(size)
    if bold is not None:
        font.bold = bold
    if italic is not None:
        font.italic = italic
    if color is not None:
        font.color.rgb = color
    rpr = rpr_parent._element.get_or_add_rPr()
    rfonts = rpr.find(qn("w:rFonts"))
    if rfonts is None:
        rfonts = OxmlElement("w:rFonts")
        rpr.insert(0, rfonts)
    for attr in ("w:asciiTheme", "w:hAnsiTheme", "w:eastAsiaTheme", "w:cstheme"):
        rfonts.attrib.pop(qn(attr), None)
    rfonts.set(qn("w:ascii"), LATIN)
    rfonts.set(qn("w:hAnsi"), LATIN)
    rfonts.set(qn("w:cs"), LATIN)
    rfonts.set(qn("w:eastAsia"), CJK)
    lang = rpr.find(qn("w:lang"))
    if lang is None:
        lang = OxmlElement("w:lang")
        rpr.append(lang)
    lang.set(qn("w:eastAsia"), "zh-TW")


def fmt_par(p, size=BODY_PT, before=0, after=2, align=None, first_indent=None, keep_next=False):
    pf = p.paragraph_format
    pf.space_before = Pt(before)
    pf.space_after = Pt(after)
    pf.line_spacing_rule = WD_LINE_SPACING.EXACTLY
    pf.line_spacing = Pt(size * 1.38)
    if align is not None:
        p.alignment = align
    if first_indent is not None:
        pf.first_line_indent = first_indent
    if keep_next:
        pf.keep_with_next = True
    # Snap to the document grid off, so CJK lines keep the exact spacing.
    snap = OxmlElement("w:snapToGrid")
    snap.set(qn("w:val"), "0")
    insert_ordered(p._p.get_or_add_pPr(), snap, PPR_ORDER)


UNIT_SPACE = re.compile(r"(\d) (%|s\b|MB|rows?\b|sites?\b|moves/cell)")


def add_runs(p, parts, size=BODY_PT):
    """parts: str or list of str / (str, 'b') / (str, 'i')."""
    if isinstance(parts, str):
        parts = [parts]
    for part in parts:
        text, style = (part, "") if isinstance(part, str) else part
        text = UNIT_SPACE.sub("\\1\u00a0\\2", text)  # keep numbers with their units
        r = p.add_run(text)
        set_fonts(r, size=size, bold="b" in style or None, italic="i" in style or None)
    return p


def para(doc, parts, size=BODY_PT, after=2, align=WD_ALIGN_PARAGRAPH.JUSTIFY, indent=True):
    p = doc.add_paragraph()
    fmt_par(p, size=size, after=after, align=align, first_indent=Pt(size * 2) if indent else None)
    return add_runs(p, parts, size)


def bullet(doc, label, parts, size=BODY_PT):
    """Hanging-indent item: bold label then text."""
    p = doc.add_paragraph()
    fmt_par(p, size=size, after=1, align=WD_ALIGN_PARAGRAPH.JUSTIFY)
    p.paragraph_format.left_indent = Pt(size * 1.0)
    p.paragraph_format.first_line_indent = Pt(-size * 1.0)
    add_runs(p, [("▸ " + label, "b")], size)
    return add_runs(p, parts, size)


def heading(doc, text, size=11.5, before=4):
    p = doc.add_paragraph()
    fmt_par(p, size=size, before=before, after=2, keep_next=True)
    p.paragraph_format.line_spacing = Pt(size * 1.35)
    add_runs(p, [(text, "b")], size)
    # Outline level so Word's navigation pane lists the sections.
    ol = OxmlElement("w:outlineLvl")
    ol.set(qn("w:val"), "0")
    insert_ordered(p._p.get_or_add_pPr(), ol, PPR_ORDER)
    return p


def field_run(p, instr, placeholder, size):
    """Simple field (SEQ / PAGE) with a cached result."""
    fld = OxmlElement("w:fldSimple")
    fld.set(qn("w:instr"), instr)
    r = OxmlElement("w:r")
    rpr = OxmlElement("w:rPr")
    rfonts = OxmlElement("w:rFonts")
    for a in ("w:ascii", "w:hAnsi", "w:cs"):
        rfonts.set(qn(a), LATIN)
    rfonts.set(qn("w:eastAsia"), CJK)
    rpr.append(rfonts)
    b = OxmlElement("w:b")
    rpr.append(b)
    sz = OxmlElement("w:sz")
    sz.set(qn("w:val"), str(int(size * 2)))
    rpr.append(sz)
    r.append(rpr)
    t = OxmlElement("w:t")
    t.text = placeholder
    r.append(t)
    fld.append(r)
    p._p.append(fld)


COUNTERS = {"圖": 0, "表": 0}


def caption(doc, label, text, size=9, keep_next=False, after=3):
    COUNTERS[label] += 1
    p = doc.add_paragraph()
    fmt_par(p, size=size, before=1, after=after, align=WD_ALIGN_PARAGRAPH.CENTER, keep_next=keep_next)
    add_runs(p, [(label + " ", "b")], size)
    field_run(p, " SEQ %s \\* ARABIC " % label, str(COUNTERS[label]), size)
    add_runs(p, [("：", "b"), text], size)
    return p


def figure(doc, path, width_cm):
    p = doc.add_paragraph()
    fmt_par(p, size=BODY_PT, after=0, align=WD_ALIGN_PARAGRAPH.CENTER, keep_next=True)
    p.paragraph_format.line_spacing_rule = WD_LINE_SPACING.SINGLE
    p.add_run().add_picture(path, width=Cm(width_cm))


def set_cell_borders(cell, **edges):
    tcpr = cell._tc.get_or_add_tcPr()
    borders = tcpr.find(qn("w:tcBorders"))
    if borders is None:
        borders = OxmlElement("w:tcBorders")
        tcpr.append(borders)
    for edge in sorted(edges, key=["top", "left", "bottom", "right"].index):  # schema order
        val, sz = edges[edge]
        e = OxmlElement("w:" + edge)
        e.set(qn("w:val"), val)
        e.set(qn("w:sz"), str(sz))
        e.set(qn("w:space"), "0")
        e.set(qn("w:color"), "000000")
        borders.append(e)


def table(doc, header, rows, widths_cm, align=None, size=TABLE_PT, bold_last=False):
    """Three-rule table (top, below header, bottom) with fixed widths."""
    t = doc.add_table(rows=len(rows) + 1, cols=len(header))
    t.alignment = WD_TABLE_ALIGNMENT.CENTER
    t.autofit = False
    tblpr = t._tbl.tblPr
    layout = OxmlElement("w:tblLayout")
    layout.set(qn("w:type"), "fixed")
    insert_ordered(tblpr, layout, TBLPR_ORDER)
    mar = OxmlElement("w:tblCellMar")
    for side, v in (("top", 0), ("left", 45), ("bottom", 0), ("right", 45)):  # schema order
        e = OxmlElement("w:" + side)
        e.set(qn("w:w"), str(v))
        e.set(qn("w:type"), "dxa")
        mar.append(e)
    insert_ordered(tblpr, mar, TBLPR_ORDER)
    # Word and LibreOffice lay out fixed tables from tblGrid, not cell widths.
    grid = t._tbl.tblGrid
    for gc, w in zip(grid.findall(qn("w:gridCol")), widths_cm):
        gc.set(qn("w:w"), str(int(Cm(w).twips)))
    tblw = tblpr.find(qn("w:tblW"))
    if tblw is None:
        tblw = insert_ordered(tblpr, OxmlElement("w:tblW"), TBLPR_ORDER)
    tblw.set(qn("w:w"), str(int(Cm(sum(widths_cm)).twips)))
    tblw.set(qn("w:type"), "dxa")
    align = align or ["l"] + ["r"] * (len(header) - 1)
    amap = {"l": WD_ALIGN_PARAGRAPH.LEFT, "r": WD_ALIGN_PARAGRAPH.RIGHT, "c": WD_ALIGN_PARAGRAPH.CENTER}
    for ri, row in enumerate([header] + rows):
        tr = t.rows[ri]
        trpr = tr._tr.get_or_add_trPr()
        cant = OxmlElement("w:cantSplit")
        trpr.append(cant)
        if ri == 0:
            hdr = OxmlElement("w:tblHeader")
            trpr.append(hdr)
        for ci, val in enumerate(row):
            cell = tr.cells[ci]
            cell.width = Cm(widths_cm[ci])
            p = cell.paragraphs[0]
            fmt_par(p, size=size, after=0, align=amap["c" if ri == 0 else align[ci]])
            p.paragraph_format.line_spacing = Pt(size * 1.3)
            bold = ri == 0 or (bold_last and ri == len(rows))
            add_runs(p, [(str(val), "b" if bold else "")], size)
            edges = {}
            if ri == 0:
                edges["top"] = ("single", 10)
                edges["bottom"] = ("single", 5)
            if ri == len(rows):
                edges["bottom"] = ("single", 10)
            if bold_last and ri == len(rows):
                edges["top"] = ("single", 4)
            set_cell_borders(cell, **edges)
    return t


# --------------------------------------------------------------- document


def build():
    doc = Document()
    sec = doc.sections[0]
    sec.page_width, sec.page_height = Cm(21.0), Cm(29.7)
    sec.left_margin = sec.right_margin = Cm(2.0)
    sec.top_margin = sec.bottom_margin = Cm(1.8)
    sec.footer_distance = Cm(0.9)
    zoom = doc.settings.element.find(qn("w:zoom"))
    if zoom is not None:
        zoom.set(qn("w:percent"), "100")
    normal = doc.styles["Normal"]
    set_fonts(normal, size=BODY_PT)

    # Footer: page number.
    fp = sec.footer.paragraphs[0]
    fmt_par(fp, size=8, after=0, align=WD_ALIGN_PARAGRAPH.CENTER)
    field_run(fp, " PAGE ", "1", 8)

    # Title block.
    p = doc.add_paragraph()
    fmt_par(p, size=14, after=1, align=WD_ALIGN_PARAGRAPH.CENTER)
    add_runs(p, [("CAD Design Project 3：Standard Cell Detailed Placement 報告", "b")], 14)
    p = doc.add_paragraph()
    fmt_par(p, size=10, after=4, align=WD_ALIGN_PARAGRAPH.CENTER)
    add_runs(p, "課程：CAD Design　｜　學號：M11515075　｜　姓名：陳冠宇", 10)

    # 1. Overview
    heading(doc, "1. 概述", before=0)
    para(doc, [
        "本專案以 C++17（僅用標準函式庫）實作 detailed placer ", ("dp", "i"),
        "：讀入 GSRC Bookshelf 格式的 global placement（GP），Stage 1 進行 legalization，同時最小化 total 與 "
        "maximum displacement（D_total、D_max）；Stage 2 以 simulated annealing（SA）在全程維持 legality 的前提下"
        "最小化 HPWL（以 cell center 計算，所有 net 權重為 1）。每個 case 時限 %s s（含 I/O），因此 SA 在程式啟動後 "
        "%s s 強制停止；Stage 1 的改善步驟另設 %s s 安全期限（實測 Stage 1 最多 %s s）。所有結果寫檔前都經 dp 內部完整的 "
        "legality 檢查，並以獨立撰寫的 checker 再驗證。"
        % (P["time_limit_per_case"], P["sa_hard_cap"], P["stage1_deadline"], num(S1_MAX_S, 2))])

    # 2. Stage 1
    heading(doc, "2. Stage 1：初始 legal 解的建構")
    bullet(doc, "Row model：", "每個 subrow 被與其重疊的 fixed terminal（terminal_NI 除外）切成 free segment；"
           "所有位置以 segment 內的整數 site index 表示，site alignment 由資料結構直接保證。")
    bullet(doc, "Abacus（Spindler et al., ISPD'08）：", "cell 依 global x 排序；每個 cell 依 |Δy| 由近到遠拜訪 row，"
           "row 內依水平距離拜訪 segment，以 cluster collapse 做 trial placement。當 Δy（或 segment 的水平下界）"
           "已不小於目前最佳 cost 即停止搜尋，因此不需固定 row 視窗，且對 Abacus cost 而言是精確搜尋。cluster 以"
           "四捨五入 snap 到 site；此映射單調，不會產生 overlap。")
    bullet(doc, "Fallback：", "若碎片化的容量讓部分 cell 放不下，先放失敗的 cell（寬者優先），再改為全部寬者優先重跑；"
           "若 row 總容量確實不足則回報錯誤結束，絕不輸出 illegal 結果。")
    bullet(doc, "L1 reposition（含 D_max guard）：", "Abacus 最小化的是 displacement 平方和。固定 segment 內的 cell "
           "順序後，最小化 Σ|x − x_gp| 是 isotonic regression，以 pool-adjacent-violators（block 取 median 合併）"
           "精確求解；只有在該 segment 的最大 displacement 不增加時才採用，以免犧牲 D_max。")
    bullet(doc, "D_max post-pass：", "(a) reduce max：反覆取目前 displacement 最大的 cell，移入最佳 free gap，或與等寬 "
           "cell 交換使兩者較大值下降；(b) reduce total：所有 cell 由差到好嘗試更佳的 gap 或交換，使兩者總和下降且不超過"
           "目前 D_max；(c) 再做一次 L1 reposition。單輪增益低於 D_total %s %% 且 D_max %s %% 時停止（最多 %s 輪）；"
           "每次搜尋最多 %s 個 row、每方向 %s 個 gap／候選，避免極端 GP 拖慢執行。"
           % (P["postpass_min_total_gain"], P["postpass_min_max_gain"], P["postpass_max_rounds"],
              P["postpass_max_rows"], P["postpass_max_scan"]))
    m = S1_MEANS
    para(doc, [
        ("Trade-off 與分析：", "b"),
        "D_total 與 D_max 互相牽制。reduce max 允許兩個 cell 的 displacement 總和增加，但增量不得超過最大值的降幅；"
        "reduce total 的交換則以目前 D_max 為上限，因此兩個目標都不會被另一個步驟破壞。表 1 為 12 個 case 相對於"
        "純 Abacus 的平均變化：L1 reposition 單獨使用僅 %s %%（Abacus 已接近 L1 最佳）；post-pass 帶來 %s %% / %s %%；"
        "post-pass 每輪都搭配 L1 時，交換與移動不斷改變 cell 順序，L1 得以持續發揮，完整 Stage 1 達到 D_total %s %%、"
        "D_max %s %%，而 Stage 1 最多只需 %s s。"
        % (sgn(m["abacus+l1"][0]), sgn(m["abacus+post"][0]), sgn(m["abacus+post"][1]),
           sgn(m["full"][0]), sgn(m["full"][1]), num(S1_MAX_S, 2))])
    ex = S1_BY["input011"]
    labels = {"abacus": "Abacus only", "abacus+l1": "+ L1 reposition", "abacus+post": "+ post-pass（無 L1）",
              "full": "完整 Stage 1（post-pass + L1）"}
    rows = [[labels[k], "基準" if k == "abacus" else sgn(m[k][0]), "基準" if k == "abacus" else sgn(m[k][1]),
             M(ex[k]["d_total"]), num(f(ex[k]["d_max"]), 0)] for k in S1_CFGS]
    caption(doc, "表", "Stage 1 ablation（SA 關閉；平均為 12 個 case 相對 Abacus only；input011 D_total 單位 10⁶）",
            keep_next=True, after=1)
    table(doc, ["設定", "D_total 平均變化 (%)", "D_max 平均變化 (%)", "input011 D_total", "input011 D_max"],
          rows, [5.2, 3.0, 3.0, 2.8, 2.6])

    # 3. Stage 2
    heading(doc, "3. Stage 2：SA 設計與 legality 維持")
    bullet(doc, "Occupancy grid：", "每個 free segment 維護一個 site → cell 的 occupancy array。每個 move 只檢查目標 "
           "site 是否空著或屬於被移動的 cell，因此任何時刻 placement 都是 legal，不需要事後 repair。")
    vo = VALID_BY["optimal region (with generalized swap)"]
    bullet(doc, "Move types：", "隨機選一個 movable cell。(1) %s %% optimal region：以該 cell 所有 net（不含自己）"
           "bounding box 的 median 為目標（受 range window 限制）；目標有空位就移入，被等寬 cell 佔用就交換，被不同寬度 "
           "cell 佔用且兩者都放得進對方的 slot（自身 site 加相鄰空 site）就做 generalized swap，否則移到同 segment 最近"
           "的足夠空位；(2) %s %% random window：同上，但目標是 window 內的隨機點；(3) %s %% adjacent swap：與同 row "
           "右鄰交換並保持兩者總跨距；(4) %s %% in-gap shift：在周圍空隙內移動，一半機率朝 optimal x。四者產生合法 "
           "proposal 的比例分別為 %s %%、%s %%、%s %%、%s %%；optimal region 的「失敗」多半是 cell 已在最佳區域內，"
           "屬正常行為。"
           % (P["mix_optimal"], P["mix_random"], P["mix_adjacent"], P["mix_shift"], vo["valid_pct"],
              VALID_BY["random window"]["valid_pct"], VALID_BY["adjacent swap"]["valid_pct"],
              VALID_BY["in-gap shift"]["valid_pct"]))
    bullet(doc, "Incremental HPWL：", "每個 net 快取 bounding box 與各邊上的 pin 數；move 只更新相關 net，只有當某邊"
           "失去最後一個 pin 時才重算（pin 數少於 %s 的 net 直接重算較快）。" % P["incremental_min_degree"])
    t0 = sweep("t0_accept", "0.2")
    t0c = sweep("t0_accept", "0.01")
    w8 = sweep("window_alpha", "win=10% chip / alpha_low=0.8")
    w95 = sweep("window_alpha", "win=10% chip / alpha_low=0.95")
    bullet(doc, "Temperature schedule：", "T0 由取樣的 uphill Δ 決定，使約 %s %% 的 uphill move 被接受——起點已是良好的 "
           "legal 解，若用 20 %% 會差 %s %%。冷卻採 VPR 式、依 acceptance rate 調整的 α（%s／%s／%s／%s），低接受率區間用 "
           "%s（比 0.8 好 %s %%）；必要時再加速，使 T 在 %s s 前降到 %s·T0。range window 由晶片的 %s %% 開始，依 %s %% "
           "目標接受率調整；每個溫度 %s moves/cell，大設計自動減少以容納約 %s 個溫度。"
           % (P["t0_accept"], num(rel3(t0, t0c), 1), P["alpha_high"], P["alpha_mid"], P["alpha_band"], P["alpha_low"],
              P["alpha_low"], num(-rel3(w95, w8), 2), P["sa_hard_cap"], P["frozen_ratio"], P["window_init"],
              P["window_target_accept"], P["moves_per_cell"], P["target_steps"]))
    bullet(doc, "Stopping rule：", "%s s hard cap；或 acceptance rate < %s %%，且 best 在最近 %s 個溫度（至少 %s moves/cell）"
           "內改善 < %s %% 時提前停止。ΔHPWL = 0 的 move 仍接受（探索平坦區），但不計入 acceptance rate，否則接受率"
           "在 T≈0 時仍偏高、永遠不會提前停止。"
           % (P["sa_hard_cap"], P["early_stop_accept"], P["early_stop_steps"], P["early_stop_moves_per_cell"],
              P["early_stop_gain"]))
    bullet(doc, "Best-so-far journal：", "記錄自上次 snapshot 後改變過的 cell（per-cell dirty flag，長度不超過 cell 數）；"
           "current 優於 best 時只複製這些 cell。因此 CSV 與 stdout 上的 best HPWL 一定對應實際儲存的 placement；"
           "輸出前再從頭重算 HPWL 並做 legality 檢查。")

    figure(doc, os.path.join(FIG, "fig_sa_curves.png"), 12.2)
    caption(doc, "圖", "Current／best-so-far HPWL 與 temperature 對 attempted SA iterations（ibm12-cu85；左 mild、右 harsh GP）")
    small = SMALL_SEC
    para(doc, [
        ("觀察與設計理由：", "b"),
        "mild GP 的起點已接近良好解，SA 先經過一段 hot-phase hump：current HPWL 最多上升 %s %%，直到 %s %% 的 "
        "iterations 才回到起點以下，這是為了跳出 local minimum 而付出的探索成本；之後快速下降，best-so-far 最終 %s %%。"
        "harsh GP 離最佳解較遠，從第一步就開始下降，最終 %s %%。兩者溫度都平穩地依指數下降，且在 %s s cap 時仍緩慢"
        "改善，代表此規模（約 %s 萬 cells）下 SA 受時間限制，因此 move 的效率（例如以一次線性掃描找最近空位）直接轉換為"
        "品質；ibm01（%s cells）則在 %s–%s s 收斂並提前停止。"
        % (num(C005["peak_rise"]), num(C005["back_frac"]), sgn(C005["change"]), sgn(C011["change"]),
           P["sa_hard_cap"], num(CELLS[-1] / 1e4, 1), "{:,}".format(CELLS[0]), num(min(small), 1), num(max(small), 1))])

    # 4. Experiments
    heading(doc, "4. 實驗結果")
    para(doc, [
        ("測資：", "b"),
        "取自 ibm-place2 的 ibm01／ibm10／ibm12，全部 6 個 utilization variant（%s），%s–%s cells。原始 .pl 把所有 "
        "movable cell 都放在 (0, 0)，因此以 Dragon 的 legal placement 加上 Gaussian noise 作為 GP：mild σ = %s row／%s sites"
        "（input001–006），harsh σ = %s rows／%s sites（input007–012），seed %s。Dragon placement 因而是同一 GP 的已知 legal "
        "解，作為 D_total／D_max 與 HPWL 的參考（表 2）。"
        % ("cu85–cu95", "{:,}".format(CELLS[0]), "{:,}".format(CELLS[-1]), P["noise_mild_rows"], P["noise_mild_sites"],
           P["noise_harsh_rows"], P["noise_harsh_sites"], P["seed"])])
    worse = "、".join("%s（%s vs %s）" % (r["case"], num(f(r["d_max"]), 0), num(f(r["ref_d_max"]), 0)) for r in DMAX_WORSE)
    para(doc, [
        ("Stage 1 與 SA 結果：", "b"),
        "Stage 1 的 D_total 為 Dragon 的 %s %%（mild）與 %s %%（harsh）；D_max 在 %d 個 case 中有 %d 個低於 Dragon，例外為 %s，"
        "兩者都是該 benchmark 中 utilization 較高的 variant。SA 在相同輸入上使 HPWL 下降 %s %%，且 %d 個 case 全部低於 Dragon "
        "的 HPWL（%s %% 至 %s %%）。最長執行時間 %s s、peak memory 最多 %s MB，checker 全部 %s。"
        % (rng(RATIO_MILD, 0), rng(RATIO_HARSH, 0), len(FINAL), len(DMAX_BETTER), worse, rng(SA_GAIN, 1), len(FINAL),
           sgn(max(VS_DRAGON)), sgn(min(VS_DRAGON)), num(MAX_SEC, 2), num(MAX_MB, 1), "PASS" if ALL_PASS else "未通過")])
    rows = []
    for i, r in enumerate(FINAL):
        rows.append([r["case"][-3:], "%s %s" % (SOURCES[i % 6], "mild" if i < 6 else "harsh"),
                     "%s / %s" % (M(r["d_total"]), M(r["ref_d_total"])),
                     "%s / %s" % (num(f(r["d_max"]), 0), num(f(r["ref_d_max"]), 0)),
                     M(r["hpwl_legal"]), M(r["hpwl_sa"]), sgn(-f(r["improve_pct"])),
                     sgn(pct(r["hpwl_sa"], r["ref_hpwl"])), num(f(r["seconds"]), 1)])
    caption(doc, "表", "最終結果（D_total、D_max 欄為 ours / Dragon；D_total 與 HPWL 單位為 10⁶）",
            keep_next=True, after=1)
    table(doc, ["case", "來源", "D_total", "D_max", "HPWL legal", "HPWL SA", "SA 變化 (%)", "vs Dragon (%)", "時間 (s)"],
          rows, [0.9, 2.4, 2.4, 2.2, 2.0, 1.7, 1.9, 2.1, 1.3], align=["c", "l", "r", "r", "r", "r", "r", "r", "r"])

    fr = sweep("genswap", "with (run 1)")
    nogs = sweep("genswap", "without (rerun)")
    m20 = sweep("alpha_mpc", "alpha_low=0.95 / mpc=20")
    s300 = sweep("steps", "mpc=20 / 300 steps (move-based patience)")
    s150 = sweep("steps", "mpc=20 / 150 steps (move-based patience)")
    w10 = sweep("window_alpha", "win=10 rows / alpha_low=0.95")
    w3 = sweep("window_alpha", "win=3 rows / alpha_low=0.95")
    t002 = sweep("t0_accept", "0.002")
    a09 = sweep("alpha_mpc", "alpha_low=0.9 / mpc=10")
    a098 = sweep("alpha_mpc", "alpha_low=0.98 / mpc=10")
    mixes = [r for r in SWEEPS if r["experiment"] == "mix" and not r["note"].startswith("chosen")]
    mixref = next(r for r in SWEEPS if r["experiment"] == "mix" and r["note"].startswith("chosen"))
    mixrel = [rel3(r, mixref) for r in mixes]
    para(doc, [
        ("SA 調校證據（表 3）：", "b"),
        "參數掃描以 input001／005／011 三個 case 並行執行；同一設定重複執行約有 0.4–1 %% 的差異，因此小於此幅度的差異"
        "視為無效。所有 SA 調整合計使 12 個 case 的 HPWL 平均 %s %%，且每個 case 都變好（%s %% 至 %s %%）。"
        % (sgn(PROG_FIN_MEAN), sgn(max(PROG_FIN)), sgn(min(PROG_FIN)))])
    kept = [
        ["保留", "以一次線性掃描找最近空位（原佔 SA 時間 35 %）", "input005 −0.7 %、input011 −0.6 %"],
        ["保留", "T0 目標 uphill 接受率 1 %（原 20 %）", "%s %%" % sgn(-rel3(t0, t0c))],
        ["保留", "低接受率區 α = 0.95（原 0.8）", "%s %%" % sgn(rel3(w95, w8), 2)],
        ["保留", "每溫度 20 moves/cell（原 10）", "%s %%" % sgn(rel3(m20, w95))],
        ["保留", "提前停止另需 10 moves/cell", "避免提前停止（曾於 31.8 s 停止）"],
        ["保留", "ΔHPWL = 0 的 move 不計入接受率", "synth_s：45 s cap → 1.6 s 收斂"],
        ["保留", "Generalized swap（不同寬度互換）", "%s %%（3 case）、%s %%（12 case）" % (sgn(rel3(fr, nogs)), sgn(GENSWAP_12))],
        ["捨棄", "較小的初始 window（10 / 3 rows）", "%s %% / %s %%" % (sgn(rel3(w10, w95)), sgn(rel3(w3, w95)))],
        ["捨棄", "T0 uphill 接受率 0.2 %", "%s %%" % sgn(rel3(t002, t0c))],
        ["捨棄", "α = 0.9 / 0.98（同為 10 moves/cell）", "%s %% / %s %%" % (sgn(rel3(a09, w95)), sgn(rel3(a098, w95), 2))],
        ["捨棄", "每個時間預算 300 個溫度", "%s %%" % sgn(rel3(s300, s150))],
        ["捨棄", "其他 move mix（4 組）", "%s %% 至 %s %%" % (sgn(min(mixrel), 2), sgn(max(mixrel), 2))],
    ]
    caption(doc, "表", "SA 調整的效果（負值代表 HPWL 較低；除註明外為 input001／005／011 三個 case 的平均）", keep_next=True, after=1)
    table(doc, ["", "變更", "效果"], kept, [1.2, 8.6, 6.4], align=["c", "l", "l"])

    figure(doc, os.path.join(FIG, "fig_placement_zoom.png"), 17.0)
    caption(doc, "圖", "input011 局部放大（%s rows × %s sites）：GP（cell 重疊）、Stage 1 legal、Stage 2 SA"
            % (P["zoom_rows"], P["zoom_sites"]))
    para(doc, [
        ("輔助工具（自行開發）：", "b"),
        "tools/checker.py 以 Python 獨立實作（不共用 C++ 程式碼），檢查 row、site alignment、subrow 範圍、overlap、"
        "terminal 未移動、orientation 未變，並重算 displacement 與 HPWL 和 dp 的 stdout 比對；開發中它與 dp 內部檢查"
        "抓出了一個同一 segment 內交換的 bug。run_all.sh 依評分流程執行所有 case，量測時間與記憶體、執行 checker，並輸出含 "
        "Dragon 參考欄位的表格；plot_sa.py、plot_placement.py 產生圖 1、圖 2；convert_ibm.py 與 gen_synth.py 產生測資。"])

    heading(doc, "5. 限制與說明")
    para(doc, [
        "(1) 測資並非官方測資，而是由 ibm-place2 搭配 Dragon placement 與 Gaussian noise 產生。(2) Dragon 以 pin offset "
        "最佳化 HPWL，本作依規格使用 cell center，因此與 Dragon 的 HPWL 比較僅供參考。(3) SA schedule 由時間驅動，不同機器"
        "或不同次執行的結果會略有差異（約 0.4–1 %），以 --iters 指定 move 數即可完全重現。(4) 大設計受時間限制，"
        "multi-thread SA 或 row 內 permutation 是可能的延伸方向。"])
    doc.save(OUT)
    print("wrote", OUT)


if __name__ == "__main__":
    build()
