set(source main.cpp)

include_directories(${CMAKE_SOURCE_DIR})

set(EXECUTABLE_OUTPUT_PATH ${PA_BINARY_DIR})
    link_directories(${PA_BINARY_DIR} / lib)
        add_executable(analyzer ${source})
            target_link_libraries(analyzer LLVMAsmParser LLVMSupport
                                        LLVMCore LLVMAnalysis LLVMIRReader)
