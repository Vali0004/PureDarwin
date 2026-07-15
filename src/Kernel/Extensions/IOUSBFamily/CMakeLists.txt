add_kext_bundle(IOUSBFamily KERNEL_PRIVATE INFO_PLIST Info.plist
    BUNDLE_IDENTIFIER org.puredarwin.iokit.IOUSBFamily BUNDLE_VERSION 1.0.0)

target_sources(IOUSBFamily PRIVATE
    IOUSBFamily/IOUSBBus.cpp
    IOUSBFamily/IOUSBNub.cpp
    IOUSBFamily/IOUSBController.cpp
    IOUSBFamily/IOUSBDevice.cpp
    IOUSBFamily/IOUSBInterface.cpp
    IOUSBFamily/IOUSBPipe.cpp
)

set_property(TARGET IOUSBFamily PROPERTY CXX_STANDARD 11)
target_compile_options(IOUSBFamily PRIVATE
    -Wno-implicit-int-conversion -Wno-inconsistent-missing-override
    -Wno-sign-conversion -Wno-shadow -Wno-unused-parameter)
target_link_libraries(IOUSBFamily PRIVATE IOUSBFamilyHeaders)
install(TARGETS IOUSBFamily DESTINATION System/Library/Extensions COMPONENT BaseSystem)

add_library(IOUSBFamilyHeaders INTERFACE)
target_include_directories(IOUSBFamilyHeaders INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/IOKit/usb
)

add_subdirectory(IOUSBCompositeDriver)
add_subdirectory(AppleUSBMergeNub)
