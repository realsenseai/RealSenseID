pluginManagement {
    repositories {
        google {
            content {
                includeGroupByRegex("com\\.android.*")
                includeGroupByRegex("com\\.google.*")
                includeGroupByRegex("androidx.*")
            }
        }
        mavenCentral()
        gradlePluginPortal()
    }
}

plugins {
    id("org.gradle.toolchains.foojay-resolver-convention") version "1.0.0"
}

dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
        maven {
            setUrl("https://jitpack.io")
        }
    }
}

rootProject.name = "RealSenseID Sample"
include(":app")

// Auto-update gradle.properties when RealSenseID_release.aar changes.
// Skip on CI to avoid dirtying the working tree.
if (System.getenv("CI") == null) {
    val aarFile = file("app/libs/RealSenseID_release.aar")
    val propsFile = file("gradle.properties")
    val currentFingerprint = if (aarFile.exists()) "${aarFile.length()}:${aarFile.lastModified()}" else "missing"

    if (propsFile.exists()) {
        val lines = propsFile.readLines().toMutableList()
        val key = "rsid.aar.fingerprint"
        val newLine = "$key=$currentFingerprint"
        val idx = lines.indexOfFirst { it.startsWith("$key=") }
        if (idx >= 0) {
            if (lines[idx] != newLine) {
                lines[idx] = newLine
                propsFile.writeText(lines.joinToString("\n") + "\n")
            }
        } else {
            propsFile.appendText("$newLine\n")
        }
    }
}
 