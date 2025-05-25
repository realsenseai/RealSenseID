FROM bellsoft/liberica-openjdk-debian:21 AS base

LABEL maintainer="RealSense-ID Team"

# Base environment
RUN set -eux; \
    apt-get -qq update \
    && apt-get -qqy --no-install-recommends install \
    apt-utils \
    zip \
    unzip \
    curl \
    lldb \
    swig \
    git \
    && rm -rf /var/lib/apt/lists/* /tmp/* /var/tmp/*

# Command line tools only
# https://developer.android.com/studio/index.html
ENV ANDROID_SDK_TOOLS_VERSION=13114758
ENV ANDROID_SDK_TOOLS_CHECKSUM=7ec965280a073311c339e571cd5de778b9975026cfcbe79f2b1cdcb1e15317ee

ENV ANDROID_HOME="/opt/android-sdk-linux"
ENV ANDROID_SDK_ROOT=$ANDROID_HOME
ENV PATH=$PATH:$ANDROID_HOME/cmdline-tools:$ANDROID_HOME/cmdline-tools/bin:$ANDROID_HOME/platform-tools

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=en_US.UTF-8

# Download and unzip Android SDK Tools
RUN set -eux; \
    curl -Ls https://dl.google.com/android/repository/commandlinetools-linux-${ANDROID_SDK_TOOLS_VERSION}_latest.zip > /tools.zip \
    && echo "$ANDROID_SDK_TOOLS_CHECKSUM /tools.zip" | sha256sum -c \
    && unzip -qq /tools.zip -d $ANDROID_HOME \
    && rm -v /tools.zip

# Accept licenses
RUN set -eux; \
    mkdir -p $ANDROID_HOME/licenses/ \
    && echo "8933bad161af4178b1185d1a37fbf41ea5269c55\nd56f5187479451eabf01fb78af6dfcb131a6481e\n24333f8a63b6825ea9c5514f83c2829b004d1fee" > $ANDROID_HOME/licenses/android-sdk-license \
    && echo "84831b9409646a918e30573bab4c9c91346d8abd\n504667f4c0de7af1a06de9f4b1727b84351f2910" > $ANDROID_HOME/licenses/android-sdk-preview-license --licenses \
    && yes | $ANDROID_HOME/cmdline-tools/bin/sdkmanager --licenses --sdk_root=${ANDROID_SDK_ROOT}

ENV HOME=/rsid-builder
WORKDIR $HOME

RUN set -eux; \
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} --update && \
    # $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} --list && \
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "ndk;27.0.12077973" &&\
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "build-tools;35.0.0" &&\
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "build-tools;34.0.0" &&\
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "platforms;android-25" &&\
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "platforms;android-35" &&\
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "platform-tools" &&\
    $ANDROID_HOME/cmdline-tools/bin/sdkmanager --sdk_root=${ANDROID_SDK_ROOT} "cmake;3.31.6"

## HOW TO BUILD
# docker build --platform=linux/amd64 -t rsid-builder:latest .
# docker run --rm -it -v.:/rsid-builder rsid-builder bash -c "cd wrappers/android && ./gradlew clean bundleStandardReleaseAar"
# Find your output in `wrappers/android/realsenseid/build/outputs/aar/`
