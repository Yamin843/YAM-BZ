plugins {
    id("com.android.application")
}

// قراءة ABI من gradle.properties (افتراضي: arm64-v8a)
val targetAbi = (project.findProperty("yambz.targetAbi") as String?) ?: "arm64-v8a"

android {
    buildFeatures { buildConfig = true }
    namespace = "com.yambz"
    compileSdk = 34
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.yambz"
        minSdk = 26
        targetSdk = 34
        versionCode = 1
        versionName = "1.0.0"

        ndk {
            abiFilters += targetAbi
        }

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20", "-frtti", "-fexceptions")
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DCMAKE_BUILD_TYPE=Release",
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            ndk { debugSymbolLevel = "FULL" }
        }
        debug {
            isJniDebuggable = true
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }
}

dependencies {
    implementation(files("../albatross-libs/aar/core.jar"))
    implementation(files("../albatross-libs/aar/annotation.jar"))
}

// تحقق من وجود ABI
tasks.register("verifyAbi") {
    doLast {
        println("YAMBZ: target ABI = $targetAbi")
    }
}
