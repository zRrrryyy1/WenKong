# WenKong 温控系统

STM32 PID 温控 + Android App + Spring Boot 云端监控

## 项目结构

```
D:\Android\project\
├── app/           ← Android App（Java, minSdk 21, targetSdk 32）
│   ├── src/main/java/com/wenkong/thermostat/
│   │   ├── MainActivity.java       ← 蓝牙连接 + 数据显示 + 云端上报
│   │   ├── TempChartView.java      ← 温度曲线图（Canvas 绘制）
│   │   └── DataUploader.java       ← HTTP 上报到后端
│   └── build.gradle.kts
├── firmware/       ← STM32F103C8 固件（Keil 项目 hot.uvprojx）
│   ├── main.c                     ← PID 控温主逻辑
│   ├── pid.c / pid.h              ← PID 控制器（积分分离/微分先行/增益调度）
│   ├── pid_autotune.c/.h          ← 继电器自整定（Astrom-Hagglund）
│   ├── ds18b20.c/.h               ← 温度传感器
│   └── oled.c/.h / oled_font.c    ← OLED 显示
├── server/         ← Spring Boot 后端（Java 17+, Gradle）
│   ├── .../controller/             ← REST API
│   ├── .../model/                  ← 数据模型（JPA）
│   ├── .../repository/             ← 数据库访问
│   ├── .../service/                ← 业务逻辑
│   └── src/main/resources/static/  ← Web 前端仪表盘
├── docs/           ← 项目文档
└── .agents/skills/huashu-design/   ← 已安装的 design skill
```

## 通信协议

蓝牙 SPP（9600bps），下位机每秒上报：
```
Temp:25.50    Set:27.00    PWM:350    Err:1.50    Mode:PID
```

手机→下位机：直接发数字设定温度，或 ATUNE/ABORT/APPLY/IGNORE。

## 后端 API

| 方法 | 路径 | 用途 |
|------|------|------|
| POST | /api/temps | 温度历史上报 |
| GET  | /api/temps/recent | 最近 60 条记录 |
| POST | /api/current | 实时状态上报 |
| GET  | /api/current | 实时状态查询 |
| GET  | / | Web 仪表盘 |

## huashu-design 设计 Skill

已安装在 `.agents/skills/huashu-design/`，SKILL.md（631 行）包含完整设计方法论。
Agent SDK 模式无法直接调用 Skill 工具，但 Claude 可以**读取 SKILL.md 并按其中方法工作**。
需要做设计时，告诉 Claude 去读 `.agents/skills/huashu-design/SKILL.md` 并遵循其流程。

## 启动命令

```bash
# 后端
cd D:/Android/project && ./gradlew :server:bootRun

# App 装到手机
cd D:/Android/project && ./gradlew :app:installDebug

# 关闭后端
按 Ctrl+C
```
