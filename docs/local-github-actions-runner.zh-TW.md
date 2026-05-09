# 本地 GitHub Actions Runner

Review 日期：2026-05-09

這份文件說明如何在 GitHub 收到 `git push` 後，讓 GitHub Actions job 派送到本地機器執行。本地機器扮演 GitHub Actions self-hosted runner。

## 運作模型

流程如下：

1. Developer 將 commits push 到 GitHub。
2. GitHub 讀取 `.github/workflows/` 下的 workflow files。
3. 有 `on: push` 的 workflow 建立 job。
4. Job 指定 self-hosted runner label，例如 `edgepulse-local`。
5. 已經連上 GitHub 的本地 runner process 接收並執行 job。
6. Job 在本地執行 build 與 validation commands，例如 `make test`、OpenWrt package compile commands，或 Rust `cargo test`。

本地 runner 是主動對 GitHub 建立 outbound connection。通常不需要從外網開 inbound webhook port 到本地網路。

## 何時使用

當驗證流程依賴本地資源時，適合使用 local runner：

- 既有 OpenWrt buildroot，
- 大型本地 cache，例如 `dl/` 或 `ccache/`，
- 硬體或 target-specific toolchains，
- 本地測試設備，
- GitHub-hosted runners 無法存取的 private LAN resources。

對這個專案而言，local runner 適合用來使用 `/home/nier/workspace/openwrt-build` 編譯 `edgepulse`，並在發布前驗證 package output。

## 安全注意事項

Self-hosted runners 要小心使用：

- 優先用於 private repositories。
- 不要讓不可信任的 pull requests 在擁有 credentials 或 private LAN access 的機器上執行任意程式碼。
- 使用專用、低權限的 runner user。
- 不要把 secrets 放在 runner working directory。
- 保持 runner machine 的 OS 與工具更新。
- 優先使用 repository-level runner 限縮範圍，或使用 organization runner groups 搭配明確 repository allow list。
- 避免用 `root` 執行 runner。

GitHub 官方提醒，public repository forks 可能送出會在 self-hosted runner 上執行的 workflow。請把 self-hosted runner 視為 trusted-compute infrastructure。

## 建議本地目錄配置

範例配置：

```text
/home/nier/actions-runner/       # GitHub runner application
/home/nier/workspace/edgepulse/  # repository checkout used by jobs
/home/nier/workspace/openwrt-build/
/home/nier/workspace/edgepulse-openwrt-feed/
```

Runner 會在自己的 `_work/` 目錄下建立每個 job 的工作目錄。不要假設 workflow 會直接在 IDE 目前開啟的 checkout 內執行。

## 新增 Self-Hosted Runner

使用 GitHub web UI 取得目前 runner download URL 與 registration token。Token 有時間限制。

Repository-level runner：

1. 打開 GitHub repository。
2. 進入 `Settings` → `Actions` → `Runners`。
3. 點選 `New self-hosted runner`。
4. 選擇 Linux 與正確 CPU architecture。
5. 在本地 runner machine 上執行 GitHub 顯示的 commands。

指令形狀大致如下：

```sh
mkdir -p ~/actions-runner
cd ~/actions-runner
curl -o actions-runner-linux-x64.tar.gz -L <download-url-from-github>
tar xzf actions-runner-linux-x64.tar.gz
./config.sh --url https://github.com/<owner>/<repo> --token <token-from-github> --labels edgepulse-local,openwrt,linux
```

請使用 GitHub 頁面顯示的實際 commands，因為 runner 版本與 token 會變動。

## 以 Service 執行

`config.sh` 成功後，在 runner directory 內安裝 systemd service：

```sh
cd ~/actions-runner
sudo ./svc.sh install
sudo ./svc.sh start
sudo ./svc.sh status
```

停止或解除安裝：

```sh
cd ~/actions-runner
sudo ./svc.sh stop
sudo ./svc.sh uninstall
```

## Workflow 範例

建立 `.github/workflows/local-validate.yml`：

```yaml
name: Local validation

on:
  push:
    branches:
      - main
      - develop
  workflow_dispatch:

jobs:
  validate:
    runs-on:
      - self-hosted
      - edgepulse-local
      - openwrt
      - linux

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Unit tests
        run: make test

      - name: Sync local OpenWrt feed
        run: cp -a "$GITHUB_WORKSPACE/packaging/openwrt-feed/." /home/nier/workspace/edgepulse-openwrt-feed/

      - name: Build edgepulse package
        working-directory: /home/nier/workspace/openwrt-build
        run: make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE="$GITHUB_WORKSPACE"

      - name: Build LuCI package
        working-directory: /home/nier/workspace/openwrt-build
        run: make package/feeds/edgepulse/luci-app-edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE="$GITHUB_WORKSPACE"
```

這個 workflow 會在 push 到 `main` 或 `develop` 時執行，也可以透過 `workflow_dispatch` 手動啟動。

## Runner Labels

使用 labels 讓 jobs 被正確的本地 node 接走。建議 labels：

- `edgepulse-local`
- `openwrt`
- `linux`
- target-specific labels，例如 `mediatek-filogic`

接著在 `runs-on` 指定這些 labels，避免不相關的 self-hosted runner 接走 job。

## 本地環境需求

Runner user 應具備：

- 對 `/home/nier/workspace/edgepulse-openwrt-feed` 的讀寫權限，
- 對相關 OpenWrt build directories 的讀寫權限，
- 可使用 `make`、`gcc`、`node`、`cargo` 或專案測試需要的其他工具，
- 足夠磁碟空間可執行 OpenWrt package builds。

如果 OpenWrt build paths 由多位使用者共用，使用 runner 前要先確認 `dl/`、`ccache/` 與 build output 權限已設定好。

## Push Flow 測試

1. 在 GitHub `Settings` → `Actions` → `Runners` 確認 runner online。
2. 確認 systemd service 正在執行：

   ```sh
   cd ~/actions-runner
   sudo ./svc.sh status
   ```

3. 新增或修改 `.github/workflows/local-validate.yml`。
4. Push branch。
5. 打開 repository 的 `Actions` tab。
6. 確認 job 顯示 self-hosted runner label，並在本地機器上開始執行。

## Troubleshooting

- Job 一直 queued：runner offline、labels 不吻合，或 runner 被分配到不同 repository/runner group。
- OpenWrt buildroot permission denied：修正 runner user 對目錄的 ownership 或 group permissions。
- Workflow 用錯 checkout path：使用 `$GITHUB_WORKSPACE` 指向本次 job 的 checkout。
- 本地 buildroot metadata stale：視情況重新執行 `./scripts/feeds update`、`./scripts/feeds install` 或 `make defconfig`。
- Reboot 後 runner service 沒起來：檢查 `sudo ./svc.sh status` 與 systemd logs。

## 參考資料

- GitHub Docs: [Self-hosted runners](https://docs.github.com/en/actions/hosting-your-own-runners)
- GitHub Docs: [Adding self-hosted runners](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/add-runners)
- GitHub Docs: [Configuring the self-hosted runner application as a service](https://docs.github.com/en/actions/hosting-your-own-runners/managing-self-hosted-runners/configuring-the-self-hosted-runner-application-as-a-service?platform=linux)
- GitHub Docs: [Using self-hosted runners in a workflow](https://docs.github.com/actions/hosting-your-own-runners/using-self-hosted-runners-in-a-workflow)
