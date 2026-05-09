# EdgePulse Docs

This directory keeps implementation-oriented project documents.

## Documents

- [README review](readme-review.md) / [繁體中文](readme-review.zh-TW.md): current README assessment and documentation gaps.
- [Execution plan](execution-plan.md) / [繁體中文](execution-plan.zh-TW.md): staged plan for the first OpenWrt One implementation.
- [OpenWrt feeds and repos](openwrt-feeds-and-repos.md) / [繁體中文](openwrt-feeds-and-repos.zh-TW.md): repository split, custom feed, and OpenWrt integration workflow.
- [Local OpenWrt package validation](local-openwrt-validation.md) / [繁體中文](local-openwrt-validation.zh-TW.md): local buildroot flow for validating the EdgePulse package.
- [Unit test plan](unit-test-plan.md) / [繁體中文](unit-test-plan.zh-TW.md): unit-test boundary, test program, and near-term coverage plan.
- [OpenWrt One telemetry MVP](openwrt-one-telemetry-mvp.md) / [繁體中文](openwrt-one-telemetry-mvp.zh-TW.md): minimal C package, SQLite, feature sampling, and LuCI plan.
- [Training data upload and normalization](training-data-upload-and-normalization.md) / [繁體中文](training-data-upload-and-normalization.zh-TW.md): planned remote upload mechanism and fixed-schema normalization strategy.
- [OpenWrt AI Agent project requirements plan](openwrt_ai_agent_requirements_plan.md) / [繁體中文](openwrt_ai_agent_requirements_plan.zh-TW.md): runtime requirements, architecture, and milestones for an OpenWrt AI agent.

## Documentation Rules

- Keep architecture intent in the root `README.md`.
- Keep implementation details, schemas, metric catalogs, package layout, and LuCI plans in `docs/`.
- Prefer small documents that can become development tickets.
- Record hardware-specific assumptions with source links and review dates.
