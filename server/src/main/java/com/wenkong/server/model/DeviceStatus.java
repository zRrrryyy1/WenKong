package com.wenkong.server.model;

import java.time.LocalDateTime;

public class DeviceStatus {
    private String deviceId;
    private Double temperature;
    private Double setpoint;
    private Integer pwm;
    private String mode;
    private Double kp;
    private Double ki;
    private Double kd;
    private LocalDateTime lastUpdate;

    public DeviceStatus() {}

    public DeviceStatus(String deviceId, Double temperature, Double setpoint,
                        Integer pwm, String mode, Double kp, Double ki, Double kd) {
        this.deviceId = deviceId;
        this.temperature = temperature;
        this.setpoint = setpoint;
        this.pwm = pwm;
        this.mode = mode;
        this.kp = kp;
        this.ki = ki;
        this.kd = kd;
        this.lastUpdate = LocalDateTime.now();
    }

    // Getters & Setters
    public String getDeviceId() { return deviceId; }
    public void setDeviceId(String deviceId) { this.deviceId = deviceId; }

    public Double getTemperature() { return temperature; }
    public void setTemperature(Double temperature) { this.temperature = temperature; }

    public Double getSetpoint() { return setpoint; }
    public void setSetpoint(Double setpoint) { this.setpoint = setpoint; }

    public Integer getPwm() { return pwm; }
    public void setPwm(Integer pwm) { this.pwm = pwm; }

    public String getMode() { return mode; }
    public void setMode(String mode) { this.mode = mode; }

    public Double getKp() { return kp; }
    public void setKp(Double kp) { this.kp = kp; }

    public Double getKi() { return ki; }
    public void setKi(Double ki) { this.ki = ki; }

    public Double getKd() { return kd; }
    public void setKd(Double kd) { this.kd = kd; }

    public LocalDateTime getLastUpdate() { return lastUpdate; }
    public void setLastUpdate(LocalDateTime lastUpdate) { this.lastUpdate = lastUpdate; }
}
