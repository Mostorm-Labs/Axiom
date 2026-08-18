plugins { id("com.android.application") }

val canvasPocAbi = providers.gradleProperty("canvasPocAbi").orElse("arm64-v8a")
val canvasPocSdkTarget = canvasPocAbi.map {
    when (it) {
        "arm64-v8a" -> "android-arm64-v8a-gles3"
        "x86_64" -> "android-x86_64-gles3"
        else -> throw GradleException("Unsupported POC-02 ABI: $it")
    }
}

android {
    namespace = "dev.mostorm.canvas.poc02"
    compileSdk = 35
    ndkVersion = "27.2.12479018"
    defaultConfig {
        applicationId = "dev.mostorm.canvas.poc02"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCANVAS_BUILD_POC01=OFF",
                    "-DCANVAS_BUILD_POC02=ON",
                    "-DCANVAS_POC02_BUILD_TESTS=OFF",
                    "-DCANVAS_POC02_ENABLE_SKIA=ON",
                    "-DCANVAS_SKIA_SDK_ROOT=${rootProject.projectDir}/../../../../.deps/skia-sdk/${canvasPocSdkTarget.get()}"
                )
                cppFlags += listOf("-std=c++20")
            }
        }
        ndk { abiFilters += listOf(canvasPocAbi.get()) }
    }
    externalNativeBuild {
        cmake {
            path = file("../../../../../CMakeLists.txt")
            version = "3.30.5"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    sourceSets["main"].assets.srcDirs("../../../fixtures", "../../../goldens")
}
