# WenKong 温控 — 云端远程监控方案（方案 C）

> 项目书版本：1.0  
> 日期：2026-06-08  

---

## 一、方案概述

### 1.1 架构图

```
┌──────────────┐   蓝牙 SPP    ┌──────────────────┐   HTTP/REST    ┌───────────────────┐
│  STM32F103C8 ├──────────────►│  Android App      ├───────────────►│  Spring Boot 后端  │
│  PID 温控     │◄─────────────┤  (WenKong 温控)   │◄──────────────┤  (本地运行)        │
│  DS18B20     │   命令下发    │                   │    JSON       │  localhost:8080    │
└──────────────┘              └──────────────────┘                └────────┬──────────┘
                                                                           │
                                                                           │ cpolar 内网穿透
                                                                           ▼
┌──────────────┐               ┌──────────────────┐                ┌───────────────────┐
│  任意浏览器   │◄──────────────┤  公网 URL        │◄───────────────┤  cpolar 隧道       │
│  Web 仪表盘  │               │  xxx.cpolar.cn   │                │  (免费版)          │
└──────────────┘               └──────────────────┘                └───────────────────┘
```

### 1.2 数据流

```
温度上报：  App → POST /api/temps → 后端 → H2 数据库
历史查询：  Web → GET  /api/temps  → 后端 → H2 数据库 → JSON → ECharts 图表
实时监控：  App → POST /api/current → 后端内存 → Web 轮询 GET /api/current
```

### 1.3 核心特性

| 特性 | 说明 |
|------|------|
| 后端框架 | Spring Boot 3.x + Java 17 |
| 数据库 | H2 嵌入式数据库（零配置，单文件存储）|
| 内网穿透 | cpolar（免费版，1 个隧道，域名随机）|
| Web 前端 | 纯 HTML + ECharts（无前端框架依赖）|
| App 对接 | OkHttp + Gson，主线程外异步上报 |
| 部署方式 | 一个 `java -jar` 启动，无需安装数据库 |

---

## 二、后端模块详细设计

### 2.1 技术栈

| 组件 | 版本/选择 | 理由 |
|------|----------|------|
| JDK | 17（已安装） | 与 Android 编译兼容 |
| Spring Boot | 3.2.x | 最新稳定版，支持 GraalVM |
| Spring Web | — | REST API |
| Spring Data JPA | — | ORM 操作数据库 |
| H2 Database | 2.x | 嵌入式，单 jar 依赖 |
| Gradle | 8.7 (已有) | 统一项目构建 |
| Lombok | — | 减少样板代码 |

### 2.2 项目结构

```
D:\Android\project\server\
  ├── src/main/java/com/wenkong/server/
  │   ├── WenKongServerApplication.java    ← 入口
  │   ├── controller/
  │   │   ├── TempController.java          ← 温度数据 API
  │   │   ├── StatusController.java        ← 实时状态 API
  │   │   └── WebPageController.java       ← Web 页面路由
  │   ├── model/
  │   │   ├── TempRecord.java              ← 温度记录实体
  │   │   └── DeviceStatus.java            ← 实时设备状态
  │   ├── repository/
  │   │   └── TempRecordRepository.java    ← 数据库访问
  │   └── service/
  │       └── TempService.java             ← 业务逻辑
  ├── src/main/resources/
  │   ├── application.yml                  ← 配置
  │   ├── static/
  │   │   ├── index.html                   ← 主仪表盘
  │   │   ├── history.html                 ← 历史查询页面
  │   │   └── echarts.min.js               ← ECharts CDN 备份
  │   └── templates/                       ← （暂不需要）
  ├── build.gradle.kts                     ← Gradle 构建
  └── settings.gradle.kts                  ← 项目设置
```

### 2.3 REST API 接口

#### 2.3.1 温度数据上报（App → 服务器）

```
POST /api/temps
Content-Type: application/json

{
  "deviceId": "wenkong-001",
  "temperature": 25.5,
  "setpoint": 27.0,
  "pwm": 350,
  "error": 1.5,
  "mode": "PID",
  "timestamp": "2026-06-08T14:30:00"
}

响应：201 Created
{
  "id": 1,
  "receivedAt": "2026-06-08T14:30:01"
}
```

#### 2.3.2 实时状态上报（App → 服务器，覆盖最新值）

```
POST /api/current
Content-Type: application/json

{
  "deviceId": "wenkong-001",
  "temperature": 25.5,
  "setpoint": 27.0,
  "pwm": 350,
  "mode": "PID",
  "kp": 6.0,
  "ki": 0.6,
  "kd": 0.7,
  "timestamp": "2026-06-08T14:30:00"
}

响应：200 OK
```

#### 2.3.3 历史数据查询（Web → 服务器）

```
GET /api/temps?deviceId=wenkong-001&start=2026-06-08T00:00:00&end=2026-06-08T23:59:59

响应：200 OK
{
  "deviceId": "wenkong-001",
  "count": 8640,
  "records": [
    { "temperature": 25.5, "setpoint": 27.0, "pwm": 350, "error": 1.5, "mode": "PID", "timestamp": "..." },
    ...
  ]
}
```

#### 2.3.4 实时状态查询（Web → 服务器）

```
GET /api/current?deviceId=wenkong-001

响应：200 OK
{
  "temperature": 25.5,
  "setpoint": 27.0,
  "pwm": 350,
  "mode": "PID",
  "kp": 6.0,
  "ki": 0.6,
  "kd": 0.7,
  "lastUpdate": "2026-06-08T14:30:00"
}
```

#### 2.3.5 自整定结果上报（App → 服务器）

```
POST /api/tune-result
Content-Type: application/json

{
  "deviceId": "wenkong-001",
  "kp": 6.0,
  "ki": 0.6,
  "kd": 0.7,
  "confidence": 5,
  "applied": true,
  "timestamp": "2026-06-08T14:30:00"
}
```

### 2.4 数据模型

#### 温度记录表 `TEMP_RECORDS`

| 字段 | 类型 | 说明 |
|------|------|------|
| ID | BIGINT (自增) | 主键 |
| DEVICE_ID | VARCHAR(32) | 设备标识 |
| TEMPERATURE | DOUBLE | 当前温度 |
| SETPOINT | DOUBLE | 目标温度 |
| PWM | INT | PWM 输出值 |
| ERROR_VAL | DOUBLE | 误差 |
| MODE | VARCHAR(16) | 运行模式 |
| TIMESTAMP | DATETIME | 数据时间戳 |
| RECEIVED_AT | DATETIME | 服务器接收时间 |

#### 实时状态表（仅内存/单记录）

| 字段 | 说明 |
|------|------|
| deviceId | 设备标识 |
| temperature, setpoint, pwm, mode | 与上报字段一致 |
| kp, ki, kd | PID 参数 |
| lastUpdate | 最后更新时间 |

### 2.5 配置文件 `application.yml`

```yaml
server:
  port: 8080

spring:
  datasource:
    url: jdbc:h2:file:./data/wenkong
    driver-class-name: org.h2.Driver
    username: sa
    password:
  h2:
    console:
      enabled: true        # 开发时可通过 /h2-console 查看数据库
      path: /h2-console
  jpa:
    hibernate:
      ddl-auto: update     # 自动建表
    show-sql: false
```

---

## 三、Android App 对接改动

### 3.1 改动清单

| 文件 | 改动内容 | 预估行数 |
|------|---------|---------|
| `app/build.gradle.kts` | 添加 `okhttp` 和 `gson` 依赖 | +3 |
| `MainActivity.java` | 解析数据后调用上报线程 | +50 |
| `DataUploader.java` **新增** | OkHttp 异步上报封装类 | +100 |

### 3.2 新增依赖

```kotlin
// app/build.gradle.kts
dependencies {
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    implementation("com.google.code.gson:gson:2.10.1")
}
```

### 3.3 新增 `DataUploader.java`

```java
package com.wenkong.thermostat;

import okhttp3.*;
import com.google.gson.Gson;
import java.util.concurrent.TimeUnit;

public class DataUploader {
    private static final String BASE_URL = "http://localhost:8080"; // ← 上线时改为 cpolar 地址
    private final OkHttpClient client = new OkHttpClient.Builder()
            .connectTimeout(5, TimeUnit.SECONDS)
            .writeTimeout(5, TimeUnit.SECONDS)
            .build();
    private final Gson gson = new Gson();

    public void uploadTemp(float temp, float set, int pwm, float err, String mode) {
        Map<String, Object> data = new HashMap<>();
        data.put("deviceId", "wenkong-001");
        data.put("temperature", temp);
        data.put("setpoint", set);
        data.put("pwm", pwm);
        data.put("error", err);
        data.put("mode", mode);
        data.put("timestamp", new Date().toString());

        Request request = new Request.Builder()
                .url(BASE_URL + "/api/temps")
                .post(RequestBody.create(gson.toJson(data), MediaType.get("application/json")))
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override public void onFailure(Call call, IOException e) { /* 静默失败，不影响主功能 */ }
            @Override public void onResponse(Call call, Response res) { res.close(); }
        });
    }
}
```

### 3.4 调用时机

在 `MainActivity.parse()` 方法中，每次解析完数据后调用：

```java
private DataUploader uploader = new DataUploader();

// 在 parse() 末尾：
uploader.uploadTemp(currentTemp, setpoint, pwmValue, error, mode);
```

App 上报频率与下位机数据频率一致：**每 1 秒 1 次**。

### 3.5 上线切换

1. 启动后端 + cpolar → 拿到公网 URL（如 `https://abc123.cpolar.cn`）
2. 修改 `DataUploader.BASE_URL` 为 cpolar 地址
3. Android App 在 WiFi/4G 下自动上报

---

## 四、Web 前端仪表盘

### 4.1 页面一览

| 页面 | 路由 | 功能 |
|------|------|------|
| 实时仪表盘 | `/` 或 `/index.html` | 大字温度、实时曲线、设备状态 |
| 历史查询 | `/history.html` | 时间选择器、历史曲线、数据统计 |

### 4.2 实时仪表盘布局

```
┌─────────────────────────────────────────────────┐
│  WenKong 温控 · 云端监控         实时: 25.3°C    │  ← 导航栏
├──────────────────────┬──────────────────────────┤
│  当前状态             │                          │
│  温度: 25.3°C        │     实时温度曲线          │
│  目标: 27.0°C        │   ┌──────────────────┐   │
│  PWM:  350           │   │  ECharts 折线图    │   │
│  误差: -1.7°C        │   │  滚动 60s 窗口    │   │
│  模式: PID           │   └──────────────────┘   │
│  更新: 3秒前          │                          │
│  ┌─────────────────┐ │                          │
│  │ PID 参数:        │ │  实时: 25.2°C 目标: 27  │
│  │ Kp: 6.0  Ki:0.6  │ │  PWM: 348  误差: -1.8  │
│  │ Kd: 0.7          │ │  模式: PID              │
│  └─────────────────┘ │                          │
├──────────────────────┴──────────────────────────┤
│  数据统计:  最高 25.8°C  最低 24.9°C  平均 25.3°C │
│  运行时间: 02:15:33  数据条数: 8,123             │
└─────────────────────────────────────────────────┘
```

### 4.3 技术选型

| 组件 | 选择 | 理由 |
|------|------|------|
| 图表库 | ECharts 5.x | CDN 引入，零安装，曲线图表现力强 |
| 数据刷新 | 定时轮询 `GET /api/current` | 每 3 秒一次，实现简单 |
| 布局 | CSS Flexbox | 纯 HTML，不需要任何框架 |
| 配色 | 延续 App 的蓝橙配色 (#1976D2/#F4511E) | 保持项目统一风格 |

### 4.4 关键代码结构（实时仪表盘）

```html
<!DOCTYPE html>
<html>
<head>
    <title>WenKong 温控 · 云端监控</title>
    <script src="https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js"></script>
</head>
<body>
    <!-- 状态卡片 -->
    <div id="statusCard">...</div>
    
    <!-- 实时曲线 -->
    <div id="tempChart" style="height: 300px"></div>
    
    <script>
        const API_BASE = ''; // 同源请求
        
        function updateStatus() {
            fetch('/api/current?deviceId=wenkong-001')
                .then(r => r.json())
                .then(data => { /* 更新UI */ });
        }
        
        function updateChart() {
            fetch('/api/temps?deviceId=wenkong-001&limit=60')
                .then(r => r.json())
                .then(data => {
                    // ECharts setOption 更新曲线
                });
        }
        
        setInterval(updateStatus, 3000);     // 3秒刷状态
        setInterval(updateChart, 3000);      // 3秒刷曲线
    </script>
</body>
</html>
```

---

## 五、内网穿透配置（cpolar）

### 5.1 什么是 cpolar

cpolar 是一个国内内网穿透工具，能将你本机的 HTTP 服务暴露到公网。免费版提供：

- 1 个随机域名（`xxx.cpolar.cn`）
- 每月 1024 MB 流量（纯文本数据绰绰有余）
- 单向隧道（Web → 你的本地服务）

### 5.2 安装与使用

```bash
# 1. 下载 cpolar
#    官网：https://www.cpolar.com/download

# 2. 解压后，启动隧道（指向本地 8080 端口）
cpolar http 8080

# 3. 输出示例
Forwarding  https://abc123.cpolar.cn -> http://localhost:8080

# 4. 把这个 URL 填到 App 的 DataUploader.BASE_URL 中
```

### 5.3 注意事项

| 注意事项 | 说明 |
|---------|------|
| 域名每次重启会变 | 免费版不能固定域名，重启后需要在 App 里改 URL |
| 电脑不能关机 | 后端跑在本地，关了就没了 |
| 演示时可以固定 | 演示前一小时启动，中途不重启即可 |
| 流量够用 | 每次上报 ~200 字节，每秒 1 次，一天 ~17MB，免费额度够用 |

**演示时的操作流程**：

```
1. 启动后端：  cd server && ./gradlew bootRun
2. 启动 cpolar：cpolar http 8080
3. 复制公网 URL： https://xxx.cpolar.cn
4. 修改 App 里的 BASE_URL
5. 安装 APK 到手机，演示开始！
```

---

## 六、开发计划

### 6.1 任务分解

| 阶段 | 任务 | 预估时间 | 产出 |
|------|------|---------|------|
| **P1** | Spring Boot 项目搭建 + 数据模型 + API | 4h | 可运行的后端，Swagger 可测 |
| **P2** | App 对接（DataUploader + 集成） | 2h | 手机数据上报后端 |
| **P3** | Web 前端仪表盘（ECharts 曲线） | 4h | 网页实时监控 |
| **P4** | cpolar 配置 + 端到端联调 | 2h | 公网可访问 |
| **P5** | 历史查询页面 + 数据统计 | 3h | 完整 Web 功能 |
| **P6** | 文档 + 测试 + 答辩准备 | 2h | 项目报告 |
| **合计** | | **~17h** | |

### 6.2 依赖关系

```
P1 后端 API ──→ P2 App 对接
         └──→ P3 Web 前端（先 mock 数据，后端就绪后对接）
                     └──→ P5 历史页面
                             └──→ P6 文档
P4 cpolar 贯穿全程，最后联调
```

### 6.3 项目根目录变更

```
D:\Android\project\
  ├── app/               ← Android App
  ├── firmware/           ← STM32 固件
  ├── server/             ← 📦 Spring Boot 后端（新增）
  │   ├── src/
  │   │   ├── main/java/com/wenkong/server/
  │   │   └── main/resources/
  │   │       ├── application.yml
  │   │       ├── static/index.html
  │   │       └── static/history.html
  │   └── build.gradle.kts
  ├── docs/
  └── settings.gradle.kts
```

**注意**：`server/` 和 `app/` 是两个独立的 Gradle 子模块，但共用同一个 Gradle 根配置。`settings.gradle.kts` 需添加：

```kotlin
// settings.gradle.kts
rootProject.name = "WenKong"
include(":app")
include(":server")  // ← 新增
```

---

## 七、课设答辩加分点

### 展示流程建议

```
1. 打开后端 (idea 启动 server)
2. 打开 Web 仪表盘（浏览器打开 localhost:8080）
3. 连接 STM32/模拟器，观察 App 上报数据
4. 在 Web 页面看到实时曲线开始跳动
5. 演示历史查询、统计数据
6. 展示手机在 4G 网络下（断开 WiFi）数据仍能上传
   → 因为穿透的是公网 URL，不需要局域网
```

### 答辩话术准备

| 可能的问题 | 参考答案 |
|-----------|---------|
| "你这个架构是怎么设计的？" | "端-边-云三层架构：STM32 是端、手机 App 是边缘网关转发、Spring Boot 是云后端" |
| "为什么用 H2 不用 MySQL？" | "为了零部署，演示时不需要安装数据库，一个 jar 启动即可；正式部署可一键切换到 MySQL（改一行配置）" |
| "安全性怎么考虑？" | "当前在课程设计阶段使用了 cpolar 随机域名做简单防护；生产环境可加 HTTPS + JWT 认证" |
| "数据实时性如何？" | "每 1 秒采集一次温度，每 3 秒 Web 前端轮询刷新，端到端延迟不超过 4 秒" |

---

## 八、需要的外部资源

| 资源 | 下载地址 | 用途 |
|------|---------|------|
| cpolar | https://www.cpolar.com/download | 内网穿透 |
| ECharts | CDN: `https://cdn.jsdelivr.net/npm/echarts@5/dist/echarts.min.js` | Web 图表 |
| Spring Boot | Gradle 自动下载 | 后端框架 |
| H2 数据库 | Gradle 自动下载 | 嵌入式数据库 |

**零成本清单**：✅ 不买服务器 ✅ 不买域名 ✅ 不买云服务
