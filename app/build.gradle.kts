// Nova Engine — app/build.gradle.kts
//
// Android application module. Builds the Kotlin layer (activity, surface,
// input, assets) together with the native engine (CMake -> libnova.so).

import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val novaProps = Properties().apply {
    val f = rootProject.file("gradle.properties")
    if (f.exists()) f.inputStream().use { load(it) }
}
fun novaProp(key: String, default: String): String =
    providers.gradleProperty(key).orNull ?: novaProps.getProperty(key, default)

android {
    namespace = "com.nova.engine"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "com.nova.engine"
        // Android 5+. GLES 2 on API 21-25; prefer GLES 3 on API 26+,
        // retrying GLES 2 when the GPU/driver cannot create an ES 3 context.
        minSdk = 21
        targetSdk = 34
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        versionCode = 5
        versionName = "0.3.0"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++20")
                arguments += listOf(
                    // GLES-only compatibility APK: linking Vulkan here would
                    // prevent Android 5/6 from loading the native engine.
                    "-DNOVA_BUILD_TESTS=OFF",
                    "-DNOVA_ENABLE_RHI_VULKAN=OFF",
                    "-DNOVA_ENABLE_RHI_GLES=ON",
                )
            }
        }

        ndk {
            abiFilters += novaProp("nova.abiFilters", "arm64-v8a,armeabi-v7a,x86,x86_64").split(",").map { it.trim() }
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = true
            isShrinkResources = true
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
            // Never ship with the validation layers enabled.
            buildConfigField("boolean", "NOVA_VALIDATION", "false")
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DCMAKE_BUILD_TYPE=Release")
                }
            }
        }
        debug {
            // Opt in/out with -Pnova.validation=false (see gradle.properties).
            buildConfigField(
                "boolean",
                "NOVA_VALIDATION",
                novaProp("nova.validation", "true"),
            )
            externalNativeBuild {
                cmake {
                    arguments += listOf("-DCMAKE_BUILD_TYPE=Debug")
                }
            }
        }
    }

    buildFeatures {
        buildConfig = true
    }

    externalNativeBuild {
        cmake {
            // The Nova Engine root CMakeLists.txt drives all native code.
            path = file("../CMakeLists.txt")
            version = "3.22.1"
        }
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    sourceSets {
        getByName("main") {
            // Shaders are compiled into src/main/assets/shaders/*.spv by the
            // compileShaders task below (or kept as the checked-in fallback).
        }
    }

    packaging {
        jniLibs {
            // Extract native libraries on old Android; avoid direct-mmap constraints.
            useLegacyPackaging = true
        }
    }
}

// ---------------------------------------------------------------------------
// Shader compilation: shaders/*.vert|*.frag -> app/src/main/assets/shaders/*.spv
//
// Uses glslangValidator or glslc found via the nova.glslang property or PATH.
// When no compiler is available the task is skipped and the checked-in .spv
// files are used, so a plain checkout builds out of the box (see
// tools/compile_shaders.sh for the CLI equivalent).
// ---------------------------------------------------------------------------
val shaderMode = novaProp("nova.compileShaders", "auto").lowercase()

val compileShaders by tasks.registering {
    group = "nova"
    description = "Compile GLSL shaders to SPIR-V into the app assets."

    val outDir = file("src/main/assets/shaders")
    outputs.dir(outDir)
    inputs.dir(rootProject.file("shaders"))

    onlyIf {
        if (shaderMode == "off") {
            logger.lifecycle("[nova] Shader compilation disabled (nova.compileShaders=off); using checked-in .spv")
            return@onlyIf false
        }
        when (val compiler = findShaderCompiler()) {
            null -> {
                if (shaderMode == "on") {
                    throw GradleException(
                        "nova.compileShaders=on but no glslangValidator/glslc was found. " +
                            "Install glslang or set -Pnova.glslang=/path/to/glslangValidator.",
                    )
                }
                logger.warn(
                    "[nova] No glslangValidator/glslc found; using checked-in .spv files. " +
                        "Set -Pnova.glslang=/path/to/glslangValidator to rebuild shaders.",
                )
                false
            }
            else -> {
                outDir.mkdirs()
                true
            }
        }
    }
    doLast {
        val compiler = findShaderCompiler()
            ?: throw IllegalStateException("shader compiler vanished between onlyIf and execution")
        val shaderDir = rootProject.file("shaders")
        val shaders = shaderDir.listFiles { f -> f.extension == "vert" || f.extension == "frag" }
            ?: emptyArray()
        if (shaders.isEmpty()) throw GradleException("No shaders found in $shaderDir")
        shaders.sortedBy { it.name }.forEach { src ->
            val dst = File(outDir, "${src.nameWithoutExtension}.spv")
            // Match the runtime: the engine requests Vulkan 1.3 devices, so
            // compile to the same target environment.
            val args = if (compiler.name.startsWith("glslc")) {
                listOf(
                    compiler.absolutePath, "--target-env=vulkan1.3",
                    "-O", "-o", dst.absolutePath, src.absolutePath,
                )
            } else {
                listOf(
                    compiler.absolutePath, "--target-env", "vulkan1.3",
                    "-V", "-o", dst.absolutePath, src.absolutePath,
                )
            }
            logger.lifecycle("[nova] ${src.name} -> ${dst.name}")
            exec {
                commandLine(args)
            }
        }
    }
}

tasks.named("preBuild") { dependsOn(compileShaders) }

fun findShaderCompiler(): File? {
    novaProps.getProperty("nova.glslang")?.let { p ->
        val f = File(p)
        if (f.canExecute()) return f
    }
    System.getenv("GLSLANG_VALIDATOR")?.let {
        val f = File(it); if (f.canExecute()) return f
    }
    for (name in listOf("glslangValidator", "glslang")) {
        for (dir in (System.getenv("PATH") ?: "").split(File.pathSeparator)) {
            val f = File(dir, name)
            if (f.isFile && f.canExecute()) return f
        }
    }
    return null
}

// Emulator integration tests exercise real EGL, JNI and the editor controls.
dependencies {
    androidTestImplementation("androidx.test:runner:1.5.2")
    androidTestImplementation("androidx.test.ext:junit:1.1.5")
    androidTestImplementation("androidx.test:core:1.5.0")
}
