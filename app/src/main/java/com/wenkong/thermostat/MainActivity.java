package com.wenkong.thermostat;

import android.Manifest;
import android.app.Activity;
import android.app.AlertDialog;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.DialogInterface;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.View;
import android.widget.*;

import java.io.InputStream;
import java.io.OutputStream;
import java.util.*;

public class MainActivity extends Activity {

    // ===== UI =====
    private TextView tvTemp, tvSet, tvErr, tvPWM, tvMode, tvStatus, tvLog;
    private TextView tvATStatus, tvATProgress, tvATDetail;
    private EditText etSetTemp;
    private Button btnSetTemp, btnATune, btnAbort, btnApply, btnIgnore;
    private TempChartView chart;
    private View layoutATProgress;
    private ProgressBar progressBar;

    // ===== Bluetooth =====
    private BluetoothAdapter btAdapter;
    private BluetoothSocket btSocket;
    private ReaderThread readerThread;
    private boolean connected;

    // ===== 扫描 =====
    private final List<DiscoveredDevice> discoveredDevices = new ArrayList<>();
    private boolean isScanning = false;
    private final Handler scanTimeoutHandler = new Handler(Looper.getMainLooper());
    private AlertDialog scanDialog;

    // ===== 数据 =====
    private float currentTemp;
    private boolean hasATResult = false;     // 防止重复弹窗

    // ===== 常量 =====
    private static final UUID SPP_UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private static final int MSG_CONNECTED = 1;
    private static final int MSG_DISCONNECTED = 2;
    private static final int MSG_DATA = 3;
    private static final int MSG_LOG = 4;
    private static final int REQUEST_BT_PERMISSIONS = 100;
    private static final int SCAN_TIMEOUT_MS = 12000;

    // ===== Handler =====
    private final Handler handler = new Handler(Looper.getMainLooper()) {
        @Override
        public void handleMessage(Message msg) {
            String data = (String) msg.obj;
            switch (msg.what) {
                case MSG_CONNECTED:
                    connected = true;
                    tvStatus.setText("已连接");
                    log("已连接 " + data);
                    break;
                case MSG_DISCONNECTED:
                    connected = false;
                    tvStatus.setText("已断开");
                    log("已断开");
                    break;
                case MSG_DATA:
                    parse(data);
                    log(data);
                    break;
                case MSG_LOG:
                    log(data);
                    break;
            }
        }
    };

    // ===== 蓝牙扫描广播接收器 =====
    private final BroadcastReceiver discoveryReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            String action = intent.getAction();
            if (BluetoothDevice.ACTION_FOUND.equals(action)) {
                BluetoothDevice dev = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);
                short rssi = intent.getShortExtra(BluetoothDevice.EXTRA_RSSI, (short) 0);
                if (dev == null) return;

                // 去重：已存在则更新 RSSI，否则新增
                boolean found = false;
                for (DiscoveredDevice d : discoveredDevices) {
                    if (d.address.equals(dev.getAddress())) {
                        d.rssi = rssi;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    discoveredDevices.add(new DiscoveredDevice(
                            dev.getName() != null ? dev.getName() : "未知设备",
                            dev.getAddress(), rssi, dev.getBondState() != BluetoothDevice.BOND_NONE));
                }
                updateScanDialog();
            }
        }
    };

    // ===== 设备条目数据结构 =====
    private static class DiscoveredDevice {
        String name;
        String address;
        short rssi;
        boolean isBonded;
        DiscoveredDevice(String name, String address, short rssi, boolean isBonded) {
            this.name = name; this.address = address; this.rssi = rssi; this.isBonded = isBonded;
        }
        String getDisplayName() {
            String bonded = isBonded ? " [已配对]" : "";
            String rssiStr;
            if (rssi > -50) rssiStr = "■■■";
            else if (rssi > -70) rssiStr = "■■□";
            else rssiStr = "■□□";
            return name + bonded + "\n" + address + "  " + rssiStr;
        }
    }

    // ===== Activity 生命周期 =====
    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        setContentView(R.layout.activity_main);

        // 绑定 UI
        tvTemp   = findViewById(R.id.tvTemp);
        tvSet    = findViewById(R.id.tvSet);
        tvErr    = findViewById(R.id.tvErr);
        tvPWM    = findViewById(R.id.tvPWM);
        tvMode   = findViewById(R.id.tvMode);
        tvStatus = findViewById(R.id.tvStatus);
        tvLog    = findViewById(R.id.tvLog);
        etSetTemp = findViewById(R.id.etSetTemp);
        btnSetTemp = findViewById(R.id.btnSetTemp);
        btnATune   = findViewById(R.id.btnATune);
        btnAbort   = findViewById(R.id.btnAbort);
        btnApply   = findViewById(R.id.btnApply);
        btnIgnore  = findViewById(R.id.btnIgnore);
        chart      = findViewById(R.id.chart);
        layoutATProgress = findViewById(R.id.layoutATProgress);
        progressBar = findViewById(R.id.progressBar);
        tvATStatus  = findViewById(R.id.tvATStatus);
        tvATProgress = findViewById(R.id.tvATProgress);
        tvATDetail  = findViewById(R.id.tvATDetail);

        btAdapter = BluetoothAdapter.getDefaultAdapter();

        // 标题栏：点击连接 / 长按断开
        findViewById(R.id.titleBar).setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                if (checkBluetoothPermissions()) showDeviceList();
                else requestBluetoothPermissions();
            }
        });
        findViewById(R.id.titleBar).setOnLongClickListener(new View.OnLongClickListener() {
            @Override public boolean onLongClick(View v) { disconnect(); return true; }
        });

        btnSetTemp.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) {
                String val = etSetTemp.getText().toString().trim();
                if (!TextUtils.isEmpty(val)) sendCmd(val);
            }
        });
        btnATune.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { sendCmd("ATUNE"); }
        });
        btnAbort.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { sendCmd("ABORT"); }
        });
        btnApply.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { sendCmd("APPLY"); enableATButtons(false); }
        });
        btnIgnore.setOnClickListener(new View.OnClickListener() {
            @Override public void onClick(View v) { sendCmd("IGNORE"); enableATButtons(false); }
        });
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        disconnect();
        stopDiscovery();
    }

    // ================================================================
    //  F-02：Android 12+ 运行时权限
    // ================================================================

    private boolean checkBluetoothPermissions() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.M) return true;

        // Android 12+：需要 BLUETOOTH_SCAN + BLUETOOTH_CONNECT + ACCESS_FINE_LOCATION
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            return checkSelfPermission(Manifest.permission.BLUETOOTH_SCAN) == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED
                    && checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
        }
        // Android 10-11：需要 ACCESS_FINE_LOCATION
        return checkSelfPermission(Manifest.permission.ACCESS_FINE_LOCATION) == PackageManager.PERMISSION_GRANTED;
    }

    private void requestBluetoothPermissions() {
        // 需要向用户解释为什么需要权限
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (shouldShowRequestPermissionRationale(Manifest.permission.BLUETOOTH_SCAN)
                    || shouldShowRequestPermissionRationale(Manifest.permission.ACCESS_FINE_LOCATION)) {
                new AlertDialog.Builder(this)
                        .setTitle("需要蓝牙权限")
                        .setMessage("WenKong 需要通过蓝牙扫描和连接温控设备，请允许相关权限。")
                        .setPositiveButton("去授权", new DialogInterface.OnClickListener() {
                            @Override public void onClick(DialogInterface d, int w) {
                                requestPermissions(
                                        new String[]{
                                                Manifest.permission.BLUETOOTH_SCAN,
                                                Manifest.permission.BLUETOOTH_CONNECT,
                                                Manifest.permission.ACCESS_FINE_LOCATION
                                        }, REQUEST_BT_PERMISSIONS);
                            }
                        }).setNegativeButton("取消", null).show();
                return;
            }
            requestPermissions(
                    new String[]{
                            Manifest.permission.BLUETOOTH_SCAN,
                            Manifest.permission.BLUETOOTH_CONNECT,
                            Manifest.permission.ACCESS_FINE_LOCATION
                    }, REQUEST_BT_PERMISSIONS);
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            if (shouldShowRequestPermissionRationale(Manifest.permission.ACCESS_FINE_LOCATION)) {
                new AlertDialog.Builder(this)
                        .setTitle("需要位置权限")
                        .setMessage("Android 10 及以上需要位置权限才能扫描蓝牙设备。")
                        .setPositiveButton("去授权", new DialogInterface.OnClickListener() {
                            @Override public void onClick(DialogInterface d, int w) {
                                requestPermissions(
                                        new String[]{Manifest.permission.ACCESS_FINE_LOCATION},
                                        REQUEST_BT_PERMISSIONS);
                            }
                        }).setNegativeButton("取消", null).show();
                return;
            }
            requestPermissions(new String[]{Manifest.permission.ACCESS_FINE_LOCATION}, REQUEST_BT_PERMISSIONS);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode != REQUEST_BT_PERMISSIONS) return;

        boolean allGranted = true;
        for (int r : grantResults) {
            if (r != PackageManager.PERMISSION_GRANTED) { allGranted = false; break; }
        }

        if (allGranted) {
            showDeviceList();
        } else {
            // 检查是否被永久拒绝（不再询问）
            boolean neverAskAgain = false;
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                neverAskAgain = !shouldShowRequestPermissionRationale(Manifest.permission.BLUETOOTH_SCAN)
                        && !shouldShowRequestPermissionRationale(Manifest.permission.ACCESS_FINE_LOCATION);
            } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
                neverAskAgain = !shouldShowRequestPermissionRationale(Manifest.permission.ACCESS_FINE_LOCATION);
            }

            if (neverAskAgain) {
                new AlertDialog.Builder(this)
                        .setTitle("权限被永久拒绝")
                        .setMessage("请在系统设置中手动开启蓝牙相关权限。")
                        .setPositiveButton("去设置", new DialogInterface.OnClickListener() {
                            @Override public void onClick(DialogInterface d, int w) {
                                Intent intent = new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS);
                                intent.setData(android.net.Uri.parse("package:" + getPackageName()));
                                startActivity(intent);
                            }
                        }).setNegativeButton("取消", null).show();
            } else {
                toast("需要蓝牙权限才能连接设备");
            }
        }
    }

    // ================================================================
    //  F-01：蓝牙设备发现扫描
    // ================================================================

    private void showDeviceList() {
        if (!btAdapter.isEnabled()) {
            toast("请先打开蓝牙");
            return;
        }
        if (!checkBluetoothPermissions()) {
            requestBluetoothPermissions();
            return;
        }

        // 开始扫描
        startDiscovery();

        // 构建扫描对话框
        AlertDialog.Builder builder = new AlertDialog.Builder(this);
        builder.setTitle("正在扫描...");
        builder.setNegativeButton("取消", new DialogInterface.OnClickListener() {
            @Override public void onClick(DialogInterface d, int w) { stopDiscovery(); }
        });
        builder.setNeutralButton("重新扫描", new DialogInterface.OnClickListener() {
            @Override public void onClick(DialogInterface d, int w) {
                stopDiscovery();
                discoveredDevices.clear();
                startDiscovery();
                updateScanDialog();
            }
        });
        scanDialog = builder.show();
        updateScanDialog();
    }

    private void startDiscovery() {
        if (isScanning) return;
        isScanning = true;
        discoveredDevices.clear();

        // 注册广播接收器
        registerReceiver(discoveryReceiver, new IntentFilter(BluetoothDevice.ACTION_FOUND));

        // 先加入已配对设备
        if (btAdapter.isEnabled()) {
            for (BluetoothDevice dev : btAdapter.getBondedDevices()) {
                if (dev != null) {
                    discoveredDevices.add(new DiscoveredDevice(
                            dev.getName() != null ? dev.getName() : "未知设备",
                            dev.getAddress(), (short) 0, true));
                }
            }
        }

        btAdapter.startDiscovery();

        // 超时自动停止
        scanTimeoutHandler.postDelayed(new Runnable() {
            @Override public void run() {
                if (isScanning) {
                    stopDiscovery();
                    updateScanDialogTitle("扫描完成");
                }
            }
        }, SCAN_TIMEOUT_MS);
    }

    private void stopDiscovery() {
        if (!isScanning) return;
        isScanning = false;
        scanTimeoutHandler.removeCallbacksAndMessages(null);
        try {
            unregisterReceiver(discoveryReceiver);
        } catch (Exception ignored) {}
        try {
            btAdapter.cancelDiscovery();
        } catch (Exception ignored) {}
    }

    private void updateScanDialog() {
        if (scanDialog == null || !scanDialog.isShowing()) return;

        final String[] items = new String[discoveredDevices.size()];
        for (int i = 0; i < discoveredDevices.size(); i++) {
            items[i] = discoveredDevices.get(i).getDisplayName();
        }

        String title = isScanning ? "正在扫描... (" + discoveredDevices.size() + ")" : "已完成 (" + discoveredDevices.size() + ")";
        scanDialog.setTitle(title);
        // 用 ListView 自定义布局代替 setItems（支持动态更新）
        ListView listView = new ListView(this);
        ArrayAdapter<String> adapter = new ArrayAdapter<>(this,
                android.R.layout.simple_list_item_1, items);
        listView.setAdapter(adapter);
        listView.setOnItemClickListener(new AdapterView.OnItemClickListener() {
            @Override public void onItemClick(AdapterView<?> parent, View view, int pos, long id) {
                if (pos >= 0 && pos < discoveredDevices.size()) {
                    DiscoveredDevice dev = discoveredDevices.get(pos);
                    stopDiscovery();
                    scanDialog.dismiss();
                    connectToDevice(dev.address);
                }
            }
        });
        scanDialog.setView(listView);
    }

    private void updateScanDialogTitle(String title) {
        if (scanDialog != null && scanDialog.isShowing()) {
            scanDialog.setTitle(title);
        }
    }

    // ================================================================
    //  蓝牙连接
    // ================================================================

    private void connectToDevice(final String address) {
        if (!btAdapter.isEnabled()) { toast("蓝牙未开启"); return; }

        BluetoothDevice dev;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            dev = btAdapter.getRemoteDevice(address);
        } else {
            dev = btAdapter.getRemoteDevice(address);
        }

        disconnect();
        new Thread(new Runnable() { @Override public void run() {
            try {
                BluetoothSocket tmp;
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                    tmp = dev.createRfcommSocketToServiceRecord(SPP_UUID);
                } else {
                    tmp = dev.createRfcommSocketToServiceRecord(SPP_UUID);
                }
                btAdapter.cancelDiscovery();
                tmp.connect();
                btSocket = tmp;
                sendMsg(MSG_CONNECTED, dev.getName());
                readerThread = new ReaderThread();
                readerThread.start();
            } catch (Exception e) {
                sendMsg(MSG_DISCONNECTED, "失败: " + e.getMessage());
            }
        }}).start();
    }

    private void disconnect() {
        if (readerThread != null) { readerThread.interrupt(); readerThread = null; }
        try { if (btSocket != null) btSocket.close(); } catch (Exception ignored) {}
        btSocket = null;
        connected = false;
        runOnUiThread(new Runnable() {
            @Override public void run() {
                tvStatus.setText("已断开");
                showATProgress(false);
                enableATButtons(false);
                hasATResult = false;
            }
        });
    }

    private void sendCmd(String cmd) {
        if (btSocket == null || !btSocket.isConnected()) { toast("未连接"); return; }
        try {
            OutputStream os = btSocket.getOutputStream();
            os.write((cmd + "\r\n").getBytes());
            os.flush();
            sendMsg(MSG_LOG, ">> " + cmd);
        } catch (Exception e) { toast("失败: " + e.getMessage()); }
    }

    private class ReaderThread extends Thread {
        public void run() {
            try {
                InputStream is = btSocket.getInputStream();
                byte[] buf = new byte[256];
                StringBuilder sb = new StringBuilder();
                while (!isInterrupted()) {
                    int n = is.read(buf);
                    if (n <= 0) continue;
                    for (int i = 0; i < n; i++) {
                        char c = (char) buf[i];
                        if (c == '\r' || c == '\n') {
                            if (sb.length() > 0) {
                                sendMsg(MSG_DATA, sb.toString());
                                sb.setLength(0);
                            }
                        } else sb.append(c);
                    }
                }
            } catch (Exception e) {
                if (!isInterrupted()) sendMsg(MSG_DISCONNECTED, "连接断开");
            }
        }
    }

    // ================================================================
    //  F-03 & F-04：数据解析 + 自整定进度/结果处理
    // ================================================================

    private void parse(String line) {
        Map<String, String> kv = new HashMap<>();
        String[] parts = line.split("    ");
        for (String p : parts) {
            int col = p.indexOf(':');
            if (col > 0 && col < p.length() - 1)
                kv.put(p.substring(0, col).trim(), p.substring(col + 1).trim());
        }
        final String mode = kv.get("Mode");
        if (mode == null) return;

        // ---- 温度 + 模式 ----
        String ts = kv.get("Temp");
        if (ts != null) try {
            currentTemp = Float.parseFloat(ts);
            tvTemp.setText(String.format("%.1f°C", currentTemp));
            tvMode.setText("Mode: " + mode);
            chart.addTemp(currentTemp);
        } catch (NumberFormatException ignored) {}

        // ---- 设定温度 ----
        String ss = kv.get("Set");
        if (ss != null) { tvSet.setText(ss + "°C"); etSetTemp.setText(ss); }

        // ---- PWM ----
        String ps = kv.get("PWM");
        if (ps != null) tvPWM.setText(ps);

        // ---- 误差 ----
        String es = kv.get("Err");
        if (es != null) tvErr.setText(es + "°C");

        // ============================================================
        //  F-04：自整定进度可视化
        // ============================================================
        if ("PID".equals(mode) || "Error".equals(mode)) {
            // 正常控温模式 → 隐藏进度条
            showATProgress(false);
        } else {
            showATProgress(true);
            updateATProgress(mode, kv);
        }

        // ============================================================
        //  F-03：自整定完成 → APPLY / IGNORE
        // ============================================================
        if ("Result".equals(mode)) {
            String kp = kv.get("Kp");
            String ki = kv.get("Ki");
            String kd = kv.get("Kd");
            String conf = kv.get("Conf");
            if (kp != null && ki != null && kd != null) {
                enableATButtons(true);
                if (!hasATResult) {
                    hasATResult = true;
                    showATResultDialog(kp, ki, kd, conf);
                }
            }
        }
        if ("Failed".equals(mode)) {
            enableATButtons(false);
            showATProgress(true);
            tvATStatus.setText("❌ 整定失败");
            tvATProgress.setText("");
            tvATDetail.setText("过冲/噪声超出安全阈值，请检查系统");
            toast("整定失败");
        }
    }

    // ================================================================
    //  F-04：自整定进度更新
    // ================================================================

    private void showATProgress(boolean show) {
        layoutATProgress.setVisibility(show ? View.VISIBLE : View.GONE);
    }

    private void updateATProgress(String mode, Map<String, String> kv) {
        switch (mode) {
            case "Baseline": {
                String prog = kv.get("Prog");
                int p = parsePercent(prog);
                progressBar.setProgress(p);
                tvATStatus.setText("📊 基线采集");
                tvATProgress.setText(p + "%");
                tvATDetail.setText("等待温度稳定...");
                break;
            }
            case "Heat": {
                String half = kv.get("Half");
                int progress = calcHalfCycleProgress(half);
                progressBar.setProgress(progress);
                tvATStatus.setText("🔥 加热阶段");
                tvATProgress.setText(progress + "%");
                String max = kv.get("TrackingMax");
                String halfStr = half != null ? half : "?";
                tvATDetail.setText("半周期 " + halfStr + "  最高 " + (max != null ? max + "°C" : "?"));
                break;
            }
            case "Cool": {
                String half = kv.get("Half");
                int progress = calcHalfCycleProgress(half);
                progressBar.setProgress(progress);
                tvATStatus.setText("❄️ 冷却阶段");
                tvATProgress.setText(progress + "%");
                String min = kv.get("TrackingMin");
                String halfStr = half != null ? half : "?";
                tvATDetail.setText("半周期 " + halfStr + "  最低 " + (min != null ? min + "°C" : "?"));
                break;
            }
            case "Result": {
                progressBar.setProgress(100);
                tvATStatus.setText("✅ 整定完成");
                tvATProgress.setText("100%");
                tvATDetail.setText("请点击「应用参数」或「忽略」");
                break;
            }
            case "Failed": {
                progressBar.setProgress(0);
                break;
            }
            // PID / Error / 其他 → 不处理（已在上层隐藏）
        }
    }

    private int parsePercent(String prog) {
        if (prog == null) return 0;
        try {
            String num = prog.replace("%", "").trim();
            return Integer.parseInt(num);
        } catch (NumberFormatException e) {
            return 0;
        }
    }

    private int calcHalfCycleProgress(String half) {
        if (half == null) return 10;
        try {
            String[] parts = half.split("/");
            if (parts.length == 2) {
                int current = Integer.parseInt(parts[0].trim());
                int total = Integer.parseInt(parts[1].trim());
                if (total > 0) {
                    return 10 + (int) ((float) current / total * 80);
                }
            }
        } catch (NumberFormatException ignored) {}
        return 10;
    }

    // ================================================================
    //  F-03：自整定结果弹窗
    // ================================================================

    private void showATResultDialog(String kp, String ki, String kd, String conf) {
        String stars;
        if (conf != null) {
            int c = 0;
            try { c = Integer.parseInt(conf.trim()); } catch (NumberFormatException ignored) {}
            stars = getStars(c);
        } else {
            stars = "★★★☆☆";
        }

        String msg = "推荐 PID 参数：\n\n"
                + "  Kp = " + kp + "\n"
                + "  Ki = " + ki + "\n"
                + "  Kd = " + kd + "\n\n"
                + "置信度：" + stars;

        new AlertDialog.Builder(this)
                .setTitle("🎯 自整定完成")
                .setMessage(msg)
                .setPositiveButton("✓ 应用参数", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        sendCmd("APPLY");
                        enableATButtons(false);
                    }
                })
                .setNegativeButton("✗ 忽略", new DialogInterface.OnClickListener() {
                    @Override public void onClick(DialogInterface d, int w) {
                        sendCmd("IGNORE");
                        enableATButtons(false);
                    }
                })
                .setCancelable(false)
                .show();
    }

    private String getStars(int confidence) {
        if (confidence >= 5) return "★★★★★";
        if (confidence >= 4) return "★★★★☆";
        if (confidence >= 3) return "★★★☆☆";
        if (confidence >= 2) return "★★☆☆☆";
        return "★☆☆☆☆";
    }

    private void enableATButtons(boolean enabled) {
        btnApply.setEnabled(enabled);
        btnIgnore.setEnabled(enabled);
        if (!enabled) hasATResult = false;
    }

    // ================================================================
    //  工具方法
    // ================================================================

    private void sendMsg(int what, String obj) {
        handler.sendMessage(handler.obtainMessage(what, obj));
    }
    private void log(String s) {
        String cur = tvLog.getText().toString();
        if ("等待连接...".equals(cur)) cur = "";
        tvLog.setText(s + "\n" + cur);
    }
    private void toast(String s) {
        final String msg = s;
        runOnUiThread(new Runnable() {
            @Override public void run() { Toast.makeText(MainActivity.this, msg, Toast.LENGTH_SHORT).show(); }
        });
    }
}
