package com.wenkong.server.service;

import com.wenkong.server.model.DeviceStatus;
import com.wenkong.server.model.TempRecord;
import com.wenkong.server.repository.TempRecordRepository;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.*;
import java.util.concurrent.ConcurrentHashMap;

@Service
public class TempService {

    private final TempRecordRepository repository;
    private final Map<String, DeviceStatus> latestStatus = new ConcurrentHashMap<>();

    public TempService(TempRecordRepository repository) {
        this.repository = repository;
    }

    public TempRecord saveRecord(String deviceId, Double temperature, Double setpoint,
                                  Integer pwm, Double errorVal, String mode, LocalDateTime timestamp) {
        TempRecord record = new TempRecord(deviceId, temperature, setpoint, pwm, errorVal, mode, timestamp);
        return repository.save(record);
    }

    public void updateStatus(String deviceId, Double temperature, Double setpoint,
                              Integer pwm, String mode, Double kp, Double ki, Double kd) {
        latestStatus.put(deviceId, new DeviceStatus(deviceId, temperature, setpoint, pwm, mode, kp, ki, kd));
    }

    public DeviceStatus getLatestStatus(String deviceId) {
        return latestStatus.get(deviceId);
    }

    public Map<String, DeviceStatus> getAllLatestStatus() {
        return Collections.unmodifiableMap(latestStatus);
    }

    public List<TempRecord> getHistory(String deviceId, LocalDateTime start, LocalDateTime end) {
        if (start == null) start = LocalDateTime.now().minusHours(1);
        if (end == null) end = LocalDateTime.now();
        return repository.findByDeviceIdAndTimestampBetweenOrderByTimestampAsc(deviceId, start, end);
    }

    public List<TempRecord> getRecent(String deviceId) {
        return repository.findTop60ByDeviceIdOrderByTimestampDesc(deviceId);
    }

    public long getRecordCount(String deviceId) {
        return repository.countByDeviceId(deviceId);
    }
}
