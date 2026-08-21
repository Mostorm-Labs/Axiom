plugins {
    id("com.android.application")
    kotlin("android")
}

val canvasPoc04Abi = providers.gradleProperty("canvasPoc04Abi").orElse("arm64-v8a")
val canvasPoc04SdkTarget = canvasPoc04Abi.map {
    when (it) {
        "arm64-v8a" -> "android-arm64-v8a-gles3"
        "x86_64" -> "android-x86_64-gles3"
        else -> throw GradleException("Unsupported POC-04 ABI: $it")
    }
}

android {
    namespace = "com.mostorm.canvas.poc04"
    compileSdk = 35
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "com.mostorm.canvas.poc04"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.1.0"
        buildConfigField(
            "boolean",
            "CANVAS_POC04_CANONICAL_BEHAVIOR",
            providers.gradleProperty("canvasPoc04CanonicalBehavior").orElse("false").get(),
        )
        buildConfigField(
            "boolean",
            "CANVAS_POC04_ENFORCE_PERFORMANCE_GATE",
            providers.gradleProperty("canvasPoc04EnforcePerformanceGate").orElse("true").get(),
        )
        externalNativeBuild {
            cmake {
                arguments += listOf(
                    "-DCANVAS_POC04_BUILD_TESTS=OFF",
                    "-DCANVAS_POC04_ENABLE_SKPARAGRAPH=ON",
                    "-DCANVAS_POC04_BUILD_ANDROID=ON",
                    "-DCANVAS_POC04_SKIA_SDK_ROOT=${rootProject.projectDir}/../../../../.deps/skia-sdk-poc04/${canvasPoc04SdkTarget.get()}",
                )
                cppFlags += listOf("-std=c++20")
            }
        }
        ndk { abiFilters += listOf(canvasPoc04Abi.get()) }
    }

    sourceSets["main"].java.srcDir("../src/main/java")
    buildFeatures { buildConfig = true }
    buildTypes {
        getByName("release") {
            // The canonical performance gate must measure optimized native code.
            // Debug signing only makes this POC-only APK installable in CI.
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    externalNativeBuild {
        cmake {
            path = file("../../../CMakeLists.txt")
            version = "3.30.5"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

val canvasPoc04FontRoot = rootProject.file("../../../../.deps/assets")
val canvasPoc04FontNames = listOf("Roboto-Regular.ttf", "NotoSansCJK-VF-subset.otf.ttc")
tasks.register("verifyPoc04Fonts") {
    doLast {
        canvasPoc04FontNames.forEach { name ->
            check(canvasPoc04FontRoot.resolve(name).isFile) {
                "Missing POC-04 font fixture $name; run tools/bootstrap_deps.py --font-only"
            }
        }
    }
}
tasks.register<Copy>("syncPoc04Fonts") {
    from(canvasPoc04FontRoot) {
        include(*canvasPoc04FontNames.toTypedArray())
    }
    into(layout.projectDirectory.dir("src/main/assets/fonts"))
}
tasks.named("preBuild").configure {
    dependsOn("verifyPoc04Fonts", "syncPoc04Fonts")
}

kotlin {
    compilerOptions { jvmTarget.set(org.jetbrains.kotlin.gradle.dsl.JvmTarget.JVM_17) }
}
