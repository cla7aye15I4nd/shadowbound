include_directories(..)

set(SHADOWBOUND_RTL_SOURCES
  memp.cpp
  )

set(SHADOWBOUND_RTL_CXX_SOURCES
  )

set(SHADOWBOUND_RTL_HEADERS
  memp_interface_internal.h
  )

set(SHADOWBOUND_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF SHADOWBOUND_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE SHADOWBOUND_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding SHADOWBOUND_RTL_CFLAGS)

set(SHADOWBOUND_RUNTIME_LIBRARIES)

add_compiler_rt_component(memp)

foreach(arch ${SHADOWBOUND_SUPPORTED_ARCH})
	add_compiler_rt_runtime(clang_rt.memp
		STATIC
		ARCHS ${arch}
		SOURCES ${SHADOWBOUND_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
    ADDITIONAL_HEADERS ${SHADOWBOUND_RTL_HEADERS}
		CFLAGS ${SHADOWBOUND_RTL_CFLAGS}
		PARENT_TARGET memp)
  list(APPEND SHADOWBOUND_RUNTIME_LIBRARIES clang_rt.memp-${arch})
  if(SANITIZER_USE_SYMBOLS)
    add_sanitizer_rt_symbols(clang_rt.memp
      ARCHS ${arch}
      EXTRA memp.syms.extra)
    add_dependencies(memp clang_rt.memp-${arch}-symbols)
  endif()
endforeach()
