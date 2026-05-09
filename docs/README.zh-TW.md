# EdgePulse 文件

這個目錄保存偏向實作的專案文件。

## 文件

- [README 檢視](readme-review.zh-TW.md)：目前 README 的評估與文件缺口。
- [執行計畫](execution-plan.zh-TW.md)：第一版 OpenWrt One 實作的分階段計畫。
- [OpenWrt feeds 與 repos](openwrt-feeds-and-repos.zh-TW.md)：repository 拆分、自訂 feed 與 OpenWrt 整合流程。
- [本地 OpenWrt Package 驗證](local-openwrt-validation.zh-TW.md)：使用本地 buildroot 驗證 EdgePulse package 的流程。
- [Unit test plan](unit-test-plan.zh-TW.md)：unit-test 邊界、test program 與近期 coverage plan。
- [OpenWrt One telemetry MVP](openwrt-one-telemetry-mvp.zh-TW.md)：最小 C package、SQLite、feature 採樣與 LuCI 規劃。
- [訓練資料上傳與標準化](training-data-upload-and-normalization.zh-TW.md)：遠端上傳機制與固定 schema normalization strategy 規劃。
- [OpenWrt AI Agent 專案需求計畫](openwrt_ai_agent_requirements_plan.zh-TW.md)：OpenWrt AI Agent runtime 的需求、架構與里程碑。

## 文件規則

- 專案架構意圖保留在根目錄 `README.md`。
- 實作細節、schema、metric catalog、package layout 與 LuCI plan 放在 `docs/`。
- 優先維持小型文件，讓文件可以轉成開發 ticket。
- 記錄硬體相關假設時，要附上來源連結與 review 日期。
