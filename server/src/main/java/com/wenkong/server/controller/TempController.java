package com.wenkong.server.controller;

import com.wenkong.server.model.TempRecord;
import com.wenkong.server.service.TempService;
import org.springframework.format.annotation.DateTimeFormat;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.time.LocalDateTime;
import java.util.*;

@RestController
@RequestMapping("/api")
public class TempController {

    private final TempService tempService;

    public TempController(TempService tempService) {
        this.tempService = tempService;
    }

    /** 温度数据上报 */
    @PostMapping("/temps")
    public ResponseEntity<Map<String, Object>> uploadTemp(@RequestBody Map<String, Object> body) {
        String deviceId = (String) body.getOrDefault("deviceId", "wenkong-001");
        Double temperature = toDouble(body.get("temperature"));
        Double setpoint = toDouble(body.get("setpoint"));
        Integer pwm = toInt(body.get("pwm"));
        Double errorVal = toDouble(body.get("error"));
        String mode = (String) body.getOrDefault("mode", "PID");
        LocalDateTime timestamp = LocalDateTime.now();

        TempRecord saved = tempService.saveRecord(deviceId, temperature, setpoint, pwm, errorVal, mode, timestamp);

        Map<String, Object> resp = new HashMap<>();
        resp.put("id", saved.getId());
        resp.put("receivedAt", saved.getReceivedAt().toString());
        return ResponseEntity.status(201).body(resp);
    }

    /** 历史数据查询 */
    @GetMapping("/temps")
    public ResponseEntity<Map<String, Object>> getHistory(
            @RequestParam(defaultValue = "wenkong-001") String deviceId,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime start,
            @RequestParam(required = false) @DateTimeFormat(iso = DateTimeFormat.ISO.DATE_TIME) LocalDateTime end) {

        List<TempRecord> records = tempService.getHistory(deviceId, start, end);

        Map<String, Object> resp = new HashMap<>();
        resp.put("deviceId", deviceId);
        resp.put("count", records.size());
        resp.put("records", records.stream().map(this::toRecordMap).toList());
        return ResponseEntity.ok(resp);
    }

    /** 最近 60 条（供仪表盘曲线用） */
    @GetMapping("/temps/recent")
    public ResponseEntity<List<Map<String, Object>>> getRecent(
            @RequestParam(defaultValue = "wenkong-001") String deviceId) {
        List<TempRecord> records = tempService.getRecent(deviceId);
        return ResponseEntity.ok(records.stream().map(this::toRecordMap).toList());
    }

    private Map<String, Object> toRecordMap(TempRecord r) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("temperature", r.getTemperature());
        m.put("setpoint", r.getSetpoint());
        m.put("pwm", r.getPwm());
        m.put("error", r.getErrorVal());
        m.put("mode", r.getMode());
        m.put("timestamp", r.getTimestamp() != null ? r.getTimestamp().toString() : null);
        return m;
    }

    private Double toDouble(Object v) {
        if (v instanceof Number) return ((Number) v).doubleValue();
        return null;
    }

    private Integer toInt(Object v) {
        if (v instanceof Number) return ((Number) v).intValue();
        return null;
    }
}
