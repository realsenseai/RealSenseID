include(FetchContent REQUIRED)

message(STATUS "Populating Catch2")

FetchContent_Declare(
	Catch2
	URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.13.0.zip
	URL_HASH SHA256=b10a1f4930f576a0dd8fa37e86a14309dbb766944d7776c0a38472e5760f0d70
	EXCLUDE_FROM_ALL
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(Catch2)

# Hide all Catch2 targets in Visual Studio
set_target_properties(Catch2 PROPERTIES FOLDER "_deps/Catch2")
set_target_properties(Catch2WithMain PROPERTIES FOLDER "_deps/Catch2")