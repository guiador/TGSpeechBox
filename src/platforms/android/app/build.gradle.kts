import java.util.Properties

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

val sigProps = Properties()
val sigFile = rootProject.file("signing.properties")
if (sigFile.exists()) sigProps.load(sigFile.inputStream())

android {
    namespace = "com.tgspeechbox.tts"
    compileSdk = 35

    ndkVersion = "27.2.12479018"

    signingConfigs {
        create("release") {
            storeFile = file(sigProps.getProperty("STORE_FILE", ""))
            storePassword = sigProps.getProperty("STORE_PASSWORD", "")
            keyAlias = sigProps.getProperty("KEY_ALIAS", "")
            keyPassword = sigProps.getProperty("KEY_PASSWORD", "")
        }
    }

    defaultConfig {
        applicationId = "com.tgspeechbox.tts"
        minSdk = 26
        targetSdk = 35
        versionCode = 299
        versionName = "2.99"

        externalNativeBuild {
            cmake {
                cppFlags += listOf("-std=c++17", "-fvisibility=hidden")
                arguments += listOf(
                    "-DCMAKE_BUILD_TYPE=MinSizeRel",
                    "-DESPEAK_NG_DIR=${rootProject.projectDir}/../../../../espeak-ng",
                    "-DTGSB_DIR=${rootProject.projectDir}/../../.."
                )
            }
        }

        ndk {
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../jni/CMakeLists.txt")
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("release")
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
            kotlin.srcDirs("src/main/kotlin")
        }
    }
}

// ── Build-time asset copy ────────────────────────────────────
// Single canonical data lives at repo root:
//   resources/espeak-ng-data/   (eSpeak dictionary + voice data)
//   packs/                      (language packs + phonemes.yaml)
// Copy into the build's asset tree so the APK is self-contained.
val repoRoot = rootProject.projectDir.resolve("../../..")
val generatedAssets = layout.buildDirectory.dir("generated/assets/main")

val copyEspeakData by tasks.registering(Copy::class) {
    from(repoRoot.resolve("resources/espeak-ng-data"))
    into(generatedAssets.map { it.dir("espeak-ng-data") })
}

val copyPacks by tasks.registering(Copy::class) {
    from(repoRoot.resolve("packs"))
    into(generatedAssets.map { it.dir("tgsb/packs") })
}

android.sourceSets.getByName("main") {
    assets.srcDir(generatedAssets)
}

tasks.named("preBuild") {
    dependsOn(copyEspeakData, copyPacks)
}

dependencies {
    implementation("androidx.core:core-ktx:1.13.1")
}
