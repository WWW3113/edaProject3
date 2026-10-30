# 專案簡報：CAD Project 3 — Standard Cell Detailed Placement（dp）

> 讀者：沒看過這個 repo、也沒參與開發對話的人（例如要擔任這個專案家教的另一個 AI）。
> 本文所有關於程式的敘述都對照目前的程式碼，並以 `檔案:函式` 標註出處。路徑相對於 repo 根目錄。
> 數字若是實驗結果，出處是 `report/tables.md`、`report/ablation.md` 或 `report/data/*.csv`；
> 開發過程中量到、但沒有收進 `report/data/` 的數字會註明「開發期量測」。

---

## 1. 問題定義（規格摘要）

規格文件：`docs/Project03.pdf`（原始）與 `docs/Project03_update.pdf`（10/28 更新，衝突時以此為準），輸入格式見 `docs/BookshelfFormat.txt`。

**輸入**：GSRC Bookshelf 格式的一組檔案，共用同一個 base name（例如 `input001`）：
`.aux`（列出其他檔名）、`.nodes`（物件寬高；fixed 物件標 `terminal`）、`.nets`（`NetDegree : k` 後接 k 行 pin，pin offset 可有可無）、`.pl`（lower-left 座標、orientation，fixed 物件帶 `/FIXED`）、`.scl`（`CoreRow`：`Coordinate`、`Height`、`Sitewidth`、`Sitespacing`、一或多個 `SubrowOrigin … NumSites`）、`.wts`。
規格簡化：沒有 macro、cell 同高；orientation 與 fixed terminal 位置不可改變。

**Stage 1（legalization）**：把 global placement（GP）變成 legal placement——每個 movable cell 在某個 row 上、x 對齊 site、完整落在某個 subrow 內、cell 之間不重疊——並同時最小化：
- 單一 cell 的 displacement：d_i = |x_i^gp − x_i^legal| + |y_i^gp − y_i^legal|（Manhattan，lower-left 座標）
- D_total = Σ_i d_i，D_max = max_i d_i（只算 movable cells）

**Stage 2（SA）**：從 Stage 1 的結果出發，以自行實作的 simulated annealing 在**始終維持 legality** 的前提下最小化 total HPWL，輸出找到的最佳解。
- HPWL(net) = (max x − min x) + (max y − min y)，座標用 **cell center**（忽略 pin offset），total HPWL 為所有 net 的未加權總和。

**輸出**（寫到 current working directory）：`<out>_legal.pl`（Stage 1）、`<out>.pl`（Stage 2 best）、`<out>.aux/.nodes/.nets/.scl/.wts`。stdout 只印四行：
```
Total displacement (legal): <float>
Maximum displacement (legal): <float>
Total HPWL (legal): <num>
Total HPWL (SA): <num>
```

**評分環境與硬性限制**（update PDF）：Ubuntu 24.04、GCC、建議 C++17、8 GB RAM、無網路；每個 test case **60 s**（含 I/O 與兩個 stage）；build ≤ 60 s。評分流程：
```
cd M11515075 && cmake -B build && cmake --build build && cd build && ./dp input001 output001
```
執行檔必須在 `build/dp`；參數不足要印錯誤訊息；測資名稱為三位數（`input001`…）。提交物：只含 `src/`、`CMakeLists.txt`、`README.md` 的 `M11515075.zip`，另交 ≤ 3 頁的 PDF 報告。

---

## 2. 整體流程（從 `main()` 看）

出處：`M11515075/src/main.cpp:main`。時間以「程式啟動後經過的秒數」計，由 `M11515075/src/Timer.h:Timer::elapsed`（`steady_clock`）提供。

1. **解析參數**（`main.cpp:parseArgs`）：少於兩個參數 → 印 usage、回傳 1；其餘 flag 僅供實驗。
2. **讀檔**（`Parser.cpp:parseDesign`）：`.aux` → `.nodes` → `.nets` → `.pl` → `.scl`（開發期量測：ibm12 約 0.2 s）。
3. **建 row model**（`Layout.cpp:buildLayout`）：subrow 被 fixed 障礙物切成 free segment。
4. **Stage 1**（`Legalizer.cpp:legalize` → `Legalizer::run`）：Abacus 沒有時間限制，一定跑完；之後的 L1 reposition 與 post-pass 在 **10 s**（`main.cpp` 的 `kStage1Deadline`）前停止。實測 12 個 ibm case 的 Stage 1 最多 1.04 s（`report/tables.md` T2）。
5. **Stage 1 驗證**（`Layout.cpp:verifyLegal`）：若有任何違規 → 印 internal error、回傳 1（不寫出 illegal 結果）。
6. **Stage 2**（`Annealer.cpp:anneal` → `Annealer::run`）：在 **45 s**（`Annealer.h` 的 `kHardCapSec`）強制停止，或更早收斂。
7. **Stage 2 驗證**：`verifyLegal` 失敗、或 SA 的 HPWL 比 legal 還差時，`<out>.pl` 改用 Stage 1 結果（`main.cpp:main`）。
8. **寫檔**（`Output.cpp:writePl`、`writeAuxAndCopies`）與 **stdout**（`Metrics.cpp:displacement`、`totalHpwl`，`Output.cpp:formatNumber`）。

45 s cap 為較慢的評分機器與寫檔保留約 15 s；大 case 總時間實測 ≤ 45.11 s（`report/data/run_final.csv`）。

---

## 3. 檔案地圖

### `M11515075/src/`（提交的程式）
| 檔案 | 角色 |
|---|---|
| `main.cpp` | 參數解析、整體流程、兩次 legality 驗證、stdout 四行輸出。 |
| `Design.h` | 資料結構 `Cell`、`Net`、`Subrow`、`Row`、`Design`、`Placement`。 |
| `Parser.cpp` | Bookshelf 解析：整檔讀入記憶體，以手寫 tokenizer（`LineReader`）切 token。 |
| `Layout.h` / `Layout.cpp` | `Segment` / `Layout`（free segment 與整數 site index）、`buildLayout`、`verifyLegal`、`Layout::sitesOf`、`Layout::nearestRow`。 |
| `Legalizer.h` / `Legalizer.cpp` | Stage 1：Abacus、fallback、L1 reposition（PAV）、post-pass（gap move / swap）。 |
| `Annealer.h` / `Annealer.cpp` | Stage 2：occupancy grid、四種 move、incremental HPWL、溫度排程、提前停止、best-so-far journal、CSV log。`Annealer.h` 的 `sa_tuning` namespace 集中所有 SA 常數。 |
| `Metrics.h` / `Metrics.cpp` | `displacement`（D_total、D_max）、`totalHpwl`（cell center）。 |
| `Output.h` / `Output.cpp` | `writePl`、`writeAuxAndCopies`、`formatNumber`。 |
| `Timer.h` | 程式啟動時間點與 `elapsed()`。 |
| `../CMakeLists.txt` | 未指定時預設 `Release`，flags `-O2 -DNDEBUG -std=c++17`，`RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}` 讓執行檔落在 `build/dp`。 |

### `tools/`（輔助工具，不在提交 zip 內）
| 檔案 | 角色 |
|---|---|
| `checker.py` | 以 Python 獨立重寫的檢查器：row、site、subrow、overlap、terminal 字串不變、orientation／`/FIXED` 不變；重算 D_total、D_max、HPWL 並與 dp 的 stdout 比對。 |
| `gen_synth.py` | 合成測資產生器（見第 10 節）。 |
| `convert_ibm.py` | ibm-place2 variant → `inputXXX`；`--gp` 以 Dragon placement 加 Gaussian noise 當 GP，並寫出 `inputXXX.ref.pl`。 |
| `ref_metrics.py` | 計算參考 placement（`.ref.pl`）相對 GP 的 D_total、D_max 與 HPWL。 |
| `run_all.sh` | 模擬評分流程跑完所有 case：量測時間與 peak memory、跑 checker、輸出 `results.csv/.md`（含 Dragon 參考欄位）。 |
| `plot_sa.py` | 由 `--sa-log` 的 CSV 畫 current / best-so-far HPWL 與溫度（可多個 case 並排）。 |
| `plot_placement.py` | 畫 GP / Stage 1 / Stage 2 三聯圖，可 `--zoom`。 |

其他：`tests/toy/`（見第 10 節）、`report/`（報告資料與產生腳本）。

---

## 4. 資料模型

出處：`M11515075/src/Design.h`、`M11515075/src/Layout.h`、`M11515075/src/Layout.cpp:buildLayout`、`M11515075/src/Annealer.cpp:Annealer::init`。

- **`Cell`**：`name`、`w`、`h`、`gx`/`gy`（輸入的 lower-left，也就是 GP）、`fixed`、`ni`、`orient`、`fixedTag`、`xStr`/`yStr`（`.pl` 原始座標字串）。`fixed` 為真的條件：`.nodes` 標 `terminal` 或 `terminal_NI`，或 `.pl` 標 `/FIXED` 或 `/FIXED_NI`（`Parser.cpp:parseNodes`、`parsePl`）。`ni` 表示 non-image（`terminal_NI` / `/FIXED_NI`），**不算障礙物**。
- **`Net`**：`pins` 是**去重後**的 cell id（`Parser.cpp:parseNets` 內的 `finish` lambda 做 sort + unique）；pin offset 讀過即丟。少於 2 個不同 cell 的 net 不參與 HPWL（`Metrics.cpp:totalHpwl`、`Annealer::init`）。
- **`Row`**：`y`、`h`、`siteW`、`siteSp`、`subrows`（`origin`、`numSites`）。解析完依 y 排序（`Parser.cpp:parseScl`）。
- **`Segment`**（`Layout.h`）：某個 subrow 被障礙物切開後的一段 free 區間，欄位 `row`、`y`、`org`（subrow origin）、`sp`（site spacing）、`kLo`、`kHi`。所有位置都是**整數 site index k**，x = org + k·sp；寬 ws 個 site 的 cell 放在 k 合法 ⇔ `kLo ≤ k` 且 `k + ws ≤ kHi`。因此 site alignment 由資料結構保證。
- **座標 → site**：`Layout::sitesOf(w, seg) = ceil(w/sp − 1e-6)`。segment 邊界：`kLo = ceil((L − org)/sp − ε)`、`kHi = floor((R − org)/sp + ε)`（`buildLayout`）。subrow 範圍取規格的 [origin, origin + (NumSites−1)·Sitespacing + Sitewidth]。當 cell 寬是 site spacing 的整數倍且 Sitewidth = Sitespacing（ibm 的情況）時完全精確，否則偏保守但不會不合法。
- **障礙物切 subrow**（`buildLayout`）：對每個 `fixed && !ni` 且寬高為正的物件，以二分搜尋找出 y 範圍可能重疊的 row（`y < y1` 且 `y > y0 − maxRowH`），**正面積重疊**才把 [gx, gx+w) 加入該 row 的 blocks；每個 subrow 減去 blocks 的聯集得到 free intervals，再轉成 `Segment`（`kHi > kLo` 才保留）。`Layout.rowFirstSeg[r] … rowFirstSeg[r+1]` 是 row r 的 segment 範圍（依 x 排序）。
- **Occupancy grid**（只在 Stage 2）：`Annealer::occ_[s]` 是長度為 `capacity()` 的 `int` 陣列，site → cell id，空位為 −1（`Annealer::init`、`isFree`、`setOcc`、`occAt`）。Stage 1 則用每個 segment 依 k 排序的 cell 清單 `Legalizer::segCells_`。

---

## 5. Parser 與 Writer 的關鍵決策

**Parser**（`M11515075/src/Parser.cpp`）
- **整檔讀入 + 手寫 tokenizer**（`readFile`、`LineReader::next/split`），不用 `>>`：速度快，而且容忍任意空白、tab 與 `\r`。`'#'` 之後是註解；**`':'` 一律單獨成一個 token**，所以 `NumNodes:10` 和 `NumNodes : 10` 解析結果相同。數字用 `std::from_chars`（`toDouble`）。
- `.nets` pin 行只取第一個 token（物件名）；`I/O/B` 與 `: dx dy` 都忽略，**有沒有 offset 都能讀**。
- `.scl` 的 `SubrowOrigin` 與 `NumSites` 可在同一行或分兩行（`parseScl` 以 `valueAfter` 逐 key 讀）。
- `.aux` 裡的路徑相對於 `.aux` 所在目錄（`parseAux`）；若 `.aux` 缺檔名，退回 `<base>.<ext>`。`.pl` 缺少的物件印 warning 並視為 (0,0)。

**Writer**（`M11515075/src/Output.cpp`）
- **fixed 物件原樣輸出座標字串**（`writePl` 使用 `xStr`/`yStr`）：完全避免浮點格式造成的「terminal 被移動」（例如 `-5.0` 與 `-5` 文字不同）。checker 也以**字串**比對 fixed 座標。
- movable 座標用 `appendCoord`：整數印成整數，否則印 `%.6f` 並去掉尾端的 0。orientation 與 `/FIXED` 標記照原樣保留；物件順序與輸入 `.pl` 相同（`Design::plOrder`）。
- `.aux` 重寫（`writeAuxAndCopies`）：保留原本的關鍵字（通常是 `RowBasedPlacement`），每個檔名換成 `<out 的 basename>.<原副檔名>`；`.nodes/.nets/.scl/.wts` **逐 byte 複製**（`.wts` 不存在時略過）。
- **stdout 格式**（`formatNumber`）：`%.3f` 後去掉尾端的 0 與小數點（`-0` → `0`），避免 `cout` 預設格式把大的 HPWL 印成科學記號；與規格範例（`62531.2`、`757296`）一致。

---

## 6. Stage 1 詳解

出處：`M11515075/src/Legalizer.cpp`。執行順序（`Legalizer::run`）：依 GP x 排序 → `abacusAll`（必要時 fallback）→ `l1All` → 最多 20 輪 {`reduceMax` → `reduceTotal(current D_max)` → `l1All`}。

### 6.1 Abacus（Spindler et al., ISPD'08）

每個 segment 維護一串 **cluster**（`struct Cluster`：e = 權重總和、q = 加權目標和、w = 寬度（sites）、x = 最佳且被夾在邊界內的起點）。每個 cell 權重 e = 1（displacement 是逐 cell 計算，所以不用面積加權），目標 t = (gx − org)/sp（site 單位）。

**合併數學**：cluster 的位置 x 要最小化 Σ (x + off_i − t_i)²，其中 off_i 是 cell 在 cluster 內的偏移，所以最佳解是 x* = q/e，其中 q = Σ (t_i − off_i)。把右邊的 cluster c 併入左邊的 p 時，c 的 cell 偏移都要加上 p.w，因此：
```
p.q ← p.q + c.q − c.e · p.w ;  p.e ← p.e + c.e ;  p.w ← p.w + c.w
x ← clamp(q/e, kLo, kHi − w)；若與左鄰 cluster 重疊（左鄰.x + 左鄰.w > x）就繼續往左合併
```

**Trial placement**（`Legalizer::trial`）：不修改資料，只模擬把 cell 接在 segment 最右邊後的 collapse，回傳該 cell 自己的 displacement（Abacus 原始 cost，只算被放的 cell）。**Commit**（`Legalizer::commit`）做同樣的事，但會真的 pop 與合併 cluster，並維護 `clFirst`（每個 cluster 第一個 cell 的索引）。

```cpp
// M11515075/src/Legalizer.cpp : Legalizer::trial
double Legalizer::trial(const AbacusSeg& s, const Segment& g, int id, double& xSites) const {
    long long wc = ws(id, g);
    double e = 1, q = target(id, g);
    long long w = wc;
    int idx = static_cast<int>(s.cl.size()) - 1;
    double x;
    for (;;) {
        x = std::min(std::max(q / e, static_cast<double>(g.kLo)), static_cast<double>(g.kHi - w));
        if (idx >= 0 && s.cl[idx].x + static_cast<double>(s.cl[idx].w) > x) {
            const Cluster& p = s.cl[idx];
            q = p.q + q - e * static_cast<double>(p.w);
            e += p.e;  w += p.w;  --idx;
        } else break;
    }
    xSites = x + static_cast<double>(w - wc);          // new cell is the cluster's last cell
    const Cell& c = d_.cells[id];
    return std::fabs(g.org + xSites * g.sp - c.gx) + std::fabs(g.y - c.gy);
}
```

**Row 搜尋與 pruning**（`Legalizer::bestSegment`、`forRowsNear`）：
```
best = ∞
for row r in 依 |row.y − gy| 由小到大（上下雙指標交錯）:
    dy = |row.y − gy|;  if dy ≥ best: break          # 之後的 row 只會更遠
    s0 = row 內第一個 xLo > gx 的 segment
    for s = s0 往右: if dy + max(0, xLo − gx) ≥ best: break;  visit(s)
    for s = s0−1 往左: if dy + max(0, gx + w − xHi) ≥ best: break;  visit(s)
visit(s): 若 used + ws > capacity 則略過；否則 cost = trial(s)，更新 best
```
因為 cost ≥ 下界，這個 pruning 對 Abacus cost 而言是**精確**的，不需要固定的 row 視窗。

**Snapping**（`abacusAll` 的結尾）：每個 cluster 起點取 `llround(x)`，cluster 內的 cell 依序緊接著放。因為四捨五入是單調的、寬度是整數 site，且 x 已被夾在 [kLo, kHi − w]，所以不會產生 overlap，也不會超出 segment。

**小例子**（segment kLo = 0、kHi = 20、sp = 1；依 x 順序放入 A(w=4, t=5)、B(w=3, t=6)、C(w=2, t=7)）：
1. A：cluster {e=1, q=5, w=4}，x = 5 → A 佔 [5, 9)。
2. B：新 cluster {1, 6, 3}，x = 6，但 A 的結尾 9 > 6 → 合併：q = 5 + 6 − 1·4 = 7、e = 2、w = 7，x = 7/2 = 3.5（驗算：min (x−5)² + (x+4−6)² ⇒ 2x = 7）。A 在 3.5、B 在 7.5。
3. C：新 cluster {1, 7, 2}，x = 7，但前一個 cluster 的結尾 3.5 + 7 = 10.5 > 7 → 合併：q = 7 + 7 − 1·7 = 7、e = 3、w = 9，x = 7/3 ≈ 2.333。對 C 做 trial 時回傳 xSites = 2.333 + (9 − 2) = 9.333、cost = |9.333 − 7| = 2.333。
4. Snap：`llround(2.333) = 2` → A = 2、B = 6、C = 9。site 單位的 displacement 為 3 + 0 + 2 = 5。

### 6.2 Fallback（`Legalizer::run`）

只有**所有 segment** 都放不下（`used + ws > capacity`）時 cell 才失敗。之後最多重跑 3 次：前兩次把失敗的 cell 排最前（寬者優先），第三次全部寬者優先（first-fit decreasing 精神）。仍失敗 → `main` 回傳 1，不輸出 illegal 結果。`toy3` 觸發重跑後成功，`toy4` 不可行。

### 6.3 L1 reposition（PAV + median，含 D_max guard）

Abacus 最小化的是 displacement **平方**和，規格的目標卻是 L1。固定一個 segment 內的 cell 順序後，令 W_i = 前面 cell 的寬度和、z_i = k_i − W_i、a_i = t_i − W_i，「不重疊」就等價於 z 非遞減，問題變成 **isotonic regression（L1）**：min Σ|z_i − a_i|，z 非遞減，z ∈ [kLo, kHi − W_n]。

`Legalizer::l1Segment` 用 **pool-adjacent-violators（PAV）**：每個 block 以兩個 heap（`struct MedianBlock`：lo 為 max-heap、hi 為 min-heap）維護 **lower median**；新的 block 進來後，只要左鄰的 median > 自己的 median 就合併（`absorb`：小的併入大的）。所有 cell 權重相同，因此是**不加權的 median**。最後每個 block 的值夾到 [kLo, kHi − W_n] 並 `llround`（單調，不破壞順序）。

```cpp
// M11515075/src/Legalizer.cpp : Legalizer::l1Segment（核心）
for (int i = 0; i < n; ++i) {
    MedianBlock b;
    b.first = i;  b.last = i + 1;
    b.insert(target(v[i], g) - static_cast<double>(W[i]));      // a_i = t_i - W_i
    st.push_back(std::move(b));
    while (st.size() >= 2 && st[st.size() - 2].median() > st.back().median()) {
        st[st.size() - 2].absorb(st.back());                     // pool adjacent violators
        st.pop_back();
    }
}
const double zHi = static_cast<double>(g.kHi - W[n]);
for (const MedianBlock& b : st) {
    double z = std::min(std::max(b.median(), static_cast<double>(g.kLo)), zHi);
    long long zi = std::llround(z);
    for (int i = b.first; i < b.last; ++i) nk[i] = zi + W[i];  // k_i = z + W_i
}
// D_max guard：只有在 newMax <= oldMax 且 newTot <= oldTot 時才採用 nk
```
**D_max guard**：只有在該 segment 的**最大** displacement 不增加，**而且** segment 總和也不增加時才採用新位置；否則保留原位置。延續 6.1 的例子：a = (5, 6−4, 7−7) = (5, 2, 0)，PAV 把三者合成一個 block，lower median = 2 → z = 2 → k = (2, 6, 9)，與 Abacus 相同（此例 Abacus 剛好已是 L1 最佳）。

### 6.4 D_max post-pass

`removeFromSeg` / `insertIntoSeg` 維護依 k 排序的 `segCells_`。

**`tryGapMove(id)`**：先把 cell 移出，在 `forRowsNear` 的前 **40** 個 row（`kPostPassMaxRows`）中找 free gap。每個 segment 從 gx 所在的位置向左右各最多 **64** 個 gap（`kPostPassMaxScan`），以下界 pruning；gap 內試 floor(t) 與 floor(t)+1（夾在 gap 內）。只有 displacement **嚴格變小**（< 目前值 − 1e-6）才移動，否則放回原位。

**`trySwap(id, goal, cap)`**：候選 b 必須**同寬**（|w_a − w_b| ≤ 1e-9），在 dy < d_a 的 row 中、x 距離 a 的 GP 不超過 d_a − dy，從 gx 往兩側各最多 64 個。交換後 a、b 的新 displacement 為 na、nb，令 m = max(na, nb)：
- `kMax`：需 m < d_a（同樣 m 時取總和較小者），且 (na + nb) − (da + db) ≤ d_a − m（總和增加不得超過最大值的降幅）。
- `kTotal`：需 gain = (da + db) − (na + nb) > 1e-6（取最大的 gain），且 m ≤ cap（cap = 本輪 reduceMax 後的 D_max）。
- 寫回時**先找到兩個 iterator 再寫入**（因為 a、b 可能在同一個 segment，見第 9 節 bug 1）。

```
reduceMax():                       # Legalizer::reduceMax
    pq = max-heap of (disp, id) for all movable
    loop:
        (d, id) = pq.pop();  若 d 已過期（≠ 現在的 disp）則跳過
        moved = tryGapMove(id) or (partner = trySwap(id, kMax)) >= 0
        if not moved: break            # 最差的 cell 動不了 → D_max 已定
        push (disp(id), id)；若有 partner 也 push
reduceTotal(cap):                  # Legalizer::reduceTotal
    for id in 依 disp 由大到小:
        if disp(id) <= 0: break
        if not tryGapMove(id): trySwap(id, kTotal, cap)
rounds (≤ 20): reduceMax → reduceTotal(D_max) → l1All
    停止條件：D_total 增益 < 0.1 % 且 D_max 增益 < 1 %，或已超過 10 s deadline
```
所有 post-pass 迴圈每 64（reduceMax）或 256（reduceTotal）次檢查一次 deadline。

---

## 7. Stage 2 詳解

出處：`M11515075/src/Annealer.cpp`、`M11515075/src/Annealer.h`。

### 7.1 Move types 與 legality 論證

每次 iteration 隨機選一個 movable cell a，再依累積門檻選一種 move（`Annealer::run` 內的 `propose`）。每個 proposal 最多改變兩個 cell（`Change ch_[2]`），所有新位置在提出前都已用 occupancy grid 檢查過，所以**任何時刻 placement 都合法**，不需要事後 repair。

1. **Optimal region，40 %**（`proposeOptimal` → `proposeAt`）：`optimalRegion` 對 a 的每個 net（度數 ≤ 32，`kOptRegionMaxDegree`）計算**不含 a** 的 bounding box，取所有 x 端點的 median 區間 [x1, x2]（y 同理）。目標點在區間內隨機取，再加 ±0.1·window 的雜訊，並夾在 a 目前位置 ± window 之內（range limiter）。`proposeAt` 依序嘗試：
   - 目標 sites 全空（或是 a 自己的）→ 移過去。`isFree` 保證合法。
   - 被**等寬** cell b 佔用 → 交換。同寬的兩個 cell 互換彼此的 slot，必然合法。
   - 被**不同寬度**的 b 佔用 → **generalized swap**（見下方程式碼）：`freeSpan` 算出兩者各自的 slot（自身 site 加上兩側連續空 site，每側最多 256），若 b 放得進 a 的 slot、a 放得進 b 的 slot，且兩個 slot 在同一 segment 時**互不相接**（`aHi < bLo || bHi < aLo`），兩者的新位置就落在各自不重疊的空間 → 合法。
   - 否則在同 segment 從目標往左右各線性掃描一次，找最近的「連續 w 個空 site」（`freeAt`，每側最多 256）→ 移過去。
2. **Random window，20 %**（`proposeRandom`）：目標為目前位置 ± window 內的均勻隨機點，其餘同 `proposeAt`。
3. **Adjacent swap，20 %**（`proposeAdjacentSwap`）：往右掃描（最多 256 site）找到右鄰 b。b 移到 a 的起點 k_a，a 移到 k_b + w_b − w_a。兩者總跨距 [k_a, k_b + w_b) 不變，裡面只有 a、b 與空位 → 合法。
4. **In-gap shift，20 %**（`proposeShift`）：找出 a 周圍的空隙 [left, right)（每側最多 256），一半機率移向 optimal x，否則均勻隨機，並夾在空隙內 → 合法。

```cpp
// M11515075/src/Annealer.cpp : Annealer::proposeAt（generalized swap 片段）
if (b >= 0) {
    int sa = seg_[a];
    long long wa_old = ws(a, sa), wb = ws(b, s);
    long long aLo, aHi, bLo, bHi;
    freeSpan(sa, k_[a], wa_old, -1, aLo, aHi);      // a 的 slot
    freeSpan(s, k_[b], wb, -1, bLo, bHi);           // b 的 slot
    long long wbInA = ws(b, sa);
    bool disjoint = sa != s || aHi < bLo || bHi < aLo;
    if (disjoint && aHi - aLo >= wbInA && bHi - bLo >= w) {
        long long ka = std::min(std::max(k, bLo), bHi - w);           // a 盡量靠近目標
        long long kb = std::min(std::max(k_[a], aLo), aHi - wbInA);
        ch_[0] = {a, s, ka, 0, 0};
        ch_[1] = {b, sa, kb, 0, 0};
        nch_ = 2;
        return true;
    }
}
```
各 move 能產生合法 proposal 的比例（`report/data/move_validity.csv`）：optimal region 56.4 %（加入 generalized swap 前為 51.9 %）、random window 68.0 %、adjacent swap 99.5 %、in-gap shift 17.5 %；optimal region 的「失敗」多半是 cell 已在最佳區域內（目標即現位置）。

### 7.2 Incremental HPWL

- 每個 net 快取 `Box`：xlo、xhi、ylo、yhi，以及**每條邊上的 pin 數** nxlo、nxhi、nylo、nyhi（`computeBox`）。只用 ≥ 2 個 cell 的 net，以 CSR 存 net→cells 與 cell→nets（`Annealer::init`）。
- `evaluate()`：先把 move 的新 center 暫時寫入 `cx_`/`cy_`，用 stamp 收集受影響的 net（去重）。度數 ≥ **12**（`kIncrementalMinDegree`）的 net 以 `updateAxis` 增量更新，只有某條邊失去最後一個 pin 時才 `computeBox` 重算；度數 < 12 的 net 直接重算（更便宜）。回傳 ΔHPWL。
- 接受 → `commit`（寫回 box、更新 occupancy 與 seg/k、標記 dirty）；拒絕 → `revert`（還原 center）。
- 每個溫度結束時重新加總 `cur_`（`currentTotal`），避免浮點誤差累積。

```cpp
// M11515075/src/Annealer.cpp : updateAxis
inline bool updateAxis(double& lo, double& hi, int& nlo, int& nhi, double o, double nw) {
    if (nw < lo) { lo = nw; nlo = 1; } else if (nw == lo) { ++nlo; }   // 先加入新座標
    if (nw > hi) { hi = nw; nhi = 1; } else if (nw == hi) { ++nhi; }
    if (o == lo) --nlo;                                                // 再移除舊座標
    if (o == hi) --nhi;
    return nlo > 0 && nhi > 0;     // false：某邊沒有 pin 了 → 必須整個重算
}
```
同一個 net 有兩個 cell 都移動時（swap），兩個 change 依序套用，每次都是先加後減，所以計數仍然正確。

### 7.3 溫度排程、range window、時間驅動

（`Annealer::run`）
- **T0**：先試 4000 個 proposal，只計算 Δ 不套用，取 uphill Δ 的平均值 Δ̄⁺，令 T0 = −Δ̄⁺ / ln(p0)，p0 = **0.01**（`kInitAcceptProb`）。這段同時量出每個 move 的平均耗時 `perMove`。
- **每個溫度的 moves**：mps = **20**·N（`kMovesPerCellPerStep`）。在時間驅動模式下，預估剩餘時間可做的 move 數 budget，取 fit = budget / **150**（`kTargetSteps`），mps = max(0.5·N, min(mps, fit))，且 ≥ 100。小設計用滿 20 moves/cell；大設計自動減少，讓大約 150 個溫度放得進時間預算。
- **冷卻**：acceptance rate r = 「改變了 HPWL 的接受次數 / 合法 proposal 數」（**ΔHPWL = 0 的 move 會被接受但不計入**）。α = 0.5（r > 0.96）、0.9（r > 0.8）、0.95（r > 0.15）、**0.95**（其餘，`kLowAcceptAlpha`）。若 T > Tfrozen = 1e-4·T0（`kFrozenTempRatio`），再取 α = min(α, (Tfrozen/T)^(1/stepsLeft))，stepsLeft = 剩餘時間 / 本步耗時，確保在 45 s 前冷到 Tfrozen。
- **Range window**：初始 = max(chip 寬, chip 高) 的 10 %（`kInitWindowRows = 0` 時），下限 2 個 row height、上限 max(chip 寬, chip 高)。每步 window ← window·(1 − 0.44 + r)（目標 44 % 接受率，VPR 的 range limiter）。
- **`--iters N`**：排程不看時鐘，跑滿 N 次 iteration 就停，同 seed 結果逐 byte 相同（45 s cap 仍有效）。

### 7.4 停止規則

- **Hard cap**：每 1024 次 iteration 檢查一次 `Timer::elapsed() > 45`（`kHardCapSec`）。
- **提前停止**（收斂）：每個溫度結束時把 (iter, best) 存入 `bestHist`。若 r < **5 %**（`kEarlyStopAcceptRate`），且歷史已涵蓋至少 **5** 個溫度（`kEarlyStopPatience`）**和**至少 **10·N** 次 iteration（`kEarlyStopPatienceMovesPerCell`），而這段期間 best 的改善 < **0.01 %**·best（`kEarlyStopRelImprove`）→ 停止。

### 7.5 Best-so-far journal（dirty flags）

`commit` 對每個被移動的 cell 呼叫 `markDirty`：只有第一次變髒時才 push 進 `journal_`，所以 journal 長度 ≤ N。當 `cur_ < best_ − 1e-6` 時 `syncBest` 只把 journal 裡的 cell 複製到 `bestSeg_`/`bestK_`、清除 dirty flag、`best_ = cur_`。因此任何時刻 `best_`（以及 CSV 的 best_hpwl）都**對應一個實際存著的 placement**。輸出時由 `bestSeg_`/`bestK_` 組出 placement，`main` 再從頭重算 HPWL 並驗證 legality。

```cpp
// M11515075/src/Annealer.cpp : Annealer::commit / Annealer::syncBest
void Annealer::commit(double delta) {
    for (size_t t = 0; t < touched_.size(); ++t) box_[touched_[t]] = newBox_[t];
    for (int i = 0; i < nch_; ++i) setOcc(seg_[ch_[i].id], k_[ch_[i].id], ws(ch_[i].id, seg_[ch_[i].id]), -1);
    for (int i = 0; i < nch_; ++i) {
        const Change& c = ch_[i];
        seg_[c.id] = c.seg;  k_[c.id] = c.k;
        setOcc(c.seg, c.k, ws(c.id, c.seg), c.id);
        markDirty(c.id);                     // 每個 cell 只會進 journal 一次
    }
    cur_ += delta;
    if (cur_ < best_ - 1e-6) syncBest();
}
void Annealer::syncBest() {
    for (int id : journal_) { bestSeg_[id] = seg_[id]; bestK_[id] = k_[id]; dirty_[id] = 0; }
    journal_.clear();
    best_ = cur_;
}
```
注意 `commit` 先清掉兩個 cell 的舊 occupancy，再寫新的，所以兩個 cell 互換位置時不會互相覆蓋。

### 7.6 SA 主迴圈（pseudo-code）

```
init(legal)                                   # occupancy、CSR、Box、best = current
T0 = −mean(uphill Δ of 4000 samples) / ln(0.01);  T = T0
mps = 20·N（時間驅動時再壓縮，見 7.3）
while not stop:
    tried = acc = moved = 0
    repeat mps 次:
        每 1024 iter 檢查 45 s cap；--iters 模式檢查 iteration 上限
        a = 隨機 movable cell;  依 40/20/20/20 選 move 並 propose(a)
        if proposal 合法:
            tried++;  Δ = evaluate()
            if Δ ≤ 0 or rand < exp(−Δ/T): commit(Δ); acc++; if Δ ≠ 0: moved++
            else: revert()
    cur = currentTotal();  if cur < best: syncBest()
    r = moved / tried
    if r < 5% and best 在 ≥5 步且 ≥10·N iter 內改善 < 0.01%: break   # converged
    α = VPR 規則(r)；時間驅動時 α = min(α, (Tfrozen/T)^(1/stepsLeft))
    T *= α;  window = clamp(window·(0.56 + r), 2 rows, chip)
output placement = (bestSeg_, bestK_)
```

### 7.7 所有調過的常數

`M11515075/src/Annealer.h`（`namespace sa_tuning`）：

| 常數 | 值 | 意義 |
|---|---|---|
| `kHardCapSec` | 45.0 | SA 在程式啟動後幾秒停止 |
| `kEarlyStopAcceptRate` | 0.05 | 提前停止只在接受率低於此值時考慮 |
| `kEarlyStopRelImprove` | 1e-4 | best 改善門檻（0.01 %） |
| `kEarlyStopPatience` | 5 | 觀察的溫度數 |
| `kEarlyStopPatienceMovesPerCell` | 10 | 且至少這麼多 moves/cell |
| `kInitAcceptProb` | 0.01 | T0 對應的 uphill 接受率 |
| `kInitWindowRows` | 0 | 初始 window（0 = chip 的 10 %） |
| `kLowAcceptAlpha` | 0.95 | 接受率 ≤ 15 % 時的 α |
| `kMovesPerCellPerStep` | 20 | 每個溫度的 moves/cell |
| `kMinMovesPerCellPerStep` | 0.5 | 時間不足時的下限 |
| `kTargetSteps` | 150 | 時間預算要容納的溫度數 |
| `kFrozenTempRatio` | 1e-4 | 視為冷凍的 T/T0 |
| `kOptRegionMaxDegree` | 32 | optimal region 忽略更大的 net |
| `kIncrementalMinDegree` | 12 | ≥ 此度數的 net 用 edge count 增量更新 |
| `kMaxGapScan` | 256 | 找鄰居 / 空位時每側最多掃描的 site 數 |
| `kPOptimal` / `kPRandom` / `kPAdjacent` | 0.40 / 0.60 / 0.80 | move 的累積機率門檻（即 40/20/20/20） |

`Annealer::run` 內另有寫死的值（T0 取樣 4000 次、α 與門檻、window 規則、每 1024 iter 檢查時間、mps ≥ 100），已在 7.3、7.4 說明。

Stage 1 常數（`M11515075/src/Legalizer.cpp` 檔頭）：`kMaxRounds = 20`、`kMinRoundGain = 1e-3`（0.1 %）、`kMinRoundMaxGain = 1e-2`（1 %）、`kPostPassMaxRows = 40`、`kPostPassMaxScan = 64`。`M11515075/src/main.cpp`：`kStage1Deadline = 10.0`。

`SaOptions`（`Annealer.h`）的欄位可用實驗用 flag 覆寫（`--t0-accept`、`--moves-per-cell`、`--win0`、`--low-alpha`、`--target-steps`、`--mix`）；預設值就是上表的調好的值。

---

## 8. 設計決策與取捨

實驗數字出自 `report/ablation.md` 與 `report/tables.md`（T2–T5）。參數掃描是 input001/005/011 三個 case 並行跑；同一設定重複執行的差異約 0.4–1 %，小於這個幅度的差異視為雜訊。

| 決策 | 考慮過的替代方案 | 為何選這個（證據） |
|---|---|---|
| 所有位置用整數 site index（Segment） | 存 double 座標、事後 snap | site alignment 由型別保證；四捨五入的單調性保證 snap 不產生 overlap。 |
| Abacus + 依下界 pruning 的全 row 搜尋 | 固定 ±k rows 的搜尋視窗；Tetris 式貪婪 | pruning 對 Abacus cost 是精確的，不需調視窗；12 個 ibm case 的 Stage 1 ≤ 1.04 s。 |
| L1 reposition（PAV），且加 D_max guard | 不做；或無條件採用 L1 解 | 單獨使用只帶來 −0.4 % D_total，但放在每輪 post-pass 後可持續發揮；guard 確保 D_max 不會被犧牲（T2）。 |
| Post-pass：先 reduce max 再 reduce total，swap 的總和增量不得超過最大值降幅 | 只做其中一種；直接加權組合兩個目標 | 完整 Stage 1 相對 Abacus：D_total −9.4 %、D_max −21.2 %（12 case 平均，T2）。 |
| Post-pass 的搜尋上限（40 rows、每方向 64 個） | 不設上限 | 在 ibm case 上品質變化 ≤ 0.04 %；但 all-at-origin 的極端 GP 下，Stage 1 從 15.0 s 降到 5.8 s。 |
| SA move 用 occupancy grid 做局部檢查 | 允許暫時重疊、最後再 legalize | 規格要求全程 legal；檢查是 O(寬度)。 |
| T0 用 1 % uphill 接受率 | 20 %、5 %、0.2 % | 20 % 比 1 % 差 1.5 %；5 % 與 0.2 % 與 1 % 差不多（T4 `t0_accept`）。起點已是好的 legal 解，太熱會浪費時間。 |
| 低接受率區 α = 0.95 | VPR 原本的 0.8；0.9；0.98 | 0.8 比 0.95 差 0.45 %；0.9、0.98 在雜訊範圍內。 |
| 20 moves/cell | 5、10、40 | 10 → 20：−0.8 %（主要在 input001）；40 會讓大 case 在時間到之前就提前停止。 |
| 初始 window = chip 的 10 % | 10 rows、3 rows | 較小的 window 差 +0.4 % 至 +1.3 %：初期跨晶片的大 move 很重要。 |
| 150 個溫度 | 75、300 | 300 差 +0.9 % 至 +1.0 %（每個溫度太短）；75 在雜訊範圍內（−0.26 %）。 |
| Move mix 40/20/20/20 | 60/10/15/15、30/30/20/20、50/20/10/20、40/20/35/5 | 全部落在 −0.22 % 至 +0.13 % 之間，對 mix 不敏感。 |
| Generalized swap（不同寬度互換） | 只允許等寬交換 | 三個 case −1.3 %；12 case 平均約 −1.4 %（T3、T4 `genswap`）。 |
| 提前停止的 patience 同時要求 10 moves/cell | 只看溫度數 | 只看溫度數時，短溫度會讓大 case 太早停止（曾在 31.8 s 停）。 |
| 時間驅動排程（加 `--iters`） | 固定 iteration 數 | 用滿 45 s 預算、小 case 可提前收斂；代價是跨機器略有差異，`--iters` 可重現。 |
| best-so-far 用 dirty-flag journal | 每次整個複製；只記數值 | best 一定對應存著的 placement，且只複製改變過的 cell。 |

所有 SA 改動合計：12 個 case 的 HPWL 平均 −4.3 %，而且每一個 case 都變好（−2.7 % 至 −6.0 %，T3）。

---

## 9. 開發中發現並修正的 bug（與學到的事）

1. **同 segment swap 的清單不一致**（Stage 1，`Legalizer.cpp:trySwap`）：原本先在 `va` 找到 a 並寫入 b，再到 `vb` 找 b；當 a、b 在同一個 segment 時，`vb` 就是 `va`，第二次 `find` 可能找到剛寫入的那一格，於是 `seg_`/`k_` 交換了，清單卻沒換。被 `verifyLegal` 抓到（dp 拒絕輸出）。修法：先取得兩個 iterator 再寫入。**教訓**：內部驗證要一直開著；注意容器 aliasing。
2. **合成測資的邊界堆積**（`tools/gen_synth.py`）：GP 的 y 被夾到 `core_h` 而不是 `core_h − H`，大量 cell 疊在最上方 row 之上，D_max 被這個假象主導。**教訓**：先看最差的幾個 cell 長什麼樣子，再懷疑演算法。
3. **SA 永遠不收斂**（`Annealer::run`）：ΔHPWL = 0 的 move（例如不改變任何 bounding box 的 shift）也被算進接受率，使 T≈0 時接受率仍約 17 %，提前停止的條件永遠不成立，小 case 也跑滿 45 s。修正後 synth_s 在 1.6 s 收斂（HPWL +0.6 %）。**教訓**：控制排程的統計量要反映「有意義的改變」。
4. **找空位的掃描是二次方**（`Annealer::proposeAt`）：原本對每個 offset 呼叫 `isFree` 檢查 w 個 site，佔 SA 時間 35 %；改成每側一次線性掃描計算連續空 site。input005 −0.7 %、input011 −0.6 %。**教訓**：SA 受時間限制時，move 速度就是品質，先 profile。
5. **提前停止太早**（`Annealer::run`）：patience 原本只看「5 個溫度」；溫度很短時（300 步設定）大 case 在 31.8 s 就停了。改為同時要求 10 moves/cell。**教訓**：停止條件要用與問題規模無關的單位。
6. **Post-pass 在極端輸入下太慢**（`Legalizer::tryGapMove`、`trySwap`）：所有 cell 都在原點的 GP 讓搜尋視窗涵蓋數百個 row，Stage 1 吃到 15 s 的 deadline。加上 40 rows / 64 個候選的上限後降到 5.8 s，真實 case 不受影響。**教訓**：deadline 只是安全網，每個 cell 的工作量本身要有上限。

---

## 10. 測試資料

- **`tests/toy/`**（手寫）：
  - `toy1`：最簡單的兩個 row。
  - `toy2`：邊界情況——一個 CoreRow 兩個 subrow（`NumSites` 分行寫）、row 內 fixed block、與 row 重疊的 terminal_NI、無 offset 的 pin、tab 與 `NumNodes:16`、8 種 orientation、site width 2 且 origin 為奇數、GP 在 core 外。
  - `toy3`：容量碎片化，第一次 Abacus 失敗、fallback 後成功。
  - `toy4`：row 容量不足（應回報錯誤、回傳 1）。
- **合成產生器**（`tools/gen_synth.py`）：可調 cell 數與 utilization，含熱點 GP、row 內 macro、terminal_NI、一半 pin 有 offset；用於壓力測試（最大 21 萬 cells）。
- **ibm-place2 + Dragon + Gaussian noise**：benchmark 的 `.pl` 把所有 movable cell 放在 (0,0)，沒有真正的 GP，且 pad 不在任何 net 上。因此 `tools/convert_ibm.py --gp` 拿 Dragon placer 對同一 netlist、同一 variant 的 **legal** 結果，加上 Gaussian noise 當 GP：mild σ = 1 row / 10 sites（input001–006）、harsh σ = 3 rows / 30 sites（input007–012），seed 1。使用 ibm01（12,028 cells）、ibm10（66,762）、ibm12（68,735），每個 benchmark 都有兩個 utilization variant（cu85–cu95）。
- **Dragon 參考基準**：未擾動的 Dragon placement 是同一個 GP 的**已知 legal 解**，所以它相對 GP 的 D_total/D_max（由 `tools/ref_metrics.py` 計算）是 Stage 1 有意義的比較基準；它的 HPWL 也可以拿來對照 SA（但 Dragon 以 pin offset 最佳化，比較只是參考）。

---

## 11. 最終結果

出處：`report/tables.md` T1（資料：`report/data/run_final.csv`）。D_total、HPWL 單位為 10⁶；「ours / Dragon」。

| case | 來源 | D_total ours / Dragon | D_max ours / Dragon | HPWL legal | HPWL SA | SA 變化 | vs Dragon HPWL | 時間 (s) |
|---|---|---|---|---|---|---|---|---|
| 001 | ibm01-cu85 mild | 5.24 / 10.99 | 3115 / 3710 | 68.28 | 48.06 | −29.6 % | −18.3 % | 20.36 |
| 002 | ibm01-cu88 mild | 5.44 / 10.99 | 2171 / 3710 | 68.29 | 49.16 | −28.0 % | −15.8 % | 23.33 |
| 003 | ibm10-cu90 mild | 29.94 / 61.86 | 3376 / 3920 | 659.97 | 578.38 | −12.4 % | −5.2 % | 45.09 |
| 004 | ibm10-cu95 mild | 37.00 / 61.81 | 3939 / 3920 | 660.26 | 584.08 | −11.5 % | −3.8 % | 45.07 |
| 005 | ibm12-cu85 mild | 33.94 / 63.64 | 3249 / 3920 | 914.58 | 790.65 | −13.6 % | −8.7 % | 45.08 |
| 006 | ibm12-cu90 mild | 35.75 / 63.67 | 4101 / 3920 | 908.24 | 801.80 | −11.7 % | −6.5 % | 45.08 |
| 007 | ibm01-cu85 harsh | 5.99 / 32.62 | 3733 / 11131 | 98.25 | 47.94 | −51.2 % | −18.5 % | 24.31 |
| 008 | ibm01-cu88 harsh | 6.34 / 32.59 | 2959 / 10014 | 98.56 | 49.26 | −50.0 % | −15.7 % | 24.47 |
| 009 | ibm10-cu90 harsh | 35.52 / 184.75 | 3760 / 11761 | 823.95 | 581.85 | −29.4 % | −4.6 % | 45.11 |
| 010 | ibm10-cu95 harsh | 44.88 / 184.62 | 5158 / 11761 | 824.33 | 592.73 | −28.1 % | −2.4 % | 45.07 |
| 011 | ibm12-cu85 harsh | 39.67 / 189.99 | 4290 / 11761 | 1074.89 | 797.41 | −25.8 % | −7.9 % | 45.08 |
| 012 | ibm12-cu90 harsh | 41.66 / 190.16 | 5043 / 11761 | 1071.62 | 803.74 | −25.0 % | −6.3 % | 45.07 |

所有 case 的 checker 都 PASS；peak memory 最多 48.5 MB。Stage 1 ablation 的 12 case 平均（相對 Abacus only，T2）：+L1 −0.4 % / ±0 %；+post-pass −6.2 % / −12.4 %；完整 −9.4 % D_total / −21.2 % D_max。

**解讀**：Stage 1 的 D_total 只有 Dragon 參考解的 48–60 %（mild）或 18–24 %（harsh），即比「把 cell 放回原 legal 位置」更貼近 GP。D_max 在 12 個 case 中有 10 個低於 Dragon；例外是 input004 與 input006，兩者都是同一 benchmark 中 utilization 較高的 variant。SA 在相同輸入上把 HPWL 降低 11.5–51.2 %，且全部低於 Dragon 的 HPWL（−2.4 % 至 −18.5 %）。小設計（ibm01）約 20–24 s 就收斂，大設計則一律在 45 s cap 停止、仍在緩慢改善，表示此規模下 SA 受時間限制。

---

## 12. 已知限制與開放問題

1. **高 utilization variant 的 D_max 高於 Dragon**（input004：3939 vs 3920；input006：4101 vs 3920）。可能原因：`reduceMax` 只用單一 gap move 與等寬 swap，最差的 cell 一動不了就停（`Legalizer::reduceMax` 的 `break`）。可嘗試的方向：推擠鄰居的 shift chain、bottleneck matching、讓 Abacus 的 cost 考慮 cluster 內其他 cell 的位移。
2. **時間驅動造成的變異**：同一設定重複執行約差 0.4–1 %，不同機器速度也會影響結果；`--iters` 可完全重現，但評分時用的是時間驅動模式。
3. **大設計受時間限制**：可延伸為 multi-thread SA 或 SA 後的 row 內 permutation；評分機器核心數未知，目前為單執行緒。
4. **mild case 的 hot-phase hump**：input005 的 current HPWL 先上升 7.1 %，到 13.2 % 的 iterations 才回到起點以下（`report/data/sa_log_input005.csv`）；降低 T0 無效、縮小 window 更差，目前視為必要的探索成本。
5. **測資不是官方測資**：真正的評分 GP 未知；我們的 GP 由 Dragon 加雜訊產生，pad 不在 net 上。
6. **HPWL 與 Dragon 的比較只是參考**：規格要求用 cell center、忽略 pin offset，但 Dragon 優化的是含 pin offset 的 HPWL。
7. **假設**：所有 movable cell 一樣高；寬度非 site 整數倍時以 ceil 計算（偏保守，見第 4 節）。

---

## 13. 名詞表

- **GP（global placement）**：輸入的 cell 位置，可能重疊、沒有對齊 row 與 site。
- **Legalization**：把 GP 變成合法 placement（在 row 上、對齊 site、在 subrow 內、不重疊）。
- **Displacement**：cell 從 GP 到 legal 位置的 Manhattan 距離；D_total 為總和，D_max 為最大值。
- **HPWL（half-perimeter wirelength）**：每個 net 的 bounding box 半周長，此處用 cell center。
- **Segment**：subrow 被 fixed 障礙物切開後的一段 free 區間（`Layout.h:Segment`）。
- **Abacus**：Spindler 等人（ISPD'08）的 legalization 演算法，以 cluster 合併做二次 displacement 最小化的逐 row 擺放。
- **PAV（pool-adjacent-violators）**：isotonic regression 的演算法，不斷合併違反單調性的相鄰區塊；L1 版本以 median 作為區塊值。
- **D_max guard**：只有在不增加 segment 最大 displacement 時才採用 L1 新位置的條件。
- **SA（simulated annealing）**：以溫度控制接受較差解機率的隨機搜尋；T0、α、range window、acceptance rate 的定義見 7.3。
- **Optimal region**：使 cell 所連 net 的 HPWL 最小的區域，取各 net（不含該 cell）bounding box 端點的 median。
- **Generalized swap**：不同寬度的兩個 cell，在彼此的 slot（自身 site 加相鄰空位）放得下時互換。
- **Best-so-far journal / dirty flag**：記錄自上次 snapshot 後改變過的 cell，讓 best 解可以增量儲存。
- **Dragon**：學術界的 placer；這裡用它的 legal 結果製造 GP 並作為參考基準。
- **cu85 / cu90 / cu95**：ibm-place2 variant 的 core utilization（約 85 %、90 %、95 %）。
