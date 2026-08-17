plugins {
    id("com.android.application")
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
                    "-DCANVAS_POC01_ANDROID=ON"
                )
                cppFlags += listOf("-std=c++20")
            }
        }
        val canvasPocAbi = providers.gradleProperty("canvasPocAbi").orElse("arm64-v8a")
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
        "../../../../../.deps/skia/resources/fonts"
    )
}
