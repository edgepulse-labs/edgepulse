# Local GitHub Actions Runner

Review date: 2026-05-09

This document explains how to run GitHub Actions jobs on a local machine when code is pushed to GitHub. The local machine acts as a GitHub Actions self-hosted runner.

## Model

The workflow is:

1. A developer pushes commits to GitHub.
2. GitHub evaluates workflow files under `.github/workflows/`.
3. A workflow with `on: push` creates a job.
4. The job targets a self-hosted runner label such as `edgepulse-local`.
5. The local runner process, already connected to GitHub, receives and runs the job.
6. The job runs local build and validation commands such as `make test`, OpenWrt package compile commands, or Rust `cargo test`.

The local runner opens an outbound connection to GitHub. You usually do not need to expose an inbound webhook port from the local network.

## When To Use This

Use a local runner when validation depends on local resources:

- an existing OpenWrt buildroot,
- large local caches such as `dl/` or `ccache/`,
- hardware-specific toolchains,
- a local test device,
- private LAN resources not available to GitHub-hosted runners.

For this project, a local runner is useful for compiling `edgepulse` with `/home/nier/workspace/openwrt-build` and validating package output before publishing changes.

## Security Notes

Use self-hosted runners carefully:

- Prefer private repositories.
- Do not allow untrusted pull requests to run arbitrary code on a machine that has credentials or access to private LAN resources.
- Use a dedicated low-privilege user for the runner.
- Keep secrets out of the runner working directory.
- Keep the runner machine patched.
- Prefer repository-level runners for narrow scope, or organization runner groups with explicit repository allow lists.
- Avoid running the runner as `root`.

GitHub warns that public repository forks can submit workflows that run code on self-hosted runners. Treat self-hosted runners as trusted-compute infrastructure.

## Suggested Local Layout

Example local layout:

```text
/home/nier/actions-runner/       # GitHub runner application
/home/nier/workspace/edgepulse/  # repository checkout used by jobs
/home/nier/workspace/openwrt-build/
/home/nier/workspace/edgepulse-openwrt-feed/
```

The runner creates per-job work directories under its own `_work/` directory. Do not assume a workflow runs inside the same checkout that is open in the IDE.

## Add A Self-Hosted Runner

Use the GitHub web UI to get the current runner download URL and registration token. The token is time-limited.

Repository-level runner:

1. Open the repository on GitHub.
2. Go to `Settings` → `Actions` → `Runners`.
3. Click `New self-hosted runner`.
4. Select Linux and the correct architecture.
5. Run the commands shown by GitHub on the local runner machine.

Example shape:

```sh
mkdir -p ~/actions-runner
cd ~/actions-runner
curl -o actions-runner-linux-x64.tar.gz -L <download-url-from-github>
tar xzf actions-runner-linux-x64.tar.gz
./config.sh --url https://github.com/edgepulse-labs/edgepulse-openwrt-feed --token <token-from-github> --labels edgepulse-local,openwrt,linux
```

Use the exact commands from GitHub because runner versions and tokens change.

## Run As A Service

After `config.sh` succeeds, install the runner as a systemd service from the runner directory:

```sh
cd ~/actions-runner
sudo ./svc.sh install
sudo ./svc.sh start
sudo ./svc.sh status
```

To stop or uninstall:

```sh
cd ~/actions-runner
sudo ./svc.sh stop
sudo ./svc.sh uninstall
```

## Workflow Example

Create `.github/workflows/local-validate.yml`:

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

      - name: Build edgepulse package
        working-directory: /home/nier/workspace/openwrt-build
        run: make package/feeds/edgepulse/edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE="$GITHUB_WORKSPACE"

      - name: Build LuCI package
        working-directory: /home/nier/workspace/openwrt-build
        run: make package/feeds/edgepulse/luci-app-edgepulse/compile V=s EDGEPULSE_LOCAL_SOURCE="$GITHUB_WORKSPACE"
```

This workflow runs on each push to `main` or `develop`, and it can also be started manually with `workflow_dispatch`.

## Runner Labels

Use labels to keep jobs on the correct local node. Recommended labels:

- `edgepulse-local`
- `openwrt`
- `linux`
- target-specific labels such as `mediatek-filogic`

Then require those labels in `runs-on` so unrelated self-hosted runners do not pick up the job.

## Local Environment Requirements

The runner user should have:

- read/write access to `/home/nier/workspace/edgepulse-openwrt-feed`,
- read/write access to the relevant OpenWrt build directories,
- access to `make`, `gcc`, `node`, `cargo`, or other tools required by project tests,
- enough disk space for OpenWrt package builds.

If OpenWrt build paths are shared by multiple users, make sure `dl/`, `ccache/`, and build output permissions are configured before using the runner.

## Push Flow Test

1. Confirm the runner is online in GitHub `Settings` → `Actions` → `Runners`.
2. Confirm the systemd service is running:

   ```sh
   cd ~/actions-runner
   sudo ./svc.sh status
   ```

3. Add or modify `.github/workflows/local-validate.yml`.
4. Push a branch.
5. Open the repository `Actions` tab.
6. Confirm the job shows the self-hosted runner label and starts on the local machine.

## Troubleshooting

- Job is queued forever: the runner is offline, labels do not match, or the runner is assigned to a different repository or runner group.
- Permission denied in OpenWrt buildroot: fix directory ownership or group permissions for the runner user.
- Workflow uses the wrong checkout path: use `$GITHUB_WORKSPACE` for the checked-out repository.
- Local buildroot has stale metadata: rerun `./scripts/feeds update`, `./scripts/feeds install`, or `make defconfig` as appropriate.
- Runner service stopped after reboot: check `sudo ./svc.sh status` and systemd logs.

## References

- GitHub Docs: [Self-hosted runners](https://docs.github.com/en/actions/hosting-your-own-runners)
- GitHub Docs: [Adding self-hosted runners](https://docs.github.com/en/actions/how-tos/manage-runners/self-hosted-runners/add-runners)
- GitHub Docs: [Configuring the self-hosted runner application as a service](https://docs.github.com/en/actions/hosting-your-own-runners/managing-self-hosted-runners/configuring-the-self-hosted-runner-application-as-a-service?platform=linux)
- GitHub Docs: [Using self-hosted runners in a workflow](https://docs.github.com/actions/hosting-your-own-runners/using-self-hosted-runners-in-a-workflow)
