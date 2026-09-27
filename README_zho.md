<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  🇨🇳 <b>简体中文</b> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### 现场节点固件（Waveshare ESP32-S3-ETH，三个雷达或存在传感器，以太网和 Wi-Fi），自带网页面板，并有在电脑上测试过的核心

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**诚实性检查 - 今天真正能运行的部分:** **成熟度：scaffolding。** 与硬件无关的核心（859 项检查：LD2450、LD2461 和四种存在传感器的解码器、LD2450 命令通道、设置及其校验、引脚表、用户与会话、映射引脚逻辑、网络方案以及消息序列化器，其输出被 ARMOR-COMMON 接受）已在电脑上测试，**网页面板**已在真实浏览器中对着替身节点试用，**固件镜像**能在 ESP-IDF 5.4.2 容器中构建。**它从未在开发板上运行过**：没有从真实模块捕获过任何帧，以太网、Wi-Fi 桥接、光传感器和更新的代码都未经尝试，雷达命令通道既未对照厂商文档也未对照模块检查，面板的 HTTPS 证书由节点自己生成且未在真实浏览器中试过，LD2461 和存在传感器的解码器只与其手册中的示例吻合，背后没有真实模块。

---

## 🎯 概述

* **两种开发板，一套固件：** Waveshare ESP32-S3-ETH（以太网，默认）和不带以太网的 ESP32-S3-WROOM-1 N16R8（仅 Wi-Fi：设置时会询问要加入的 Wi-Fi 网络，节点始终保留自己的网络）。镜像在构建时选择（`tools/build_node.sh generic s3-eth` 或 `generic s3-wifi`）；引脚表和接入方式随开发板而定，面板会隐藏开发板没有的功能，镜像只适用于对应的开发板。两者都能构建并已在电脑上测试，但都没在开发板上运行过。
* **两个 270 度节点：** 每块 Waveshare ESP32-S3-ETH 在其三个 UART 上读取最多三个 LD2450 雷达，彼此相隔 75 度安装，通过有线以太网（W5500）使用 DHCP 或固定地址，由 PoE 或 USB 供电。Studio 的 *Add a 270° node* 会创建已与节点关联的三个雷达。
* **每个节点自带网页面板，** 外观与 Studio 一致，支持七种语言，内嵌于固件：概览、网络、Wi-Fi、代理、雷达、引脚、用户、固件更新和日志。没有用户的节点会开启 Wi-Fi `ARMOR-SETUP-xxxxxx`，并用设置码创建第一个管理员；密码使用加盐 PBKDF2，会话是随机令牌，所有设置都保存在节点的闪存中，因此一个镜像适用于所有节点，且不会把任何密码编译进去（[面板](docs/NODE_PANEL.md)）。
* **多个节点组成一个 Wi-Fi：** 每个节点可提供接到其以太网口的接入点；给节点设置相同的名称和密码，信道设为自动（按 MAC 取 1、6 或 11），手机和 Wi-Fi 传感器就会看到一个网络和一个 DHCP 服务器。这不是无线 mesh：每个节点仍保留自己的网线。节点也可以改为加入路由器的 Wi-Fi（可搜索网络），没有网线的节点可通过 Android 应用用**蓝牙**配置。
* **为服务器提供引脚：** 任何空闲引脚都可成为输入、输出（代理丢失时有安全状态）、PWM 或模拟读数，并显示为服务器的一个设备，因此继电器或触点无需新固件。开发板的保留引脚永远不会被提供。
* **通过面板进行带回滚的空中更新（OTA）**（16 MB 闪存上有两个槽位），以及从 Studio 到每个节点面板的**链接**，地址来自节点发布的信息。
* **通过 HTTPS 访问面板：** 节点生成自己的证书（自签名，保存在闪存中），除 80 端口外也在 443 端口提供面板，或只使用 443；会话 Cookie 标为 Secure，并显示证书指纹以便与浏览器的警告比对。**稳定的轨迹身份：** 每帧的目标会与之前跟踪的目标匹配，因此一个人保持同一轨迹编号，丢帧不会闪烁，位置也会略微平滑。
* **六种传感器型号，每个端口一种：** 端口可接 LD2450 或 LD2461（为周界提供数据的跟踪器，Studio 中有该型号自己的探测范围），或存在传感器（LD2410、LD2412、LD2410S、MR24HPC1），后者成为服务器的设备并发布存在与距离。在面板中选择；每种传感器是什么、其协议以及哪些未经验证，见 `docs/SENSORS.md`。
* **LD2450 解码器、健康状态与配置：** 可重新同步的分帧器能在有噪声的数据流中找到帧；每个 30 字节的帧给出最多三个目标，成为契约轨迹；控制台和面板会按雷达说明它是在上报、静默还是数据混乱。面板还能读取模块版本、选择一个或三个目标并设置检测区域（这一协议尚未对照模块检查）。
* **环境光与严格符合契约的消息：** 带自动量程的 VEML7700；遵循已发布模式的遥测、健康和信息 JSON，拒绝写入任何无效内容，带墙上时钟时间戳（SNTP）和 MQTT 遗嘱。没有雷达上报时会扣留遥测，绝不发出空的“一切正常”。
* **一个镜像适用于所有开发板，像网络产品一样：** 固件相同，由 MAC 区分开发板（命名前为 `armor-` 加六位数字）；`adopt_node.py` 通过网络给刚刷好的节点分配管理员、代理身份和整个节点群共用的设置，设置码由其 MAC 计算得出，因此 5 个节点和 27 个节点工作量相同。**台架工具：** 通过 USB-C 刷写、用于开发面板的替身节点，以及把原始帧日志转换成测试夹具的转换器（[台架调试](docs/BENCH_BRINGUP.md)）。

## 📂 仓库结构

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        start-up: settings, radars, pins, network, panel, broker, Bluetooth
│   ├── node_store.cpp      settings and users in flash
│   ├── network.cpp         Ethernet, Wi-Fi access point and bridge, station
│   ├── web_server.cpp      the panel and its JSON API, login, update
│   ├── api_shared.cpp      the operations the panel and Bluetooth share (status, settings, Wi-Fi scan)
│   ├── ble_provision.cpp   configuration from a phone over Bluetooth (NimBLE)
│   ├── radar_manager.cpp   UARTs, frames, health, command channel (armor_radar.cpp, radar_tracks.hpp)
│   ├── gpio_manager.cpp    the pins mapped for the server
│   ├── mqtt_link.cpp       clock, health, telemetry, information, pin topics
│   ├── board_ethernet.cpp, light_sensor.cpp, tls_cert.cpp, log_buffer.cpp, entropy.cpp
│   ├── Kconfig.projbuild   the first settings of a build
│   └── core/               framer, ld2450, ld2450_command, ld2461, presence, sensor_model, node_config, board_pins, network_plan, auth, ble_frame, ble_dispatch, gpio_logic, json, veml7700, telemetry_json...
├── panel/                  index.html, app.js, text.js (7 languages), style.css
├── tests/                  test_core.cpp, test_node.cpp, test_sensors.cpp, test_board_wifi.cpp, emit_samples.cpp, check_contract.py, test_tools.py
├── tools/                  build_node.sh, make_fleet.py, adopt_node.py, provision_node.sh, flash.bat, pack_panel.py, panel_mock.mjs, frames_to_fixture.py
├── secrets/                node.conf.example, fleet.example.json (the real files are git-ignored)
├── partitions.csv, sdkconfig.defaults, sdkconfig.board.*
└── docs/                   BENCH_BRINGUP.md, NODE_PANEL.md, BLE_PROVISIONING.md, SENSORS.md, HARDWARE_BOUNDARY.md
```

## 🛠️ 开发环境

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core && build/host/test_node && build/host/test_sensors && build/host/test_board_wifi   # 859 checks, -Werror
build/host/emit_samples | python tests/check_contract.py
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
python tools/make_fleet.py                             # once: the fleet secret and the shared settings
tools/build_node.sh generic                           # ONE image for every Waveshare board (dist/generic-s3-eth.bin), in the ESP-IDF container
tools/build_node.sh generic s3-wifi                   # the same firmware for an ESP32-S3-WROOM-1 N16R8 with no Ethernet
```

```bat
tools\flash.bat generic COM5 monitor                  # each Waveshare board, once, by USB-C (add s3-wifi for the other board)
```

```bash
tools/adopt_node.py 192.168.0.181 --id perimetro-3 --fleet secrets/fleet.json --broker-ssh-host <cm5> --broker-ssh-user <user>
```

主机测试需要任意 C++17 编译器（Linux、WSL、MSYS2）。参见[台架调试](docs/BENCH_BRINGUP.md)、[面板](docs/NODE_PANEL.md)和[硬件边界](docs/HARDWARE_BOUNDARY.md)。

## 🔗 相关项目

**A.R.M.O.R.**（Autonomous Radar & Multimodal Observation Range）是由若干独立仓库组成的周界安防系统。每个仓库都有自己的版本、测试和 README；家族成员如下：

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - 消息契约、验证器、一致性向量和生成的类型
* **ARMOR-RADAR** (本仓库) - 适用于 ESP32-S3 的现场节点固件，带三个雷达和自带网页面板
* **[ARMOR-SOLAR](https://github.com/JuanenRac/ARMOR-SOLAR)** - 太阳能逆变器与电池的协议，以及网关节点的消息
* **[ARMOR-ELECTRICAL](https://github.com/JuanenRac/ARMOR-ELECTRICAL)** - 电气节点：电表、电网读数消息和开关规则
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - 本地网络：其设备、互联网以及变化
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - 中央协调器：遥测、报警、设备、太阳能读数和摄像头
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - 网页控制台：摄像头、雷达、报警、太阳能和 2D/3D 场地设计器
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - 带实时 2D/3D 雷达的 Android 操作员客户端
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - 会解释决策且从不执行动作的视觉推理策略
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - 带无法伪造确认的离线语音意图
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - 外壳、电子器件和台架验收矩阵
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - 部署、CM5 测试台、备份与 TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - 带可重复故障的离线遥测模拟器
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - 发现、安装并更新生态系统自身的仓库
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - 架构、安全基线和能力矩阵

## 📚 文档与社区

更多阅读：

* [能力矩阵：哪些已被证实，哪些没有](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [项目目录：版本以及各仓库之间的依赖](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [本仓库的变更记录](CHANGELOG.md)
* [许可证（GPL-3.0-or-later）](LICENSE)
* 问题、想法与反馈：electrohobby3d@gmail.com

## 👤 作者

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 许可证

GPL-3.0-or-later - 见 [LICENSE](LICENSE)。
