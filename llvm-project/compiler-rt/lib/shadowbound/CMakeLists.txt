include_directories(..)

set(SHADOWBOUND_RTL_SOURCES
  shadowbound.cpp
  shadowbound_allocator.cpp
  shadowbound_thread.cpp
  shadowbound_interceptors.cpp
  shadowbound_linux.cpp
  )

set(SHADOWBOUND_RTL_CXX_SOURCES
  shadowbound_new_delete.cpp
  )

set(SHADOWBOUND_RTL_HEADERS
  shadowbound.h
  shadowbound_allocator.h
  shadowbound_interceptors.h
  shadowbound_interface_internal.h
  shadowbound_thread.h
  )

set(SHADOWBOUND_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF SHADOWBOUND_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE SHADOWBOUND_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding SHADOWBOUND_RTL_CFLAGS)

set(SHADOWBOUND_RUNTIME_LIBRARIES)

add_compiler_rt_component(shadowbound)

foreach(arch ${SHADOWBOUND_SUPPORTED_ARCH})
	add_compiler_rt_runtime(clang_rt.shadowbound
		STATIC
		ARCHS ${arch}
		SOURCES ${SHADOWBOUND_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
    ADDITIONAL_HEADERS ${SHADOWBOUND_RTL_HEADERS}
		CFLAGS ${SHADOWBOUND_RTL_CFLAGS}
		PARENT_TARGET shadowbound)
  add_compiler_rt_runtime(clang_rt.shadowbound_cxx
    STATIC
		ARCHS ${arch}
    SOURCES ${SHADOWBOUND_RTL_CXX_SOURCES}
    ADDITIONAL_HEADERS ${SHADOWBOUND_RTL_HEADERS}
    CFLAGS ${SHADOWBOUND_RTL_CFLAGS}
    PARENT_TARGET shadowbound)
  list(APPEND SHADOWBOUND_RUNTIME_LIBRARIES clang_rt.shadowbound-${arch}
                                     clang_rt.shadowbound_cxx-${arch})
  if(SANITIZER_USE_SYMBOLS)
    add_sanitizer_rt_symbols(clang_rt.shadowbound
      ARCHS ${arch}
      EXTRA shadowbound.syms.extra)
    add_sanitizer_rt_symbols(clang_rt.shadowbound_cxx
      ARCHS ${arch}
      EXTRA shadowbound.syms.extra)
    add_dependencies(shadowbound clang_rt.shadowbound-${arch}-symbols
                          clang_rt.shadowbound_cxx-${arch}-symbols)
  endif()
endforeach()
