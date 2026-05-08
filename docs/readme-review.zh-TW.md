# README 檢視

Review 日期：2026-05-07

## 摘要

目前的 `README.md` 作為專案願景文件很完整。它清楚把 EdgePulse 定位為適用於 OpenWrt、CPE、Raspberry Pi、Linux gateway 與桌面系統的 edge telemetry 與 AI feature 平台。

文件已經說明：

- 為什麼 telemetry 應該成為 AI 訓練資料。
- 主要收集面向：`/proc`、`/sys`、`ubus`、`iw`、`tc`、`conntrack`、`nftables`、thermal zones 與 logs。
- Feature extraction 方向：time-window statistics、variance、correlation 與 tensorization。
- 第一個目標裝置：OpenWrt One。
- 長期 AI 目標：anomaly detection、health scoring、root cause analysis 與 self-healing。

## 待補缺口

README 尚未定義第一個可執行的里程碑。接下來的文件應該把這些項目具體化：

- 最小 OpenWrt package 名稱、layout、daemon 行為與 build dependencies。
- OpenWrt One 硬體假設與可觀測 telemetry 來源。
- raw samples、derived features、metadata 與 retention 的 SQLite schema。
- `/tmp` storage policy，因為 OpenWrt runtime storage 是 volatile，且應盡量減少 flash writes。
- Dashboard、metrics、feature windows 與 settings 的 LuCI views。
- Sampling interval、retention、enabled collectors 與 LuCI display behavior 的 UCI 設定選項。
- 第一版 training-data export format。
- 最小可用 package 的 acceptance criteria。

## 建議的 README 方向

README 應保留為高階產品與研究敘事。避免塞入太多實作細節。實作內容連到 `docs/`：

- OpenWrt One MVP implementation plan。
- Telemetry metric catalog。
- Database schema。
- LuCI design。
- Package development notes。

## 立即文件行動

建立 `docs/` 目錄，並把它作為實作規劃的 source of truth。第一份詳細文件應聚焦在最小 C-based OpenWrt One package：能將本地 telemetry 收集到 `/tmp` 下的 SQLite、定期產生 feature windows，並透過 LuCI 顯示目前資料。
