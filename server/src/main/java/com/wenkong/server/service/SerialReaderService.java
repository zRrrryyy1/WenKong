package com.wenkong.server.service;

import com.fazecast.jSerialComm.SerialPort;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.util.HashMap;
import java.util.Map;

/**
 * 串口读取服务
 * 直接读取 HC-05 蓝牙模块（COM8）发来的温度数据，无需手机中转。
 */
@Service
public class SerialReaderService {

    private static final Logger log = LoggerFactory.getLogger(SerialReaderService.class);
    private static final String COM_PORT = "COM8";
    private static final int BAUD_RATE = 9600;

    private final TempService tempService;
    private SerialPort serialPort;
    private Thread readerThread;
    private volatile boolean running = false;

    public SerialReaderService(TempService tempService) {
        this.tempService = tempService;
    }

    @PostConstruct
    public void init() {
        log.info("尝试连接串口 {} (波特率 {})...", COM_PORT, BAUD_RATE);
        serialPort = SerialPort.getCommPort(COM_PORT);
        serialPort.setBaudRate(BAUD_RATE);
        serialPort.setNumDataBits(8);
        serialPort.setNumStopBits(1);
        serialPort.setParity(SerialPort.NO_PARITY);

        if (!serialPort.openPort()) {
            log.warn("无法打开串口 {}，请检查 HC-05 是否已配对且未被占用", COM_PORT);
            log.warn("后端正常运行，只是没有串口数据来源");
            return;
        }

        serialPort.setComPortTimeouts(SerialPort.TIMEOUT_READ_SEMI_BLOCKING, 5000, 0);
        log.info("串口 {} 已连接！正在监听数据...", COM_PORT);

        running = true;
        readerThread = new Thread(this::readLoop, "serial-reader");
        readerThread.setDaemon(true);
        readerThread.start();
    }

    @PreDestroy
    public void shutdown() {
        running = false;
        if (readerThread != null) {
            readerThread.interrupt();
        }
        if (serialPort != null && serialPort.isOpen()) {
            serialPort.closePort();
            log.info("串口已关闭");
        }
    }

    private void readLoop() {
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(serialPort.getInputStream(), StandardCharsets.UTF_8))) {

            String line;
            while (running && (line = reader.readLine()) != null) {
                line = line.trim();
                if (line.isEmpty()) continue;
                processLine(line);
            }
        } catch (Exception e) {
            if (running) {
                log.error("串口读取异常: {}", e.getMessage());
            }
        } finally {
            log.info("串口读取线程已结束");
        }
    }

    /**
     * 解析下位机数据并存入数据库
     * 格式示例：
     *   Temp:25.50    Set:27.00    PWM:350    Err:1.50    Mode:PID
     *   Mode:Baseline    Prog:45%
     *   Mode:Heat    Half:3/10    TrackingMax:30.5
     *   Mode:Result    Kp:6.0    Ki:0.6    Kd:0.7    Conf:5
     */
    private void processLine(String line) {
        try {
            Map<String, String> kv = new HashMap<>();
            String[] parts = line.split("\\s{2,}|\\t");
            for (String p : parts) {
                int col = p.indexOf(':');
                if (col > 0 && col < p.length() - 1) {
                    kv.put(p.substring(0, col).trim(), p.substring(col + 1).trim());
                }
            }

            String mode = kv.get("Mode");
            if (mode == null) return;

            String ts = kv.get("Temp");
            String ss = kv.get("Set");
            String ps = kv.get("PWM");
            String es = kv.get("Err");

            // 写入温度历史
            if (ts != null && !"Error".equals(mode) && !"Failed".equals(mode) && !"Result".equals(mode)) {
                double temp = Double.parseDouble(ts);
                double setpoint = ss != null ? Double.parseDouble(ss) : 0;
                int pwm = ps != null ? Integer.parseInt(ps) : 0;
                double error = es != null ? Double.parseDouble(es) : 0;

                tempService.saveRecord("wenkong-001", temp, setpoint, pwm, error, mode, LocalDateTime.now());
            }

            // 写入实时状态（不含 Kp/Ki/Kd 时保留上次的值）
            if (ts != null) {
                double temp = Double.parseDouble(ts);
                double setpoint = ss != null ? Double.parseDouble(ss) : 0;
                int pwm = ps != null ? Integer.parseInt(ps) : 0;

                // 取当前已存的状态作为默认值
                var cur = tempService.getLatestStatus("wenkong-001");
                double kp = cur != null ? cur.getKp() : 6.0;
                double ki = cur != null ? cur.getKi() : 0.6;
                double kd = cur != null ? cur.getKd() : 0.7;
                // 如果数据行里有 PID 参数则覆盖
                if (kv.containsKey("Kp")) kp = Double.parseDouble(kv.get("Kp"));
                if (kv.containsKey("Ki")) ki = Double.parseDouble(kv.get("Ki"));
                if (kv.containsKey("Kd")) kd = Double.parseDouble(kv.get("Kd"));

                tempService.updateStatus("wenkong-001", temp, setpoint, pwm, mode, kp, ki, kd);
            }

        } catch (Exception e) {
            log.debug("解析数据行失败: [{}] - {}", line, e.getMessage());
        }
    }
}
