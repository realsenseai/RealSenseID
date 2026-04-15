cmake_minimum_required(VERSION 3.14)

include(CSharpUtilities)

if(NOT RSID_SECURE)
    project(RealSenseID_CSharp_Sample LANGUAGES CSharp)

    set(CMAKE_CSharp_FLAGS "/platform:x64")

    set(EXE_NAME rsid-csharp-sample)
    add_executable(${EXE_NAME} App.config Program.cs)




    # designer files
    csharp_set_designer_cs_properties(Properties/AssemblyInfo.cs)

    # entry point
    set_property(SOURCE App.xaml PROPERTY VS_XAML_TYPE "ApplicationDefinition")

    # cs project flags
    set_property(TARGET ${EXE_NAME} PROPERTY VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.8")

    # references
    set_property(
        TARGET ${EXE_NAME}
        PROPERTY VS_DOTNET_REFERENCES
                 "Microsoft.CSharp"
                 "PresentationCore"
                 "PresentationFramework"
                 "PresentationFramework.Aero2"
                 "System"
                 "System.Core"
                 "System.Data"
                 "System.Data.DataSetExtensions"
                 "System.Net.Http"
                 "System.Xaml"
                 "System.Xml"
                 "System.Xml.Linq"
                 "System.Configuration"
                 "System.Management"
                 "WindowsBase")

    target_link_libraries(${EXE_NAME} PRIVATE rsid_dotnet)
    set_property(TARGET ${EXE_NAME} PROPERTY FOLDER "samples")
endif()