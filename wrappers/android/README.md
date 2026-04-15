# RealSense™ ID Android wrapper

For your convenience, we offer a Java API to be used for Android application development.
If, for any reason, you prefer not to use it, you can take the regular C or C++ API and wrap
it with your own JNI code and Java/Kotlin classes.

RealSense™ ID Android wrapper is making use of [SWIG (Simplified Wrapper and Interface Generator)](https://en.wikipedia.org/wiki/SWIG)
to wrap the C++ API and offer an easy to use, simple to extend Java API for developing an application
that interacts with the F45# camera module. This way, not only the C++ API is translated to Java,
but also the Doxygen Documentation that goes with it is transferred.

The code generation is automatically triggered prior to compilation, by [src/main/CMakeLists.txt](src/main/CMakeLists.txt).
JNI code will be generated to src/main/cpp_gen and Java code will be in src/main/java/com/realsenseai/rsid/api.
In src/main/java/com/realsenseai/rsid/impl we manually define helper classes that are unique for
Android and are not simply a translation of the API between the programing languages.

The instructions for [settings.gradle](settings.gradle)SWIG about which API to wrap and how to do it is found
in [swig.i.in](src/main/swig.i.in).

In order to avoid rewriting of the complicated implementation of signature handling found in the
c signature example, a separate interface file named [signature_example_wrapper.i.in](src/main/signature_example_wrapper.i.in)
was created to wrap that code as well.

`NOTE: signature_example_wrapper.i.in SHOULD NOT be used in production for security reasons, since signature example C code was not meant to be used in production.`

## Build Instructions

```shell
./gradlew clean bundleStandardReleaseAar
```
