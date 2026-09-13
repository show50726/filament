# Filament WBOIT 實驗與驗證總報告

實驗期間：2026-09-13 至 2026-09-14。整理日期：2026-09-14。實作分支：`dev/wboit-only`；完成修正與驗證的版本為 `69c6102fe`。

本報告整合本次工作的數值測試、GPU regression、shader／材質包與 bindings 檢查、效能量測，以及失敗或未採用的嘗試。內容依據已保存的程式、CSV、測試 log 與本次執行紀錄整理；沒有為撰寫報告重新量測。重複編譯與同一測試的重跑不視為新的獨立實驗。

## 1. 結論與比較對象

本次已移除額外 OIT material variant，修正已辨識的渲染語意、累加數值、資源依賴與 GLES output 精度問題，並完成 13 個 GPU regression 案例的驗證。**尚未證明 mobile performance 可接受；桌面量測也沒有顯示新版比原始 variant WBOIT 更快。**

| 本報告名稱 | 程式版本與設定 | 實際意義 |
| --- | --- | --- |
| 傳統透明路徑 | 各版本均有 `oit=0` 的量測 | 關閉 WBOIT，使用原有透明渲染 |
| 原始 variant WBOIT | `66ee6ab77`，`oit=1` | 使用額外 material variant 實作 WBOIT；**有啟用 OIT** |
| 新版 WBOIT | `4f95c2a87` 或 `da7ec3b3d`，`oit=1` | 移除 OIT variant，改由 pass uniform 切換輸出，並包含其他正確性修正 |
| 實驗分支控制組 | 僅見 uniform／RT 實驗 CSV 的 `legacy` | 指未修改的 `da7ec3b3d`，不是 `66ee6ab77` |

主效能結果使用 `da7ec3b3d` 與 `66ee6ab77`，兩版都開啟內建材質離線最佳化。`69c6102fe` 後續新增透明背景 regression 與量測文件，沒有再改動引擎渲染實作。

必須區分「版本比較」與「單一因素實驗」。新版同時改動權重、透射率、RT 格式、資源依賴、command ordering、uniform 與 shader interface，所以新版較慢的結果，**不能單獨證明 runtime uniform 一定比 material variant 慢**。原始版本本身也有已辨識的正確性缺陷，兩版數字不代表等品質的渲染比較。

## 2. 實驗環境與量測方法

| 項目 | 條件 |
| --- | --- |
| 主機／編譯器 | Windows x64，MSVC 14.51；效能使用 Release `/O2` |
| GPU／driver | NVIDIA GeForce RTX 5070 Ti，591.86 |
| 渲染環境 | OpenGL 4.5；Engine feature level 1；隱藏的 WGL surface |
| 效能畫面 | 1280 × 720，post-processing 開啟，MSAA／AA／dithering 關閉 |
| 幾何與 coverage | 重疊三角形，1／8／32 層；以 scissor 控制約 25%／100% 覆蓋率 |
| 材質 | 一個 material、多個 instance；alpha 0.1，顏色依層變化 |
| Lighting | unlit，或 lit 加一盞 directional light；效能 workload 不含 shadow／SSAO／fog |
| 每個 case | 獨立 process，60 幀 warmup，240 幀正式量測 |
| 完整矩陣 | 3 層數 × 2 覆蓋率 × 2 lighting × OIT 開／關 × 2 版本＝48 cases |
| CPU 指標 | `renderer.render()` 的 wall time，中位數及 p95 |
| GPU 指標 | Filament frame history 的 `gpuFrameDuration`，按 frame ID 去重，中位數及 p95 |

CPU 指標涵蓋 renderer frontend，**不是單獨的 command building 時間**。GPU 指標是引擎回報的整幀 duration，不是 accumulation、resolve 或 shader ALU 的獨立耗時；不可用它直接定位瓶頸。正式有效量測已排除 benchmark 的 WGL presentation 等待。

兩版的 workload 程式相同。原版沒有 `getOitStatus()`，故 CSV 的原版 `status=-1` 表示沒有這個 API，不表示 OIT 未啟用；新版 `oit=1` 必須回報 `ENABLED`（11），否則 runner 判定失敗。完整比較會先跑新版矩陣，再跑原版，沒有隨機交錯執行。

未鎖定 GPU clocks，未進行長時間 thermal soak，也沒有多輪獨立重複量測建立信賴區間。240 個 frame samples 不等於 240 次獨立實驗。因此表內差異可用於描述本次觀察，不宜把細小差異當成穩定的百分比結論，也不能把桌面結果外推成手機結果。

實作及 runner：[oit_benchmark.cpp](D:/filament/filament/test/oit_benchmark.cpp)、[benchmark.py](D:/filament/test/oit/benchmark.py)。

## 3. 效能實驗全紀錄

### 3.1 量測流程與採用狀態

| ID | 實驗 | 已保存 case 數 | 結果與採用狀態 | 原始資料 |
| --- | --- | ---: | --- | --- |
| P0 | 最初的 WGL headless benchmark | 11，執行中止 | GPU 中位數約 16.35–16.45 ms；含 presentation 等待，不能用來判斷 OIT 成本 | [CSV](D:/filament/out/oit-benchmark/results.csv) |
| P1 | benchmark 專用 platform 略過 `SwapBuffers` | 24，只有新版 | 量測不再固定在約 16.4 ms；確認流程可用，但沒有原版對照 | [CSV](D:/filament/out/oit-benchmark-no-present/results.csv) |
| P2 | 第一輪完整版本比較 | 48 | `4f95c2a87` 對 `66ee6ab77`；內建材質離線最佳化關閉，新版較慢 | [CSV](D:/filament/test/oit/desktop-results.csv) |
| P3 | GLES highp／非有限 alpha 修正後比較 | 48 | `da7ec3b3d` 對 `66ee6ab77`；離線最佳化仍關閉，退化仍存在 | [CSV](D:/filament/test/oit/desktop-final-results.csv) |
| P4 | 獨立 per-View OIT uniform snapshot | 8，實驗／控制各 4 | 未觀察到改善，還原，未提交實驗實作 | [CSV](D:/filament/test/oit/desktop-experiments.csv) 中 `snapshot` |
| P5 | weight RT 由 R16F 換成 RG16F | 8，實驗／控制各 4 | 未觀察到改善，還原，最終仍為 R16F | 同一 CSV 中 `rg16f` |
| P6 | 兩版都啟用內建材質離線最佳化 | 48 | **主要結論來源**；新版 GPU 中位數 0.397–0.677 ms，退化仍存在 | [CSV](D:/filament/test/oit/desktop-optimized-results.csv) |

P1 至 P6 共保存 184 個 case 結果；不同輪次、設定與控制組不能合併成同一統計母體。P0 另有 11 個不適合用於渲染成本比較的結果。`out/` 內資料是本地產物，未納入原先 10 筆 commit；`test/oit/` 內四份效能 CSV 已納入。

### 3.2 P0／P1：先修正計時方法

最初多個不同 workload 的 GPU duration 都接近 16.4 ms。檢查平台程式發現，WGL 的 headless swapchain 仍呼叫 `SwapBuffers`，且 frame timer 範圍包含這個階段，因此數字混入 presentation 等待。

修正方式是在 **benchmark 專用** `PlatformWGL` subclass 中覆寫 `commit()`，不進行 presentation；引擎正式的 WGL 平台實作沒有修改。P1 完成 24 個新版 case，GPU 中位數範圍降至約 0.050–0.621 ms。這驗證了計時修正的必要性，不能把這個降幅解讀成 WBOIT 演算法加速。

### 3.3 P4：獨立 uniform snapshot

假設是同一幀內反覆改寫 per-view UBO 以切換 OIT，可能增加 GL 同步或 driver 成本。實驗為每個 View 在首次使用時建立獨立 OIT UBO，複製 frame uniforms 並將 OIT flag 設為 1，OIT pass 綁定這份 snapshot，結束後恢復一般 descriptor。shader、幾何與材質保持相同。

### 3.4 P5：改用 RG16F weight attachment

假設是 R16F 與 RGBA16F 的 MRT 配置可能在此 driver 上有額外成本。實驗僅把 weight attachment 換成 RG16F，公式與 uniform 路徑保持不變；額外的 G channel 不提供所需資訊。

以下為兩個實驗的 GPU 中位數，單位 ms。控制組均為 `da7ec3b3d`，OIT 開啟、8 層、unlit，內建材質離線最佳化關閉。

| 實驗 | 覆蓋率 | 控制組 GPU p50 | 實驗 GPU p50 |
| --- | ---: | ---: | ---: |
| 獨立 uniform snapshot | 25% | 0.417 | 0.415 |
| 獨立 uniform snapshot | 100% | 0.363 | 0.501 |
| RG16F weight RT | 25% | 0.334 | 0.420 |
| RG16F weight RT | 100% | 0.491 | 0.545 |

兩個實驗都沒有提供採用理由，故完整還原。snapshot 的差異不能證明所有 uniform 最佳化都無效；RG16F 的結果也不能排除其他 GPU 有不同表現。這些實驗仍受未鎖 clocks、執行順序與單輪量測限制，尤其不能由未改善反推瓶頸已經確定在其他模組。

### 3.5 P6：最終最佳化條件的完整比較

P2／P3 使用 `FILAMENT_DISABLE_MATOPT=ON`。雖然 C++ 是 Release，內建材質仍停用離線最佳化；P6 將兩版都設為 `OFF` 後重編，避免只以該設定作最後結論。runtime material 的建立流程沿用相同 benchmark 程式。

P6 的 build 與測試均已結束才開始量測。開始前 GPU snapshot 為 36°C、graphics 990 MHz、memory 810 MHz、34.60 W；這不是整輪運行中的頻率或功耗紀錄。

下表是 P6 **所有 12 個 workload** 的 GPU 中位數（ms）。「原版／新版關閉 OIT」都是傳統渲染；「原版／新版開啟 OIT」才是兩種 WBOIT 實作。最後兩欄比較兩個 OIT-on 結果。

| 層數 | 覆蓋率 | Shading | 原版關 OIT | 新版關 OIT | 原版開 OIT | 新版開 OIT | 增加 ms | 增加 % |
| ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 25% | unlit | 0.051 | 0.052 | 0.160 | 0.405 | +0.245 | +153.3% |
| 1 | 25% | lit | 0.050 | 0.049 | 0.173 | 0.398 | +0.224 | +129.5% |
| 1 | 100% | unlit | 0.058 | 0.057 | 0.175 | 0.397 | +0.222 | +126.4% |
| 1 | 100% | lit | 0.047 | 0.047 | 0.173 | 0.404 | +0.230 | +133.0% |
| 8 | 25% | unlit | 0.050 | 0.051 | 0.176 | 0.401 | +0.226 | +128.4% |
| 8 | 25% | lit | 0.052 | 0.054 | 0.198 | 0.452 | +0.254 | +127.9% |
| 8 | 100% | unlit | 0.088 | 0.091 | 0.284 | 0.430 | +0.146 | +51.4% |
| 8 | 100% | lit | 0.110 | 0.111 | 0.267 | 0.537 | +0.270 | +101.2% |
| 32 | 25% | unlit | 0.105 | 0.104 | 0.282 | 0.504 | +0.222 | +78.8% |
| 32 | 25% | lit | 0.121 | 0.120 | 0.263 | 0.406 | +0.142 | +54.0% |
| 32 | 100% | unlit | 0.256 | 0.256 | 0.547 | 0.609 | +0.062 | +11.4% |
| 32 | 100% | lit | 0.331 | 0.339 | 0.559 | 0.677 | +0.119 | +21.3% |

同一批 OIT-on case 的 CPU frontend 中位數與 GPU p95 如下，全部單位均為 ms。原始 CSV 另外保留 OIT-off 的 CPU 與 p95。

| 層數 | 覆蓋率 | Shading | 原版 CPU p50 | 新版 CPU p50 | 原版 GPU p95 | 新版 GPU p95 |
| ---: | ---: | --- | ---: | ---: | ---: | ---: |
| 1 | 25% | unlit | 0.027 | 0.035 | 0.191 | 0.525 |
| 1 | 25% | lit | 0.032 | 0.036 | 0.209 | 0.547 |
| 1 | 100% | unlit | 0.025 | 0.036 | 0.222 | 0.500 |
| 1 | 100% | lit | 0.030 | 0.042 | 0.212 | 0.559 |
| 8 | 25% | unlit | 0.027 | 0.038 | 0.234 | 0.593 |
| 8 | 25% | lit | 0.032 | 0.049 | 0.249 | 0.595 |
| 8 | 100% | unlit | 0.031 | 0.040 | 0.328 | 0.604 |
| 8 | 100% | lit | 0.037 | 0.048 | 0.316 | 0.794 |
| 32 | 25% | unlit | 0.043 | 0.045 | 0.362 | 0.685 |
| 32 | 25% | lit | 0.043 | 0.050 | 0.332 | 0.596 |
| 32 | 100% | unlit | 0.043 | 0.043 | 0.658 | 0.835 |
| 32 | 100% | lit | 0.049 | 0.052 | 0.661 | 0.900 |

新版在這 12 個 workload 中都比原始 variant WBOIT 有較高的 GPU 中位數。離線材質最佳化沒有消除這個現象；但不同輪次缺乏鎖 clocks 與交錯重複設計，不能由 P3／P6 的微小升降判定最佳化完全沒有影響。

現有資料沒有 per-pass GPU counters、實測 DRAM traffic、shader export／register 數據或 command building 專用時間。故本次**沒有確認退化的根因**，也沒有證明它必然來自移除 variant。停用 OIT 的兩版結果亦不足以證明 mobile 上零額外成本。

## 4. 數值正確性實驗

來源：[test_numeric.py](D:/filament/test/oit/test_numeric.py)。這是 CPU 端用 Python `struct` 模擬 FP16 儲存與每次累加量化的 reference test，不是手機 GPU 精度量測。

新版使用以下 coverage 模型；`p` 是已著色的 premultiplied RGB，`a` 是 alpha，`z` 是 reversed window depth：

```text
w = (0.5 + 0.5*z)^3 / 16
RGB += clamp(p, 0, a*65504) * w / 256
T   *= 1-a
W   += a*w
resolve coverage = 1-T
resolve RGB = bounded(RGB*256/W) * coverage
```

RGBA16F 存 RGB 與 T，R16F 存 W；零分母另行處理。shader 另外處理非有限輸入及輸出範圍。不能把上述簡式當成涵蓋所有防護的完整程式。

| ID | 實驗與輸入 | 判定方式 | 結果／邊界 |
| --- | --- | --- | --- |
| N1 | 空集合；RGB 非零但 alpha=0 | 結果必須為零 coverage／零輸出 | 通過；零 coverage 的非零能量採保守捨棄 |
| N2 | 單層 alpha=0.01／0.1／0.5／1，depth=0／0.5／1；straight RGB 含 16 | 比較 premultiplied-over，容差 `max(0.003, expected*0.035)` | 通過；不是 bit-exact 等價 |
| N3 | 三個不同顏色／alpha／depth fragment 的全部 6 種排列 | 與參考排列差異不超過 0.002 | 通過；有限樣本，不是任意 draw order 的證明 |
| N4 | RGB=65504、alpha=1 的 1／16／64／256／1024 層 | 每個結果有限，coverage=1 | 通過；沒有涵蓋無限層或任意 HDR 分布 |

此設計把中間累加量縮小，而不只在最後 clamp。仍需承認 FP16 的限制：極低 alpha 的 T 可能捨入成 1，大量重疊可能丟失低位貢獻，WBOIT 本身也不是精確的透明遮擋。這些測試沒有證明任意場景的物理正確性。

## 5. 實際 GPU regression 與修正過程

來源：[oit_rendering_test.cpp](D:/filament/filament/test/oit_rendering_test.cpp)。使用真實 OpenGL engine、32×32 headless surface、runtime 材質編譯與 GPU readback；讀回 callback 必須完成，並比較整張圖。一般容差為每個 8-bit channel 2，特定案例使用 0 或 4。

| 案例 | 要捕捉的問題與實際驗證 | 結果 |
| --- | --- | --- |
| EmptyView | 空 View 開關 OIT 圖像完全相同，status 為沒有透明物件 | 通過 |
| SingleLayerAndToggle | 單層普通 blending／OIT 近似相同；關閉後回復；重複 setter 不抹除已評估狀態 | 通過 |
| MaterialOrderInvariance | 交換兩個 instance 的顏色／alpha，OIT 圖像維持在容差內 | 通過；不是大規模隨機順序壓力測試 |
| MaskedOccludesTransparent | 前方 masked 紅色遮住後方透明藍色，masked 保留正常 depth 語意 | 通過 |
| AdditiveFallback | ADD 與一般透明混用時整個 View 回退，像素與 OIT-off 完全相同 | 通過 |
| OrderingAndDepthFallback | 顯式 blend order，以及 TWO_PASSES_ONE_SIDE，分別回退並與 OIT-off 完全相同 | 通過；名稱不代表遍歷所有 depth state |
| ViewportAndScissor | 非零 viewport `(4,4,24,24)` 與 material scissor `(2,3,12,14)` | 通過；沒有額外測動態解析度 |
| MsaaFallback | 4× MSAA 要求 OIT 時回退，像素完全相同 | 通過；未證明此 GPU 真正走過 custom-resolve 特殊路徑 |
| LightingResourcesAndPostProcessing | lit opaque／transparent、directional shadow、SSAO、fog、post-processing 組合 | 通過，容差 4；不是各功能的完整開關排列 |
| SecondViewDoesNotInheritOitUniform | 一個 View 用過 OIT 後，另一個 View 關閉 OIT 的結果不被污染 | 通過 |
| ZeroAlphaAndTwoSided | 零 alpha 無貢獻；TWO_PASSES_TWO_SIDES 可執行且與參考一致 | 通過；未窮舉封閉模型的所有前後面重疊 |
| NonFiniteCoverageIsEmpty | NaN／Infinity alpha 不污染畫面 | 通過 |
| TransparentBackgroundPreservesCoverage | 透明中間結果合成到先前藍色 View，驗證紅／藍比例約 64／128 | 通過 |

最終證據是 [12 案例整組 log](D:/filament/out/oit-gpu-tests.log) 加上 [透明背景單例 log](D:/filament/out/oit-alpha-test.log)，共 13 個不同案例；不是宣稱最後保存的單一 log 包含 13 案例。

### 驗證中找到並處理的問題

最初 eligibility 判斷曾把預設 render channel 當成 0，GPU 測試暴露出不應發生的 fallback。實際預設值是 `RenderableManager::Builder::DEFAULT_CHANNEL`（2），已改用常數，並補上 alpha-to-coverage 的保守回退。

最後的 GLES shader 檢查發現，雖然累加暫存值是 `highp`，fragment outputs 仍繼承 `mediump float`。這可能在寫入 FP16 RT 前先截斷小的 weighted 值。兩個 eligible output 已明確加上 `highp`，並加入生成 shader 的 qualifier 檢查；另把非有限 alpha 定義為空 coverage，通過上述 GPU 測試。GLES 實機效果仍未量測。

透明背景測試第一版直接要求 window readback alpha 約 128，實際得到 255，而普通 blending 參考也得到相同結果。這個測試不能辨別中間結果是否保留 alpha，因為該 WGL window 沒有可用的 alpha readback。測試改為兩個 View 的實際合成，從背景藍色保留比例驗證 coverage；最終測試通過。第一版失敗與修改原因來自本次執行紀錄，該 log 路徑後來已被成功重跑覆寫。

## 6. Shader、材質格式與 bindings 驗證

### Shader／材質包：6 組編譯與載入案例

在完整矩陣之前，也曾先對單一 unlit transparent 材質進行 FL0／FL1 編譯 smoke check，產生兩份 filamat；紀錄保留於 [FL0 log](D:/filament/out/oit-matc-fl0.log) 與 [FL1 log](D:/filament/out/oit-matc-fl1.log)。後續六組矩陣提供更明確的 interface 與載入 assertion，早期 smoke check 不另算成六組以外的完整 regression coverage。

[check_shaders.py](D:/filament/test/oit/check_shaders.py) 測試 feature level 0／1 × opaque／transparent／fade，共 6 組。FL1 編譯所有 API，FL0 編譯 OpenGL。檢查版本 72、variant key 不超過 255、eligible GLSL 的第二個 output 與 highp qualifier，以及 Metal 文字沒有新增 color(1)。6 組均通過：[log](D:/filament/out/oit-shader-tests.log)。

FL0 檢查實際 ESSL1 chunk 沒有 MRT output；FL0 package 也可含供高 feature-level engine 使用的升級 shader。因此這不等於「在真正 FL0 GPU 上渲染通過」。恢復的 8-bit key 與 base valid fragment／vertex counts 24／36 是實作結構資訊，不代表每個材質包固定只有這些 shader，也不是 shader 編譯時間或包大小的實測。

版本拒絕測試使用真實 OpenGL loader，將產生的版本 72 package 的版本欄位改成 71，要求載入失敗並回報 `Material version mismatch`。這驗證版本門禁，**不是拿原始 16-bit package 成功 round-trip，也不是宣稱新 loader 能讀取舊索引格式**；本次策略就是拒絕舊的實驗版本。測試亦確認不應用 NOOP loader 作版本拒絕證據，因為它會略過該檢查。

曾嘗試透過本機 matinfo 解碼 SPIR-V／轉成 Vulkan GLSL，但此 build 的 `FILAMENT_SUPPORTS_VULKAN=OFF`，相關 dictionary reader 不具解碼能力；該嘗試沒有成為有效驗證。最終明確使用 `--skip-spirv-inspection`：Vulkan shader 編譯有執行，SPIR-V reflection 檢查跳過，Vulkan／Metal runtime 未測。不能因為跳過就宣稱這些 backend 的 OIT 合法性已通過。

### Bindings：2 個來源一致性測試

[test_bindings.py](D:/filament/test/oit/test_bindings.py) 檢查 C++／Java／TypeScript／Embind 的 status 名稱與順序，以及 `setOitEnabled`、`isOitEnabled`、`getOitStatus` 的 Java/JNI 與 Embind entry points，兩項通過。這是來源文字與 enum mapping 檢查，不是 Java、JNI、TypeScript 或 WASM 編譯驗證。Viewer 的 C++ UI 已隨 desktop build 編譯。

## 7. 建置、核心測試與工具限制

| 驗證 | 實際結果 | 解讀／證據 |
| --- | --- | --- |
| 完整 desktop Debug | 成功；保留既有 compiler／linker／shader optimizer warnings | [build log](D:/filament/out/oit-build.log)，不是零警告 |
| 最終與原版 Release benchmark | 兩版成功，包含離線材質最佳化開啟的 build | [新版](D:/filament/out/oit-optimized-build.log)、[原版](D:/filament/out/oit-legacy-optimized-build.log) |
| Utils | 176 個中 175 通過；WinPathTest.Split 失敗 | [log](D:/filament/out/oit-utils-tests.log)，Windows drive-root 字串含尾端分隔符的期待不一致；該來源未由本次修改 |
| 未過濾 FrameGraph | WriteRead 在 precondition 終止 | [log](D:/filament/out/oit-fg-tests.log)，未走到測試期待的 exception |
| 安全子集 FrameGraph | 11 個通過 | [log](D:/filament/out/oit-fg-safe-tests.log) |
| 過濾後核心 Filament | 125 個通過 | [log](D:/filament/out/oit-core-tests.log)；排除 WriteRead、Basic、ImportResource、SubResourcesWrite |
| Material parser | 1 個通過 | [log](D:/filament/out/oit-parser-tests.log) |
| Python focused tests | 4 個數值＋2 個 bindings 通過 | 上述兩份測試程式；不可再算成另外 6 個 GPU 案例 |

不能把 11 個 FrameGraph 子集與 125 個核心案例相加成獨立 coverage，也不能宣稱核心測試全綠。四個排除項目的理由是例外／precondition 測試在此環境的執行問題，不代表四個都已獨立執行並觀察到同一失敗。

本地沒有可用的 Bash、Android SDK／adb、Java／WASM 工具鏈供既定流程使用，Nsight CLI 也未在 PATH 找到。建置改用既有 Visual Studio CMake build；header formatter 與適用規範由本地 `dev/variant-test` 讀取，沒有把其他分支的 skills 檔案混入實作。部分命令最初受 sandbox／MSBuild FileTracker 限制，改由核准的建置執行完成；此類環境失敗不當成渲染缺陷。

## 8. 哪些 concern 已有證據，哪些尚未驗證

| concern | 本次證據與決策 | 尚未涵蓋 |
| --- | --- | --- |
| 材質分類／fallback | masked、ADD、blend order、雙 pass、MSAA 有 GPU 比較；不支援組合整個 View 回退 | 未逐一像素驗證所有 blending、refraction、stencil、channel、stereo 組合 |
| 數值穩定性 | FP16 reference、單層 GPU、NaN／Inf coverage、highp interface 檢查 | 任意 HDR／大量重疊的 GPU 壓力驗證；真正 mobile 精度 |
| Frame graph／描述資源 | 顯式宣告依賴；lighting/shadow/SSAO/fog 組合 regression 通過 | 沒有專門的 aliasing 壓力實驗、barrier trace 或資源生命週期 counter |
| 空 OIT pass 成本 | 程式在沒有 OIT commands 時略過 OIT RT／resolve；EmptyView 像素及 status 通過 | 沒有 allocation counter 證明零配置成本；不能說整個 renderer 零工作 |
| 移除 variant | 8-bit key、版本 72、shader interface 與載入門禁檢查 | 不等於已量測所有材質的 shader 數、包大小、編譯成本 |
| viewport／scissor／多 View | 對應 GPU regression 與透明中間結果合成通過 | 動態解析度、stereo、任意多 View 配置未完整測試 |
| bindings／status | enum／entry point 來源一致性；部分狀態與逐幀切換 GPU 驗證 | Java/JNI/WASM 編譯、所有 fallback 優先順序排列尚未驗證 |
| mobile performance | 已交付可重跑 workload 與桌面數據；功能保持 opt-in | 尚無實機數據、頻寬計數器、指定 frame budget；不能宣稱可接受 |

目前有效 OIT 路徑限支援所需 RT 格式的 OpenGL／GLES feature level 1 以上環境。其他 backend、MSAA、stereo、折射、特殊 blending／ordering／depth／stencil 等配置採回退。GLES 是實作支援範圍，不代表本次已在 GLES 手機上通過驗證。

RT 容量為 RGBA16F 8 bytes＋R16F 2 bytes＝10 bytes/pixel。1920×1080 約 20.7 MB，假設完整 store 再 read 約 41.5 MB/frame；這**只是兩張 RT 的容量與理想化流量估算**，不包括其他 buffers，也不是實測 DRAM traffic。Tile storage、壓縮、overdraw、額外 pass 與同步均可能改變實際成本。

## 9. 重跑方式與後續要回答的問題

以下是 Windows 本地重跑主效能比較的方式。`--legacy-executable` 指原始 variant WBOIT 的 executable，不是 OIT-off executable；runner 會自行對兩版分別跑開與關。

```powershell
cmake -S . -B out -DFILAMENT_DISABLE_MATOPT=OFF
cmake --build out --config Release --target benchmark_oit --parallel 8
python test/oit/benchmark.py --executable out/filament/test/Release/benchmark_oit.exe --legacy-executable out/oit-legacy-build/filament/test/Release/benchmark_oit.exe --output out/oit-report-rerun --width 1280 --height 720 --frames 240
```

原版需在 `66ee6ab77` 的獨立 source/build 中加入相同 benchmark source，連結 `filamat` 與 `filament`，不要定義 `FILAMENT_OIT_HAS_STATUS`，並同樣設定 `FILAMENT_DISABLE_MATOPT=OFF`。上述原版 executable 路徑是本次已建立的本地 build 產物。

```powershell
cmake --build out --config Debug --parallel 8
out/filament/test/Debug/test_oit.exe
python -m unittest discover -s test/oit -p test_*.py
python test/oit/check_shaders.py --matc out/tools/matc/Debug/matc.exe --matinfo out/tools/matinfo/Debug/matinfo.exe --loader out/filament/test/Debug/test_oit.exe --output out/oit-report-shaders --skip-spirv-inspection
```

有 Vulkan 解碼能力的 matinfo 應移除最後的 skip flag。Android Release build／adb 部署指令見[操作文件](D:/filament/test/oit/README.md)。本次沒有執行成功的 Android build、部署或效能量測可供報告。

後續最有價值的是在目標 mobile GPU 上定義 frame budget，量測 accumulation／resolve／driver submission 與 DRAM／tile counters，並以相同 workload 交錯重複測量兩版。同時需要一個保留新版正確性修正、但仍使用 variant 的控制版本，才能隔離「variant 對 runtime uniform」本身的成本。這些是尚未做的實驗，不是本報告已有的成果。

## 10. 實作提交對照

原先修正系列共 10 筆 commit，沒有 push。以下用於追溯實驗引發的修改；本報告另外整理，未改寫該提交系列。

| Commit | 內容 |
| --- | --- |
| `54922066d` | eligibility、整個 View fallback 與 requested／effective 區分 |
| `69debc57c` | 乘法透射率、R16F weight、有界 FP16 累加 |
| `95bd2e6d4` | frame graph 依賴、descriptor 與 callback 隔離 |
| `63cc07148` | 移除 OIT variant、恢復 8-bit key、材質版本 72 |
| `41e63aaec` | viewport／intermediate target、單一 command buffer 範圍及排序 |
| `040443e79` | C++／Java／JNI／JS／TS 狀態 API 與 viewer |
| `4f95c2a87` | GPU regression、shader／材質包檢查 |
| `44726d64b` | benchmark、原版比較與支援限制文件 |
| `da7ec3b3d` | GLES highp outputs、非有限 alpha 防護 |
| `69c6102fe` | 透明背景 regression、最終量測及未採用實驗紀錄 |

本次可支持的結論是：已建立不新增 OIT material variant 的實作及有限範圍的正確性證據，也已量到桌面效能退化；**退化根因與 mobile 可接受性仍是待解問題**。
