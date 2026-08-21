import java.util.Properties

fun escapeForBuildConfig(raw: String): String {
    return raw
        .replace("\\", "\\\\")
        .replace("\"", "\\\"")
}

fun loadProjectProperties(
    projectDir: File,
    primaryName: String,
    fallbackName: String? = null,
): Properties {
    val props = Properties()
    val sourceFile = listOfNotNull(primaryName, fallbackName)
        .asSequence()
        .map(projectDir::resolve)
        .firstOrNull(File::exists)

    if (sourceFile != null) {
        sourceFile.inputStream().use(props::load)
    }

    return props
}

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}

val oneNetProps = loadProjectProperties(
    projectDir = rootProject.projectDir,
    primaryName = "onenet.debug.properties",
    fallbackName = "onenet.debug.properties.template",
)
val releaseSigningProps = loadProjectProperties(
    projectDir = rootProject.projectDir,
    primaryName = "release-signing.properties",
    fallbackName = "release-signing.properties.template",
)

fun projectProp(
    props: Properties,
    name: String,
    fallback: String = "",
): String {
    return props.getProperty(name)?.trim().orEmpty().ifBlank { fallback }
}

fun oneNetProp(name: String, fallback: String): String = projectProp(oneNetProps, name, fallback)

val hasReleaseSigning = listOf("storeFile", "storePassword", "keyAlias", "keyPassword")
    .all { key -> releaseSigningProps.getProperty(key)?.trim().orEmpty().isNotBlank() }

val requestsReleaseBuild = gradle.startParameter.taskNames.any { taskName ->
    taskName.contains("release", ignoreCase = true)
}

if (requestsReleaseBuild && !hasReleaseSigning) {
    error("Missing release signing config in release-signing.properties")
}

android {
    namespace = "com.stm32.envmonitor"
    compileSdk = 36

    defaultConfig {
        applicationId = "com.stm32.envmonitor"
        minSdk = 26
        targetSdk = 36
        versionCode = 1
        versionName = "1.0.0"

        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        vectorDrawables {
            useSupportLibrary = true
        }

        buildConfigField("String", "ONENET_BASE_URL", "\"${escapeForBuildConfig(oneNetProp("baseUrl", "https://iot-api.heclouds.com"))}\"")
        buildConfigField("String", "ONENET_PRODUCT_ID", "\"${escapeForBuildConfig(oneNetProp("productId", ""))}\"")
        buildConfigField("String", "ONENET_DEVICE_NAME", "\"${escapeForBuildConfig(oneNetProp("deviceName", ""))}\"")
        buildConfigField("String", "ONENET_AUTHORIZATION", "\"${escapeForBuildConfig(oneNetProp("authorization", ""))}\"")
        buildConfigField("long", "ONENET_REFRESH_INTERVAL_MS", "${oneNetProp("refreshIntervalMs", "3000")}L")
        buildConfigField("long", "ONENET_STALE_DATA_MS", "${oneNetProp("staleDataMs", "12000")}L")
        buildConfigField("int", "ONENET_HISTORY_HOURS", oneNetProp("historyHours", "6"))
        buildConfigField("int", "ONENET_HISTORY_LIMIT", oneNetProp("historyLimit", "60"))
    }

    signingConfigs {
        if (hasReleaseSigning) {
            create("release") {
                storeFile = rootProject.file(projectProp(releaseSigningProps, "storeFile"))
                storePassword = projectProp(releaseSigningProps, "storePassword")
                keyAlias = projectProp(releaseSigningProps, "keyAlias")
                keyPassword = projectProp(releaseSigningProps, "keyPassword")

                val signingStoreType = projectProp(releaseSigningProps, "storeType")
                if (signingStoreType.isNotBlank()) {
                    storeType = signingStoreType
                }
            }
        }
    }

    buildTypes {
        debug {
            applicationIdSuffix = ".debug"
            versionNameSuffix = "-debug"
        }
        release {
            if (hasReleaseSigning) {
                signingConfig = signingConfigs.getByName("release")
            }
            isMinifyEnabled = false
            proguardFiles(
                getDefaultProguardFile("proguard-android-optimize.txt"),
                "proguard-rules.pro",
            )
        }
    }

    buildFeatures {
        compose = true
        buildConfig = true
    }

    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }

    kotlinOptions {
        jvmTarget = "17"
    }

    packaging {
        resources {
            excludes += "/META-INF/{AL2.0,LGPL2.1}"
        }
    }

    testOptions {
        unitTests.isIncludeAndroidResources = true
    }
}

dependencies {
    val composeBom = platform("androidx.compose:compose-bom:2024.10.01")

    implementation(composeBom)
    androidTestImplementation(composeBom)

    implementation("androidx.core:core-ktx:1.15.0")
    implementation("androidx.lifecycle:lifecycle-runtime-ktx:2.8.7")
    implementation("androidx.lifecycle:lifecycle-runtime-compose:2.8.7")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation("androidx.navigation:navigation-compose:2.8.3")
    implementation("androidx.compose.ui:ui")
    implementation("androidx.compose.ui:ui-graphics")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.foundation:foundation")
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.material:material-icons-extended")
    implementation("com.google.android.material:material:1.12.0")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.9.0")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.7.3")
    implementation("com.squareup.okhttp3:okhttp:4.12.0")

    testImplementation("junit:junit:4.13.2")
    testImplementation("androidx.test:core:1.6.1")
    testImplementation("androidx.arch.core:core-testing:2.2.0")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.9.0")
    testImplementation("com.google.truth:truth:1.4.4")
    testImplementation("com.squareup.okhttp3:mockwebserver:4.12.0")

    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test.espresso:espresso-core:3.6.1")
    androidTestImplementation("androidx.test:runner:1.6.2")

    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
