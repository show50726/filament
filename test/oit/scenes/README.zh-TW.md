# OIT glTF 測試場景

建議開啟 `oit-review.glb`，它與 `oit-review.gltf` 是相同場景；兩者都內嵌所有 buffer 與 checker PNG，不需要額外貼圖。共 82 個 meshes，使用 unlit 材質，避免光源設定干擾透明比較。標準 glTF 的 baseColorFactor 使用 straight alpha，載入器負責轉換。

在 D:\filament 執行：

```powershell
out/samples/Debug/gltf_viewer.exe -a opengl --oit-test-materials test/oit/scenes/oit-review.glb
```

在 viewer 的 View 設定切換 OIT，確認 effective status 是 ENABLED。保持 MSAA 關閉；先關閉 TAA、FXAA 方便比較，再分別開啟。`--oit-test-materials` 只改 BLEND 的載入材質，不會自動開啟 OIT。它強制使用 JIT provider，即使同時傳入 --ubershader。一般 glTF 載入仍使用 FADE；若省略此測試開關，這個場景不會產生 OIT candidates。

從正面看，測試區排列如下，各區底部白色短線數量是編號：

| 位置 | 測試 | 觀察 |
|---|---|---|
| 左上 1 | 紅綠藍交叉薄片 | 旋轉觀察排序跳動；OIT 應穩定，但不保證精確混色 |
| 中上 2 | 互相穿插的雙面球殼 | 自身前後面與兩球交錯，檢查內外面貢獻 |
| 右上 3 | 32 層、每層 alpha 0.035 | 低 alpha 累積，不應因權重下溢消失 |
| 左下 4 | 透明薄片與 opaque 黃柱 | 黃柱擋住後方透明，前方紅片仍覆蓋黃柱 |
| 中下 5 | MASK checker 與前後透明片 | 孔洞可看見後方紅色，被保留的格子正常遮擋 |
| 右下 6 | 12 個交錯彩色粒子卡片 | 相機移動時比較物件排序與 WBOIT 的近似結果 |

所有背景板是 OPAQUE，位在透明物件後面；沒有 transmission/refraction 擴充，避免整個 View 回退。這個資產不是 HDR、stencil 或 FADE/additive 測試；glTF 沒有對應的通用 blending 設定。

可重跑的批次比較（在輸出資料夾執行以收集截圖）：

```powershell
D:/filament/out/samples/Debug/gltf_viewer.exe -a opengl --oit-test-materials -s --headless --screenshot-as-ppm --batch=D:/filament/test/oit/scenes/compare.json D:/filament/test/oit/scenes/oit-review.glb
```

重建資產：`python test/oit/scenes/generate_scene.py`。測試開關改變了 glTF 的材質語意，僅用於此 OIT review，不應用作產品的預設載入模式。

驗證：Debug gltf_viewer 已編譯通過，GLB 實際載入並完成兩次批次渲染；測試開關下日誌確認 `requested=1 effective=1`，且 OIT 開／關影像不同。未加測試開關的 `.gltf` 也已實際載入，其開／關影像相同，符合 FADE 留在 color pass 的預期。原始 GLB headers、buffer 範圍與 mesh indices 已檢查；未執行 Khronos glTF Validator。

預覽圖位於 `D:/filament/out/oit-scene-preview/oit-off0.png` 與 `oit-on1.png`。Viewer 的 JSON 設定已補上 oitEnabled 讀寫；overdraw 的 stencil write 改成只在啟用 overdraw 時開啟，以免所有 glTF 材質都被排除於 OIT。此次未 commit、未 push。

2026-09-26 rebase：main 的 App 在 pre-render 後重新設定鏡頭，批次設定改用明確的 33 mm focal length；原本的 0 值會讓鏡頭重新設定後產生空畫面。測試材質開關與 OIT 路由不變。
