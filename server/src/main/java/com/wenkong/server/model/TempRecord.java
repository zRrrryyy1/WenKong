package com.wenkong.server.model;

import jakarta.persistence.*;
import java.time.LocalDateTime;

@Entity
@Table(name = "temp_records")
public class TempRecord {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(name = "device_id", length = 32)
    private String deviceId;

    @Column(nullable = false)
    private Double temperature;

    private Double setpoint;

    private Integer pwm;

    @Column(name = "error_val")
    private Double errorVal;

    @Column(length = 16)
    private String mode;

    private LocalDateTime timestamp;

    @Column(name = "received_at")
    private LocalDateTime receivedAt;

    public TempRecord() {}

    public TempRecord(String deviceId, Double temperature, Double setpoint,
                      Integer pwm, Double errorVal, String mode, LocalDateTime timestamp) {
        this.deviceId = deviceId;
        this.temperature = temperature;
        this.setpoint = setpoint;
        this.pwm = pwm;
        this.errorVal = errorVal;
        this.mode = mode;
        this.timestamp = timestamp;
        this.receivedAt = LocalDateTime.now();
    }

    // Getters & Setters
    public Long getId() { return id; }
    public void setId(Long id) { this.id = id; }

    public String getDeviceId() { return deviceId; }
    public void setDeviceId(String deviceId) { this.deviceId = deviceId; }

    public Double getTemperature() { return temperature; }
    public void setTemperature(Double temperature) { this.temperature = temperature; }

    public Double getSetpoint() { return setpoint; }
    public void setSetpoint(Double setpoint) { this.setpoint = setpoint; }

    public Integer getPwm() { return pwm; }
    public void setPwm(Integer pwm) { this.pwm = pwm; }

    public Double getErrorVal() { return errorVal; }
    public void setErrorVal(Double errorVal) { this.errorVal = errorVal; }

    public String getMode() { return mode; }
    public void setMode(String mode) { this.mode = mode; }

    public LocalDateTime getTimestamp() { return timestamp; }
    public void setTimestamp(LocalDateTime timestamp) { this.timestamp = timestamp; }

    public LocalDateTime getReceivedAt() { return receivedAt; }
    public void setReceivedAt(LocalDateTime receivedAt) { this.receivedAt = receivedAt; }
}
