plugins { id("com.android.application") }

android {
    namespace = "ch.plum.hatintime"
    compileSdk = 35
    ndkVersion = "27.3.13750724"   // the NDK tools/build_android_so.sh uses
    buildToolsVersion = "35.0.0"
    defaultConfig {
        applicationId = "ch.plum.hatintime"
        minSdk = 29
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        ndk { abiFilters += "arm64-v8a" }
    }
    // libmain.so (runtime + lifted game code + DXVK) and libSDL3.so are prebuilt with the NDK from the
    // player's own A Hat in Time files by tools/build_android_so.sh.
    packaging { jniLibs { useLegacyPackaging = true } }
    val ks = System.getenv("HAT_KEYSTORE") ?: System.getenv("HP_KEYSTORE")
    signingConfigs {
        if (ks != null) create("own") {
            storeFile = file(ks)
            storePassword = System.getenv("HAT_KEYSTORE_PASSWORD") ?: System.getenv("HP_KEYSTORE_PASSWORD")
            keyAlias = System.getenv("HAT_KEY_ALIAS") ?: System.getenv("HP_KEY_ALIAS")
            keyPassword = System.getenv("HAT_KEY_PASSWORD") ?: System.getenv("HP_KEY_PASSWORD")
        }
    }
    buildTypes {
        getByName("debug") { isDebuggable = true }
        getByName("release") {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName(if (ks != null) "own" else "debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}
