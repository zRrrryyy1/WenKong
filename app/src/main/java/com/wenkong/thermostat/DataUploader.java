package com.wenkong.thermostat;

import com.google.gson.Gson;

import java.io.IOException;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.TimeUnit;

import okhttp3.Call;
import okhttp3.Callback;
import okhttp3.MediaType;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

/**
 * 云端数据上报器
 * 将温度数据上报到 Spring Boot 后端（方案 C）
 *
 * 服务器地址切换：
 *   - 模拟器测试:  http://10.0.2.2:8080  (模拟器用此地址访问宿主机)
 *   - 真机+USB:    http://localhost:8080
 *   - 真机+WiFi:   http://<电脑IP>:8080
 *   - cpolar公网:  https://xxx.cpolar.cn
 */
public class DataUploader {

    /** 在此处切换服务器地址 */
    private static final String BASE_URL = "http://10.192.37.160:8080";

    private final OkHttpClient client;
    private final Gson gson;
    private static final MediaType JSON = MediaType.get("application/json; charset=utf-8");

    public DataUploader() {
        this.client = new OkHttpClient.Builder()
                .connectTimeout(5, TimeUnit.SECONDS)
                .writeTimeout(5, TimeUnit.SECONDS)
                .readTimeout(5, TimeUnit.SECONDS)
                .build();
        this.gson = new Gson();
    }

    /**
     * 上报温度历史记录 + 实时状态（含 PID 参数）
     * 每次收到下位机数据时调用
     */
    public void upload(float temperature, float setpoint, int pwm, float error, String mode,
                       float kp, float ki, float kd) {
        uploadTempRecord(temperature, setpoint, pwm, error, mode);
        uploadCurrentStatus(temperature, setpoint, pwm, mode, kp, ki, kd);
    }

    /** 上报温度历史（POST /api/temps）*/
    private void uploadTempRecord(float temperature, float setpoint, int pwm, float error, String mode) {
        Map<String, Object> data = new HashMap<>();
        data.put("deviceId", "wenkong-001");
        data.put("temperature", temperature);
        data.put("setpoint", setpoint);
        data.put("pwm", pwm);
        data.put("error", error);
        data.put("mode", mode);

        Request request = new Request.Builder()
                .url(BASE_URL + "/api/temps")
                .post(RequestBody.create(gson.toJson(data), JSON))
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override public void onFailure(Call call, IOException e) { /* 静默 */ }
            @Override public void onResponse(Call call, Response response) { response.close(); }
        });
    }

    /** 上报实时状态（POST /api/current，含 PID 参数）*/
    private void uploadCurrentStatus(float temperature, float setpoint, int pwm, String mode,
                                     float kp, float ki, float kd) {
        Map<String, Object> data = new HashMap<>();
        data.put("deviceId", "wenkong-001");
        data.put("temperature", temperature);
        data.put("setpoint", setpoint);
        data.put("pwm", pwm);
        data.put("mode", mode);
        data.put("kp", kp);
        data.put("ki", ki);
        data.put("kd", kd);

        Request request = new Request.Builder()
                .url(BASE_URL + "/api/current")
                .post(RequestBody.create(gson.toJson(data), JSON))
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override public void onFailure(Call call, IOException e) { /* 静默 */ }
            @Override public void onResponse(Call call, Response response) { response.close(); }
        });
    }

    /** 上报PID参数（自整定结果）*/
    public void uploadTuneResult(float kp, float ki, float kd, int confidence, boolean applied) {
        Map<String, Object> data = new HashMap<>();
        data.put("deviceId", "wenkong-001");
        data.put("kp", kp);
        data.put("ki", ki);
        data.put("kd", kd);
        data.put("confidence", confidence);
        data.put("applied", applied);

        // 同时也更新实时状态中的PID参数
        Request request = new Request.Builder()
                .url(BASE_URL + "/api/current")
                .post(RequestBody.create(gson.toJson(data), JSON))
                .build();

        client.newCall(request).enqueue(new Callback() {
            @Override public void onFailure(Call call, IOException e) { /* 静默 */ }
            @Override public void onResponse(Call call, Response response) { response.close(); }
        });
    }
}
