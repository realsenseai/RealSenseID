cmake_minimum_required(VERSION 3.14)

include(CSharpUtilities)

project(RealSenseID_Viewer LANGUAGES CSharp)

set(CMAKE_CSharp_FLAGS "/platform:x64")

set(EXE_NAME rsid-viewer)
set(ASSEMBLY_INFO_FILE "${CMAKE_CURRENT_BINARY_DIR}/Properties/AssemblyInfo.cs")
configure_file(Properties/AssemblyInfo.cs.in "${ASSEMBLY_INFO_FILE}")

add_executable(
    ${EXE_NAME}
    App.config
    App.xaml
    App.xaml.cs    
    Dictionary1.xaml
    ErrorDialog.xaml
    ErrorDialog.xaml.cs
	PowerDialog.xaml
    PowerDialog.xaml.cs
	UpdateAvailableDialog.xaml
	UpdateAvailableDialog.xaml.cs
    OKCancelDialog.xaml
    OKCancelDialog.xaml.cs
    ProgressBarDialog.xaml
    ProgressBarDialog.xaml.cs
    MainWindow.xaml
    MainWindow.xaml.cs
    EnrollInput.xaml
    EnrollInput.xaml.cs
    EnrollInstructions.xaml
    EnrollInstructions.xaml.cs
    DeleteUserInput.xaml
    DeleteUserInput.xaml.cs
    FwUpdateInput.xaml
    FwUpdateInput.xaml.cs
    Utilities.cs
    Database.cs
    DatabaseSerializer.cs
    FrameDumper.cs
    AuthSettingsInput.xaml
    AuthSettingsInput.xaml.cs
    ModeToVisibilityConverter.cs
	"${ASSEMBLY_INFO_FILE}"
    Properties/Resources.Designer.cs
    Properties/Resources.resx
    Properties/Settings.Designer.cs
    Properties/Settings.settings
    "FONT AWESOME 5 FREE-REGULAR-400.OTF"
    "FONT AWESOME 5 FREE-SOLID-900.OTF"
    logo.png
    icon.ico    
	sn.txt)

# add fonts as content
set_property(SOURCE "FONT AWESOME 5 FREE-REGULAR-400.OTF" PROPERTY VS_TOOL_OVERRIDE "Content")
set_property(SOURCE "FONT AWESOME 5 FREE-REGULAR-400.OTF" PROPERTY VS_COPY_TO_OUT_DIR "PreserveNewest")
set_property(SOURCE "FONT AWESOME 5 FREE-SOLID-900.OTF" PROPERTY VS_TOOL_OVERRIDE "Content")
set_property(SOURCE "FONT AWESOME 5 FREE-SOLID-900.OTF" PROPERTY VS_COPY_TO_OUT_DIR "PreserveNewest")
# add empty "sn.txt" as content
set_property(SOURCE "sn.txt" PROPERTY VS_TOOL_OVERRIDE "Content")
set_property(SOURCE "sn.txt" PROPERTY VS_COPY_TO_OUT_DIR "PreserveNewest")

# add logo as content
set_property(SOURCE "logo.png" PROPERTY VS_TOOL_OVERRIDE "Content")
set_property(SOURCE "logo.png" PROPERTY VS_COPY_TO_OUT_DIR "PreserveNewest")





if(RSID_SECURE)
    add_definitions(-DRSID_SECURE)
endif()

if(RSID_PREVIEW)
    add_definitions(-DRSID_PREVIEW)
endif()

if(RSID_ONE2ONE)
    add_definitions(-DRSID_ONE2ONE)
endif()

# designer files
csharp_set_designer_cs_properties(
    "${ASSEMBLY_INFO_FILE}"
    Properties/Resources.Designer.cs
    Properties/Resources.resx
    Properties/Settings.Designer.cs
    Properties/Settings.settings)

csharp_set_xaml_cs_properties(
    App.xaml
    App.xaml.cs    
    Dictionary1.xaml
    ErrorDialog.xaml
    ErrorDialog.xaml.cs
    ProgressBarDialog.xaml
    ProgressBarDialog.xaml.cs
    MainWindow.xaml
    MainWindow.xaml.cs
    EnrollInput.xaml
    EnrollInput.xaml.cs
    FwUpdateInput.xaml
    FwUpdateInput.xaml.cs
    EnrollInstructions.xaml
    EnrollInstructions.xaml.cs
    AuthSettingsInput.xaml
    AuthSettingsInput.xaml.cs
    DeleteUserInput.xaml
    DeleteUserInput.xaml.cs
	UpdateAvailableDialog.xaml
	UpdateAvailableDialog.xaml.cs
	)

# entry point
set_property(SOURCE App.xaml PROPERTY VS_XAML_TYPE "ApplicationDefinition")

# cs project flags
set_property(TARGET ${EXE_NAME} PROPERTY VS_DOTNET_TARGET_FRAMEWORK_VERSION "v4.8")
set_property(TARGET ${EXE_NAME} PROPERTY WIN32_EXECUTABLE TRUE)
set_property(TARGET ${EXE_NAME} PROPERTY VS_GLOBAL_ApplicationIcon ${CMAKE_CURRENT_SOURCE_DIR}/icon.ico)

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
             "System.Drawing"
             "WindowsBase"
             "System.Web.Extensions")

target_link_libraries(${EXE_NAME} PRIVATE rsid_dotnet)
if(RSID_SECURE)
    target_link_libraries(${EXE_NAME} PRIVATE rsid_secure_helper)
endif()

set_property(TARGET ${EXE_NAME} PROPERTY FOLDER "tools")
