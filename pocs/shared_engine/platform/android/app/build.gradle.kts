plugins {
    id("com.android.application")
}

val canvasPocAbi = providers.gradleProperty("canvasPocAbi").orElse("arm64-v8a")
val canvasPocSdkTarget = canvasPocAbi.map {
    when (it) {
        "arm64-v8a" -> "android-arm64-v8a-gles3"
        "x86_64" -> "android-x86_64-gles3"
        else -> throw GradleException("Unsupported POC-01 ABI: $it")
    }
}

android {
    namespace = "dev.mostorm.canvas"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "dev.mostorm.canvas.poc01"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "1.0"
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCANVAS_POC01_BUILD_TESTS=OFF",
                    "-DCANVAS_POC01_ENABLE_SKIA=ON",
                    "-DCANVAS_POC01_ANDROID=ON",
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
    sourceSets["main"].assets.srcDirs(
        "../../../fixtures",
        "../../../../../.deps/skia-sdk/${canvasPocSdkTarget.get()}/resources/fonts"
    )
}
