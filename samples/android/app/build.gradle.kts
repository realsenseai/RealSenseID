import java.io.FileInputStream
import java.net.HttpURLConnection
import java.net.URI
import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import java.util.Base64
import java.util.Properties

plugins {
  alias(libs.plugins.android.application)
  id("idea")
}

val gitExecResult = providers.exec {
  commandLine("git", "rev-parse", "--short", "HEAD")
  isIgnoreExitValue = true
}

val gitSha: Provider<String> = gitExecResult.result.zip(gitExecResult.standardOutput.asText) { result, output ->
  if (result.exitValue == 0) output.trim() else "unknown"
}

android {
  namespace = "com.realsenseai.rsid.sample"
  compileSdk = 36

  defaultConfig {
    applicationId = "com.realsenseai.rsid.sample"
    minSdk = 26
    targetSdk = 29
    versionCode = 1
    versionName = "1.0"

    testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"

    // Add git SHA as a build config field
    buildConfigField("String", "GIT_SHA", gitSha.map { "\"$it\"" }.get())
  }

  buildTypes {
    release {
      isMinifyEnabled = false
      proguardFiles(
        getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
    }
  }
  compileOptions {
    sourceCompatibility = JavaVersion.VERSION_17
    targetCompatibility = JavaVersion.VERSION_17
  }
  buildFeatures {
    viewBinding = true
    buildConfig = true
  }

  // TODO: Re-enable lint and fix the underlying issues:
  //   - layout-land/fragment_preview.xml + layout/fragment_preview.xml: 4x NotSibling
  //     (@+id/video_overlay is not a sibling in the same ConstraintLayout)
  //   - targetSdk = 29 fails ExpiredTargetSdkVersion (Google Play requires 33+)
  // For now, skip lintVitalRelease so assembleRelease doesn't block CI on these.
  lint {
    checkReleaseBuilds = false
    abortOnError = false
  }
}


val props = Properties()
val localPropertiesFile: File = rootProject.file("local.properties")

if (localPropertiesFile.exists()) {
  FileInputStream(localPropertiesFile).use { input ->
    props.load(input)
  }
}

val firmwareProperties = Properties()
val firmwarePropertiesFile: File = rootProject.file("firmware.properties")
if (firmwarePropertiesFile.exists()) {
  firmwarePropertiesFile.inputStream().use { inputStream ->
    firmwareProperties.load(inputStream)
  }
}
else {
  throw GradleException("firmware.properties file is missing. Please create it in the project root.")
}

val rsidOssBuild: Boolean = props.getProperty("rsid.oss")?.toBoolean() ?: false

dependencies {
  implementation(libs.appcompat)
  implementation(libs.appcompatres)
  implementation(libs.material)
  implementation(libs.recyclerview)
  implementation(libs.constraintlayout)
  implementation(libs.lifecycle.livedata.ktx)
  implementation(libs.lifecycle.viewmodel.ktx)
  implementation(libs.navigation.fragment)
  implementation(libs.navigation.ui)
  implementation(libs.permissionx)
  implementation(libs.timber)
  implementation(libs.room)
  annotationProcessor(libs.roomCompiler)
  implementation(libs.gson)
  implementation(libs.okhttp)
  implementation(libs.opencv)
  implementation(fileTree(mapOf("dir" to "libs", "include" to listOf("*.aar"))))

  //testImplementation(libs.junit)
  //androidTestImplementation(libs.ext.junit)
  //androidTestImplementation(libs.espresso.core)
}


abstract class DownloadFirmwareFiles : DefaultTask() {

  @get:Input
  abstract val firmwareUrls: ListProperty<String>

  @get:Input
  abstract val firmwareRawNames: ListProperty<String>

  @get:Input
  abstract val firmwareShas: ListProperty<String>

  @get:Input
  abstract val ossBuild: Property<Boolean>

  @get:OutputDirectory
  abstract val rawFilesDir: DirectoryProperty

  @get:Input
  abstract val localProperties: MapProperty<String, String>

  private fun calculateSHA256(file: File): String {
    val digest = MessageDigest.getInstance("SHA-256")
    file.inputStream().use { input ->
      val buffer = ByteArray(8192)
      var read: Int
      while (input.read(buffer).also { read = it } > 0) {
        digest.update(buffer, 0, read)
      }
    }
    return digest.digest().joinToString("") { "%02x".format(it) }
  }

  private fun downloadAndGetFile(url: String, dest: File): File {
    require(url.startsWith("http://") || url.startsWith("https://")) {
      "URL must use HTTP or HTTPS protocol: $url"
    }

    if (!dest.exists()) {
      val uri = URI(url)
      var connection: HttpURLConnection
      var actualUrl = url
      val propsMap = localProperties.get()

      try {
        logger.lifecycle("Downloading from $url to $dest")

        val isGitHub = uri.host.equals("github.com", ignoreCase = true) ||
                       uri.host.equals("api.github.com", ignoreCase = true)

        if (isGitHub) {
          val githubToken = System.getenv("GITHUB_TOKEN")
                            ?: propsMap["GITHUB_TOKEN"]
                            ?: throw GradleException(
                              "GITHUB_TOKEN is required for downloading from GitHub. Set it in environment variables or local.properties.")

          logger.lifecycle("Using GITHUB_TOKEN for GitHub authentication")

          actualUrl = resolveGitHubAssetUrl(url, githubToken)

          connection = URI(actualUrl).toURL().openConnection() as HttpURLConnection
          connection.instanceFollowRedirects = false
          connection.connectTimeout = 30_000
          connection.readTimeout = 60_000
          connection.setRequestProperty("User-Agent", "Gradle Firmware Downloader/1.0")
          connection.setRequestProperty("Authorization", "token $githubToken")
          connection.setRequestProperty("Accept", "application/octet-stream")
          connection.connect()

          var responseCode = connection.responseCode
          while (responseCode == HttpURLConnection.HTTP_MOVED_TEMP ||
                 responseCode == HttpURLConnection.HTTP_MOVED_PERM ||
                 responseCode == 307 || responseCode == 308) {

            val redirectUrl = connection.getHeaderField("Location")
            connection.disconnect()

            if (redirectUrl.isNullOrEmpty()) {
              throw GradleException("Received redirect but no Location header")
            }

            logger.lifecycle("Following redirect...")
            connection = URI(redirectUrl).toURL().openConnection() as HttpURLConnection
            connection.instanceFollowRedirects = false
            connection.connectTimeout = 30_000
            connection.readTimeout = 60_000
            connection.setRequestProperty("User-Agent", "Gradle Firmware Downloader/1.0")
            connection.connect()
            responseCode = connection.responseCode
          }
        }
        else {
          connection = uri.toURL().openConnection() as HttpURLConnection
          connection.instanceFollowRedirects = true
          connection.connectTimeout = 30_000
          connection.readTimeout = 60_000
          connection.setRequestProperty("User-Agent", "Gradle Firmware Downloader/1.0")

          val artifactoryUsername = System.getenv("artifactoryUsername")
                                    ?: propsMap["artifactoryUsername"]
          val artifactoryPassword = System.getenv("artifactoryPassword")
                                    ?: propsMap["artifactoryPassword"]

          if (!artifactoryUsername.isNullOrEmpty() && !artifactoryPassword.isNullOrEmpty()) {
            val userpass = "$artifactoryUsername:$artifactoryPassword"
            val encodedUserpass = Base64.getEncoder()
              .encodeToString(userpass.toByteArray(StandardCharsets.UTF_8))
            connection.setRequestProperty("Authorization", "Basic $encodedUserpass")
            logger.lifecycle("Using artifactory credentials for authentication")
          }
          else {
            logger.lifecycle("No authentication credentials provided, attempting unauthenticated download")
          }

          connection.connect()
        }

        if (connection.responseCode != HttpURLConnection.HTTP_OK) {
          throw GradleException(
            "Failed to download file. HTTP ${connection.responseCode}: ${connection.responseMessage}\n  URL: $actualUrl")
        }

        connection.inputStream.use { input ->
          dest.outputStream().use { output ->
            input.copyTo(output)
          }
        }

        logger.lifecycle("Successfully downloaded: ${dest.name} (${dest.length()} bytes)")
      }
      catch (e: GradleException) {
        throw e
      }
      catch (e: Exception) {
        throw GradleException("Failed to download file from $url: ${e.message}", e)
      }
    }
    else {
      logger.debug("Using cached file: {}", dest)
    }
    return dest
  }

  private fun resolveGitHubAssetUrl(url: String, token: String): String {
    val uri = URI(url)

    if (uri.host.equals("api.github.com", ignoreCase = true) && uri.path.contains("/releases/assets/")) {
      return url
    }

    val pathPattern = Regex("^/([^/]+)/([^/]+)/releases/download/([^/]+)/(.+)$")
    val match = pathPattern.find(uri.path)
                ?: throw GradleException(
                  "Invalid GitHub release URL format: $url\nExpected: https://github.com/OWNER/REPO/releases/download/TAG/FILENAME")

    val (owner, repo, tag, filename) = match.destructured

    logger.lifecycle("Looking up asset ID for '$filename' in release '$tag'...")

    val releaseApiUrl = "https://api.github.com/repos/$owner/$repo/releases/tags/$tag"
    val releaseConnection = URI(releaseApiUrl).toURL().openConnection() as HttpURLConnection

    try {
      releaseConnection.connectTimeout = 30_000
      releaseConnection.readTimeout = 30_000
      releaseConnection.setRequestProperty("User-Agent", "Gradle Firmware Downloader/1.0")
      releaseConnection.setRequestProperty("Authorization", "token $token")
      releaseConnection.setRequestProperty("Accept", "application/vnd.github+json")
      releaseConnection.connect()

      if (releaseConnection.responseCode != HttpURLConnection.HTTP_OK) {
        throw GradleException(
          "Failed to fetch release info. HTTP ${releaseConnection.responseCode}: ${releaseConnection.responseMessage}\n  URL: $releaseApiUrl")
      }

      val responseBody = releaseConnection.inputStream.bufferedReader().readText()

      val jsonSlurper = groovy.json.JsonSlurper()
      val release = jsonSlurper.parseText(responseBody) as Map<*, *>
      val assets = release["assets"] as? List<*>
                   ?: throw GradleException("No assets found in release '$tag'")

      val asset = assets.filterIsInstance<Map<*, *>>()
                    .find { it["name"] == filename }
                  ?: throw GradleException(
                    "Asset '$filename' not found in release '$tag'\n  Available assets: ${assets.filterIsInstance<Map<*, *>>().mapNotNull { it["name"] }}")

      val assetId = asset["id"] ?: throw GradleException("Asset ID not found for '$filename'")

      val assetApiUrl = "https://api.github.com/repos/$owner/$repo/releases/assets/$assetId"
      logger.lifecycle("Found asset ID: $assetId")

      return assetApiUrl
    }
    finally {
      releaseConnection.disconnect()
    }
  }

  @TaskAction
  fun execute() {
    val dir = rawFilesDir.get().asFile
    val urls = firmwareUrls.get()
    val names = firmwareRawNames.get()
    val shas = firmwareShas.get()

    val firmwareEntries = urls.zip(names).zip(shas) { (url, name), sha -> Triple(url, name, sha) }

    if (ossBuild.get()) {
      firmwareEntries.forEach { (_, targetName, expectedSha) ->
        val file = File(dir, targetName)
        if (!file.exists()) {
          throw GradleException("ERROR: OSS Build is enabled, but required firmware file '$targetName' is missing.")
        }
        val actualSha = calculateSHA256(file)
        if (actualSha != expectedSha) {
          throw GradleException("SHA-256 mismatch for '$targetName'. Expected $expectedSha but got $actualSha.")
        }
      }
      return
    }

    val filesToDownload = mutableListOf<Triple<String, String, String>>()
    firmwareEntries.forEach { (url, targetName, expectedSha) ->
      val file = File(dir, targetName)
      if (!file.exists()) {
        filesToDownload.add(Triple(url, targetName, expectedSha))
      }
      else {
        val actualSha = calculateSHA256(file)
        if (actualSha != expectedSha) {
          logger.lifecycle("SHA-256 mismatch for existing file '$targetName'. Redownloading...")
          filesToDownload.add(Triple(url, targetName, expectedSha))
        }
        else {
          logger.lifecycle("File '$targetName' is up-to-date.")
        }
      }
    }

    if (filesToDownload.isNotEmpty()) {
      logger.lifecycle("Downloading required firmware files...")

      filesToDownload.forEach { (url, targetName, _) ->
        downloadAndGetFile(url, File(dir, targetName))
      }

      val checkSumErrors = mutableListOf<String>()
      filesToDownload.forEach { (url, targetName, expectedSha) ->
        val file = downloadAndGetFile(url, File(dir, targetName))
        val actualSha = calculateSHA256(file)
        if (actualSha != expectedSha) {
          checkSumErrors.add("'$targetName': expected $expectedSha - got $actualSha")
        }
      }
      if (checkSumErrors.isNotEmpty()) {
        throw GradleException("Failed SHA-256 verification.\n" + " ${checkSumErrors.joinToString("\n ")}\n" +
                              "Please move/delete the file and rebuild to download the correct version.")
      }
    }
  }
}

val firmwareDownloadMap = mutableListOf<Triple<String, String, String>>()
firmwareProperties.stringPropertyNames().filter { it.startsWith("firmware.") && it.endsWith(".url") }.forEach { key ->
  val prefix = key.substringBeforeLast(".url")
  val url = firmwareProperties.getProperty(key)
  val rawName = firmwareProperties.getProperty("$prefix.raw_name") ?: throw GradleException("Missing raw_name for $prefix")
  val sha = firmwareProperties.getProperty("$prefix.sha") ?: throw GradleException("Missing sha for $prefix")
  firmwareDownloadMap.add(Triple(url, rawName, sha))
}

tasks.register<DownloadFirmwareFiles>("downloadFirmwareFiles") {
  firmwareUrls.set(firmwareDownloadMap.map { it.first })
  firmwareRawNames.set(firmwareDownloadMap.map { it.second })
  firmwareShas.set(firmwareDownloadMap.map { it.third })
  ossBuild.set(rsidOssBuild)
  rawFilesDir.set(layout.projectDirectory.dir("src/main/res/raw"))
  localProperties.set(
    props.stringPropertyNames().associateWith { props.getProperty(it) }
  )
}

tasks.matching {
  it.name == "preBuild" || it.name.startsWith("compile") || it.name.startsWith("merge")
}.configureEach {
  dependsOn("downloadFirmwareFiles")
}

idea {
  module {
    isDownloadSources = false
    isDownloadJavadoc = true
  }
}
