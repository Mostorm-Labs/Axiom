plugins {
    id("com.android.application")
}

val canvasPoc03Abi = providers.gradleProperty("canvasPoc03Abi").orElse("arm64-v8a")
val canvasPoc03SdkTarget = canvasPoc03Abi.map {
    when (it) {
        "arm64-v8a" -> "android-arm64-v8a-gles3"
        "x86_64" -> "android-x86_64-gles3"
        else -> throw GradleException("Unsupported POC-03 ABI: $it")
    }
}

android {
    namespace = "dev.mostorm.canvas.poc03"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "dev.mostorm.canvas.poc03"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1"
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCANVAS_BUILD_POC01=OFF",
                    "-DCANVAS_BUILD_POC02=ON",
                    "-DCANVAS_POC02_BUILD_TESTS=OFF",
                    "-DCANVAS_POC02_ENABLE_SKIA=ON",
                    "-DCANVAS_POC02_BUILD_PLATFORM_SHELLS=OFF",
                    "-DCANVAS_BUILD_POC03=ON",
                    "-DCANVAS_POC03_BUILD_TESTS=OFF",
                    "-DCANVAS_POC03_ENABLE_SKIA=ON",
                    "-DCANVAS_POC03_ENABLE_INK_INTEGRATION=ON",
                    "-DCANVAS_SKIA_SDK_ROOT=${rootProject.projectDir}/../../../../.deps/skia-sdk/${canvasPoc03SdkTarget.get()}/release"
                )
                cppFlags += listOf("-std=c++20")
            }
        }
        ndk { abiFilters += listOf(canvasPoc03Abi.get()) }
    }

    externalNativeBuild {
        cmake {
            path = file("../../../../../CMakeLists.txt")
            version = "3.30.5"
        }
    }

    packaging {
        jniLibs {
            // Keep native libraries uncompressed so AGP can page-align them.
            useLegacyPackaging = false
        }
    }
}
