plugins {
    alias(libs.plugins.android.application)
}

android {
    namespace = "com.wenkong.thermostat"
    compileSdk = 32

    defaultConfig {
        applicationId = "com.wenkong.thermostat"
        minSdk = 21
        targetSdk = 32
        versionCode = 1
        versionName = "1.0"
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro"
            )
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

dependencies {
    // 纯 Android SDK + 蓝牙 — 无外部依赖
}