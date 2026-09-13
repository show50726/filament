# OIT 架構問題追蹤

建立日期：2026-09-28。最後更新：2026-09-28。

本文件追蹤目前 2-pass OIT 相對原生 Filament 的系統影響，供後續逐項修正與驗證使用。初始 review 對象為 `dev/wboit-only` 上、HEAD `57dafdf51` 之後尚未提交的 2-pass 改造；比較基準為 main `ef1a133d6`。HEAD 本身仍是 MRT 版本，不能只 checkout 此 HEAD 就重現本次 review 的程式。

初始結果來自靜態程式追蹤，不是新的效能測量。先前渲染測試通過不代表下列架構與成本問題已解決。修正時須重新核對現況；檔案位置以符號為準，避免行號變更後失效。

## 更新規則

每個 ID 永久保留。狀態使用「待處理」「進行中」「待驗證」「已解決」「接受取捨」「暫緩」。開始工作時更新狀態，完成時記錄改動、驗證命令、結果、環境與日期；若有提交，再補 commit hash。未實際執行的驗證必須列為未驗證，不能以程式已修改代替驗證通過。

「接受取捨」須記錄使用者決定、適用範圍與限制；不能由實作者自行將未解決問題關閉。修正採用當時有效的 repository skills。建立本文件不代表授權 commit 或 push，目前維持不 commit、不 push。

## 總表

| ID | 類別／優先級 | 問題 | 狀態 |
|---|---|---|---|
| OIT-001 | efficiency／P1 | 不使用 OIT 的材質也承擔四倍稠密 program cache 容量 | 待處理 |
| OIT-002 | efficiency／P2 | 一般預編譯 API 無法排除 OIT programs | 待處理 |
| OIT-003 | behavior-change／P2 | OIT 回退前已改變 viewport padding | 待處理 |
| OIT-004 | efficiency／P2 | eligibility 額外串行遍歷可見 primitives | 已解決 |
| OIT-005 | architecture／P2 | 借用 render channel 分流，Executor 隱式改選 program | 待處理 |
| OIT-006 | behavior-change／P2 | 未啟用 OIT 的透明 shader 也改成 highp output | 待處理 |
| OIT-007 | efficiency／高影響取捨 | 兩次幾何繪製、兩張累積貼圖與 subpass fusion 損失 | 待處理 |
| OIT-008 | api-contract／P2 | Viewer 測試選項擴散為 gltfio 公開 API | 待處理 |

建議先處理 OIT-001、OIT-002、OIT-003，再收斂 command 分流與 mobile 成本。OIT-007 的 2-pass 方向已由使用者選定，但其效能可接受性尚未驗證，因此不能標為已解決或全面接受成本。

## OIT-001：所有材質的 program cache 容量增加

**位置：** `filament/src/DynamicSpecConstKey.h` 的 `DYNAMIC_SPEC_CONST_KEY_BITS`，以及 `filament/src/LocalProgramCache.cpp` 的 `getCacheSize()`、`initializeForMaterial()`、`initializeForMaterialInstance()`。

Cache 以 `2^(variant bits + spec constant bits)` 配置完整陣列。相對 main，新增兩個 OIT bits 讓 surface slots 從 1,024 增為 4,096，post-process slots 從 16 增為 64，無論材質是否支援 OIT、View 是否要求 OIT 都會增加。以目前 4-byte handle 計算，每份 surface cache 約由 4 KiB 增至 16 KiB；這是 handle 陣列成本，不代表 GPU programs 全部編譯四倍。一般 MaterialInstance 共用 Material cache，覆寫 specialization constants 的 instance 才建立自己的 cache。

先前「cache 容量不變」只適用於與舊 9-bit MRT 版本比較，不能用來描述相對 main 的成本。

**解決條件：** 不支援 OIT 的材質不因 OIT 無條件擴大 cache；OIT 的額外容量可以明確歸屬到需要它的材質／program。可評估緊密索引或按需配置，尚未指定方案。

**驗證：** 比較 opaque、fade、transparent、post-process 與覆寫 constants 的 instances 的實際配置容量；驗證普通、accumulation、weight 與 lighting keys 不碰撞，並涵蓋 depth／SSR、cache 複製、失效與釋放。記憶體估算與實測需分開記錄。

## OIT-002：預編譯無法排除 OIT programs

**位置：** `filament/src/details/MaterialInstance.cpp` 的 `compile()`，`filament/src/DynamicSpecConstKey.h` 的 `getValidKeys()`／`filterUserVariant()`，`filament/src/MaterialDefinition.cpp` 的 `isValidProgram()`。

一般 `Material::compile()` 會進入完整 specialization 枚舉。符合 OIT 條件的普通非 stereo lit variant，其 lighting keys 從 6 組變為 18 組 lighting × 輸出模式。現有 UserVariantFilterMask 沒有 OIT 排除選項，這條路徑也未按 runtime backend 的 OIT 支援情況限制。未使用 OIT 的應用可能增加預編譯工作；目前禁用 OIT 的 backend 也可能編譯這些 programs。18／6 描述特定 standard variant，不能宣稱整份材質總編譯量一定為三倍。

**解決條件：** 預編譯範圍可明確選擇普通或 OIT programs，同時保留提前準備 OIT、避免首次切換卡頓的能力。一般無 View 的預編譯 API 與有 View 的預編譯路徑都需定義語意。

**驗證：** 記錄实际 program preparation／編譯數，涵蓋不使用 OIT、預先要求 OIT、執行中切換、lighting filter 與不支援 OIT 的 backend。不能僅檢查 material package 的 shader entry 數量。

## OIT-003：回退仍改變 viewport padding

**位置：** `filament/src/details/Renderer.cpp` 的 `noBufferPadding`、`evaluateOitConfiguration()` 與 `colorGradingConfig` 決策。

Padding 使用 requested 狀態 `view.isOitEnabled()`，實際 eligibility 卻在 `view.prepare()` 後才確定。在原本可省略 padding、viewport 又不是 16 倍數的配置中，只要求 OIT 就可能擴大渲染尺寸、改變投影與後續拷貝路徑；即使最後回退，前述變更仍保留。這是成本／路徑不等價，尚未據此證明必然畫錯。

**解決條件：** 回退或沒有 OIT commands 時，維持原本可用的 viewport／中間處理最佳化；若某些差異不可避免，須有明確理由與量測。

**驗證：** 使用非 16 倍數 viewport，比較 OIT 關閉與 requested-but-fallback 的渲染尺寸、pass／blit 數與影像，涵蓋沒有候選物件、功能回退、post-processing 開關與非零 viewport origin。

## OIT-004：額外的串行 eligibility 掃描

**位置：** `filament/src/details/Renderer.cpp` 的 `evaluateOitConfiguration()` 與 color pass 建立流程；`filament/src/RenderPass.cpp` 的 `appendCommands()`、`generateCommandsImpl()` 與 `complete()`。

修正前先遍歷可見 renderables／primitives，回查 RenderableManager 與材質，再進入原本使用 SoA 與 jobs 的 command generation，後者仍會重新判斷 OIT eligibility。這比遍歷整個 Scene 好，但對大量小物件、多 primitive、多 View 有額外 CPU 成本，也偏離原本盡量合併遍歷的方向。

**解決條件：** 重用既有資料準備／分類工作，或用量測證明保留掃描的成本可接受。不得為此任意引入使用者已反對的大型 summary structs；保留目前明確、簡單的 View 開關與材質分類語意。

**驗證：** 固定 workload 測量 eligibility 與 command 建立時間，涵蓋大量可見透明、混合材質、多 View 與大量不可見物件；確認 visibility、折射與 ordering 的回退優先順序不變。

**2026-09-28 修改：** 已移除 Renderer 的 geometry eligibility 預掃描。View／device 限制先行判斷；只有配置允許嘗試 OIT 時，既有 color command jobs 才從 SoA 與正在處理的 primitive 收集三個 flags。每個 job 在本地彙整，再以一次 relaxed atomic OR 合併；沿用 `runAndWait()` 保證 command writes 已完成，主執行緒不會在 jobs 仍執行時讀取 commands。

Jobs 保留普通 commands 的 raster state 與排序 key。等全 View 結果確定後，既有 program preparation 遍歷才將候選 commands 改成 OIT，並準備 accumulation／weight programs。候選標記目前暫用既有 accumulation program key，回退時在 preparation 前還原；不新增普通 commands 的候選欄位，維持 56-byte PrimitiveInfo／64-byte Command。Color-grading／custom-resolve callbacks 在決策後才選擇，然後只排序、automatic instanceify 一次；兩個 OIT passes 仍重用同一範圍。

`buildUnprepared()` 與 `complete()` 之間不得再從同一 command arena 配置其他 pass，因為完成時會 rewind；Debug assertion 檢查一般 arena 配置的尾端，並檢查預留 callback 容量。其他 passes 的 `build()` 仍立即完成。未新增公開 API 或大型 summary structs，未修改 shader／RT 配置。OIT-003 的 requested-state padding 尚未處理。

**驗證結果（2026-09-28，Windows／MSVC 18／OpenGL，Debug）：** `python tools/reorganize-headers/run.py` 已檢查本次四個 C++ 檔案；`cmake --build out --config Debug --parallel 8 --target filament test_oit test_filament gltf_viewer benchmark_oit` 成功。`RenderPass.cpp` 以自己的 header 作為第一個 include，編譯亦涵蓋 header self-containment；56／64-byte 的 static assertions 通過。

`out/filament/test/Debug/test_oit.exe --gtest_output=xml:out/oit-004-rendering.xml`：23／23 通過。新增 `ParallelEligibilityPreservesFallbackAndResetsEachFrame` 用 257 個 renderables 跨越 jobs 分割門檻，確認 ordering／refraction 回退與關閉 OIT 的完整影像逐 byte 相同、refraction 優先、移除限制後恢復 OIT、隱藏所有物件後清除候選狀態。新增 `AutomaticInstancingAfterOitDecision` 使用共用 instance 的 129 個雙面 renderables，比較 instancing 開關結果，並確認 OIT 不修改材質的 depth-write 設定、ordering 回退影像仍一致。其餘測試涵蓋空場景、FL0、masked、FADE／additive、offscreen ordering、MSAA、shadow／SSAO／fog、多 View 與非零 viewport／scissor。

`out/filament/test/Debug/test_filament.exe --gtest_filter=-BufferBoundsTest.*Rejects*:VertexBufferTest.CanceledCreationRejected*:FrameGraphTest.WriteRead:FrameGraphTest.Basic:FrameGraphTest.ImportResource:FrameGraphTest.SubResourcesWrite --gtest_output=xml:out/oit-004-filament.xml`：218 項中 217 通過；沿用先前 Windows 過濾範圍，不能視為完整核心套件全通過。既有 `LocalProgramCacheRegressionDeathTest.SurfaceVariantOnPostProcessMaterialIsRejected` 仍因期待 `utils::PostconditionPanic`、實際輸出 `Postcondition` 而失敗；非法索引確實被拒絕。

`cmake --build out --config Debug --parallel 8 --target test_utils` 成功；`out/libs/utils/Debug/test_utils.exe --gtest_output=xml:out/oit-004-utils.xml`：318 項中 313 通過、1 跳過、4 既有失敗（`AllocatorTest.LeakDetectorWithLeaksOnRewind`、`JobSystem.JobSystemLostWakeupRace`、`JobSystem.JobPoolExhaustionNullJobRun`、`WinPathTest.Split`）。這些不是 OIT 測試通過項目，本次未修改相應 utils 程式。

環境沒有 Bash，完整 desktop 驗證以 `cmake --build out --config Debug --parallel 8` 執行；仍遇到未修改的 `libs/gltfio/test/gltfio_test.cpp:55` 包含 `unistd.h` 而無法在 MSVC 編譯。完整 build 不記為通過，既有 MSVC warnings 也仍存在。詳細 log／XML 保留於本地 `out/oit-004-*`。

**尚未驗證：** CPU command 建立時間的 Release 前後比較、mobile 效能，以及依賴 framebuffer-fetch 的 color-grading／MSAA custom-resolve 實機分支。一般 OpenGL 測試通過不能替代這些驗證；本項結案表示額外 eligibility 掃描已移除且功能回歸通過，不宣稱已量測到加速。未 commit、未 push。

**關閉成本優先（2026-09-28 後續修正）：** 使用者指定完全關閉時不應承擔額外成本。本次先針對 command generation 與 preparation 處理：每個 pass 選擇 ordinary／OIT 的 C++ specialization，內層以 `if constexpr` 排除 OIT 分類；普通 job 沒有 flags 輸出參數或回傳值，不建立／更新 atomic，也不初始化候選欄位。Depth／shadow 沿用普通 specialization。Program preparation 在迴圈外分成普通、OIT、候選回退三條路，普通迴圈只有原本的 program preparation，不檢查 OIT bit，也不準備 weight program。串行 OIT generation 直接取得 flags，移除原先不必要的 atomic OR。

Release／MSVC `/O2 /Ob2` 的單檔組語檢查，已確認 ordinary `appendCommands<false>` 沒有 OIT atomic 操作、ordinary generation 不再傳遞 flags scratch argument，普通 preparation 沒有 OIT bit 分支或額外 weight preparation。產物在本地 `out/oit-disabled-release.asm` 與擷取的 `out/oit-disabled-normal-*.asm`；這是程式碼路徑檢查，不是 CPU frame-time benchmark，也不是跨編譯器指令完全等價的證明。後續 Debug 驗證已完成：`cmake --build out --config Debug --parallel 8 --target filament test_oit test_filament test_utils` 成功；完整 desktop build 仍因原有 `gltfio_test.cpp:55` 的 `unistd.h` 失敗。最終程式的 OIT 測試 23／23 通過；同一 Windows 過濾範圍內核心測試 217／218 通過，仍為上列 death-test 文字差異；utils 313 通過、1 跳過、4 項同前失敗。Log／XML 使用 `out/oit-disabled-*` 前綴。Headers formatter 與 `git diff --check` 通過；未 commit、未 push。

**整體零成本尚未成立：** pass 邊界仍有模式選擇與 RenderPass 的固定 bookkeeping；C++ specialization 增加靜態程式碼，對 code layout／instruction cache 的影響未量測。OIT-001／002／006 的 cache、預編譯、shader precision 影響也仍存在，不能因內層迴圈已分流就宣稱整個 OIT 關閉後與 main 完全零差異。

## OIT-005：channel 與 Executor 的隱含耦合

**位置：** `filament/src/RenderPass.cpp` 的 OIT key 建立與 `Executor::execute()`；`filament/src/details/Renderer.cpp` 的 `oitBegin` 分割。

OIT commands 被放入最後一個 channel，再以數值門檻切出範圍。通用 Executor 接受 `oitWeight`，執行時改選 command 的 program key。這讓使用者 channel 同時代表內部 pass 身分，而相同 command 的实际 program 取決於外部執行參數。

目前 eligibility 排除非預設 channel，相關 color-grading callbacks 也受限制，因此尚未確認現行排序 bug；問題是這些跨函式假設脆弱，未來擴充容易漏改。

**解決條件：** 內部 pass 分流與使用者 channel 語意明確分離；執行模式與 program preparation 的關係明確且可驗證。繼續共用 immutable command range，不必重建兩份完整 commands。

**驗證：** 覆蓋 command 邊界、空 OIT range、雙面、automatic instancing、custom callbacks、channel／priority／blend-order 回退，以及兩個 pass 的 program 都已準備完成。不得讓 callbacks 被重複執行。

## OIT-006：普通透明 shader 的精度改變

**位置：** `shaders/src/surface_main.fs` 的 `HAS_OIT_OUTPUT` 與 output 宣告。

符合條件的 TRANSPARENT 材質一律使用 highp output，與 View 是否啟用 OIT 無關。Spec constant 為 false 並不恢復 main 原本的 output precision。這是共用 shader 介面且避免 MRT output 的取捨，不代表已確認效能退化，但普通透明路徑的數值與 mobile 編譯結果也會受影響。

**解決條件：** 決定並記錄共用 highp 介面是否接受，或提出維持單一 output、且符合目前 shader 架構的替代方法；不能在未量測下宣稱零成本。

**驗證：** 在 mobile 比較 OIT 關閉時的 shader、影像、GPU 時間及可取得的編譯統計，包含 lit／unlit、HDR 與低 alpha。不得因本問題重新引入未使用的第二個 output。

## OIT-007：2-pass 的完整 mobile 成本

**位置：** `filament/src/PostProcessManager.cpp` 的 `oitPass()`／`oitResolve()`；`filament/src/details/Renderer.cpp` 的 subpass 與 intermediate buffer 決策。

每個 pass 只有一個 color attachment，但仍需要 RGBA16F 與 R16F 兩張貼圖，resolve 同時讀取它們，所以不能彼此 alias。邏輯儲存量仍為 10 bytes/pixel，1080p 約 20.7 MB，不含 driver padding，這不是實測 DRAM 流量。幾何與 rasterization 執行兩次；weight specialization 可能移除 RGB-only 計算，不能直接推論完整光照成本加倍，也不能假定第二次繪製廉價。

此外，OIT 停用 color-grading subpass fusion；即使 post-processing 關閉，也需要 intermediate color。這些成本在 mobile 上可能比 attachment 數量更重要。2-pass 方向已由使用者選定，效能可接受性尚未確認。

**解決條件：** 用 Release 實機數據建立可接受範圍；有退化時完成針對性修正，或由使用者明確接受指定 workload／裝置的取捨。不能僅以 desktop Debug 測試通過關閉本項。

**驗證：** 在相同畫質／workload 下比較傳統透明、先前 MRT 與 2-pass，涵蓋覆蓋率、層數、lighting 複雜度、雙面及後處理配置。記錄 GPU／CPU 時間、RT 配置與可取得的頻寬資料。缺少裝置時保留未驗證狀態，不將儲存量估算當成實測頻寬。

## OIT-008：測試用選項成為公開 gltfio API

**位置：** `libs/gltfio/include/gltfio/MaterialProvider.h` 的四參數 `createJitShaderProvider()` overload；`libs/gltfio/src/JitShaderProvider.cpp`。

`transparentBlendForTesting` 透過 UTILS_PUBLIC overload 對外公開，將只供 OIT 比較的 BLEND → TRANSPARENT 轉換變成 library API。舊 overload 仍保留，因此不是已確認的既有 ABI 破壞，但增加了公開介面與維護責任，也暴露改變 glTF 材質語意的測試行為。

**解決條件：** 保留 gltf_viewer 的測試便利性，同時把控制限制在 sample／內部測試介面，或經明確決策改成有正式用途與契約的公開能力。

**驗證：** 一般 glTF BLEND 仍載入成 FADE；只有明確測試選項才轉成 TRANSPARENT。檢查既有呼叫端、材質 cache 隔離與 viewer 測試開關，避免跨 provider 混用不同語意的材質。

## 已接受的範圍與已排除的疑慮

目前僅啟用 OpenGL／GLES runtime OIT，MSAA、stereo、自訂 RenderTarget 等配置仍會回退。FADE／additive 等留在 color pass，與 OIT 採固定 pass 合成順序；OIT 候選 commands 禁止 depth write。這些是目前明確限定或先前同意的行為，不因本次 review 自動改變。

材質版本升級要求重編，符合拒絕誤讀舊格式的目的。靜態追蹤目前未發現共用 command range 的生命週期錯誤、program preparation 的新跨執行緒問題，或兩個 OIT pass 的主要 depth／採樣資源依賴遺漏。這是本次已檢查範圍，不是對未來修改的永久保證。

## 相關未結案調查：最終截圖的 1 色階差異

先前 MRT 與 2-pass 截圖最大差異為一個 8-bit 色階，但同一 2-pass 程式跨次執行也有相同量級的差異，因此尚不能歸因於改造。若要定位，需要固定 draw order 與場景輸入，比較累積 RT、resolve 後與 tone mapping 前後的結果。不得僅因差異小就宣稱已證明等價或無害；此調查與 OIT-006、OIT-007 的驗證相關。

## 進度紀錄

| 日期 | ID | 更新 | 驗證／提交 |
|---|---|---|---|
| 2026-09-28 | 全部 | 建立追蹤檔，保存八項 review findings、解決條件與驗證要求 | 靜態 review；本次未修改 code、未 commit、未 push |
| 2026-09-28 | OIT-004 | 移除額外 eligibility 掃描，併入 command jobs，於 program preparation 轉換；更新為已解決 | OIT 23／23；Debug focused targets 通過；完整 build／核心與 utils 既有失敗及效能未驗證項目見本節；未 commit、未 push |
| 2026-09-28 | OIT-004／關閉成本 | 分離 ordinary generation／preparation；移除普通 job flags 參數與回傳值，候選改用既有 program key；串行 OIT 不使用 atomic | MSVC 最佳化組語檢查；OIT 23／23；其餘驗證限制見本節。整體零成本尚未驗證；未 commit、未 push |
