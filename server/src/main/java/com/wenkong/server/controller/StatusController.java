package com.wenkong.server.controller;

import com.wenkong.server.model.DeviceStatus;
import com.wenkong.server.service.TempService;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.Map;

@RestController
@RequestMapping("/api")
public class StatusController {

    private final TempService tempService;

    public StatusController(TempService tempService) {
        this.tempService = tempService;
    }

    /** 实时状态上报 */
    @PostMapping("/current")
    public ResponseEntity<String> updateStatus(@RequestBody Map<String, Object> body) {
        String deviceId = (String) body.getOrDefault("deviceId", "wenkong-001");
        Double temperature = toDouble(body.get("temperature"));
        Double setpoint = toDouble(body.get("setpoint"));
        Integer pwm = toInt(body.get("pwm"));
        String mode = (String) body.getOrDefault("mode", "PID");
        Double kp = toDouble(body.get("kp"));
        Double ki = toDouble(body.get("ki"));
        Double kd = toDouble(body.get("kd"));

        tempService.updateStatus(deviceId, temperature, setpoint, pwm, mode, kp, ki, kd);
        return ResponseEntity.ok("ok");
    }

    /** 实时状态查询 */
    @GetMapping("/current")
    public ResponseEntity<?> getStatus(@RequestParam(defaultValue = "wenkong-001") String deviceId) {
        DeviceStatus status = tempService.getLatestStatus(deviceId);
        if (status == null) {
            return ResponseEntity.ok(Map.of("message", "no data yet", "deviceId", deviceId));
        }
        return ResponseEntity.ok(status);
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
