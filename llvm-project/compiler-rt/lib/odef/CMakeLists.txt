include_directories(..)

set(ODEF_RTL_SOURCES
  odef.cpp
  odef_allocator.cpp
  odef_interceptors.cpp
  odef_linux.cpp
  )

set(ODEF_RTL_CXX_SOURCES
  odef_new_delete.cpp
  )

set(ODEF_RTL_HEADERS
  odef.h
  odef_allocator.h
  odef_interceptors.h
  odef_interface_internal.h
  )

set(ODEF_RTL_CFLAGS ${SANITIZER_COMMON_CFLAGS})
append_rtti_flag(OFF ODEF_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FPIE_FLAG -fPIE ODEF_RTL_CFLAGS)
append_list_if(COMPILER_RT_HAS_FFREESTANDING_FLAG -ffreestanding ODEF_RTL_CFLAGS)

set(ODEF_RUNTIME_LIBRARIES)

add_compiler_rt_component(odef)

foreach(arch ${ODEF_SUPPORTED_ARCH})
	add_compiler_rt_runtime(clang_rt.odef
		STATIC
		ARCHS ${arch}
		SOURCES ${ODEF_RTL_SOURCES}
            $<TARGET_OBJECTS:RTInterception.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommon.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonLibc.${arch}>
            $<TARGET_OBJECTS:RTSanitizerCommonSymbolizer.${arch}>
    ADDITIONAL_HEADERS ${ODEF_RTL_HEADERS}
		CFLAGS ${ODEF_RTL_CFLAGS}
		PARENT_TARGET odef)
  add_compiler_rt_runtime(clang_rt.odef_cxx
    STATIC
		ARCHS ${arch}
    SOURCES ${ODEF_RTL_CXX_SOURCES}
    ADDITIONAL_HEADERS ${ODEF_RTL_HEADERS}
    CFLAGS ${ODEF_RTL_CFLAGS}
    PARENT_TARGET odef)
  list(APPEND ODEF_RUNTIME_LIBRARIES clang_rt.odef-${arch}
                                     clang_rt.odef_cxx-${arch})
  if(SANITIZER_USE_SYMBOLS)
    add_sanitizer_rt_symbols(clang_rt.odef
      ARCHS ${arch}
      EXTRA odef.syms.extra)
    add_sanitizer_rt_symbols(clang_rt.odef_cxx
      ARCHS ${arch}
      EXTRA odef.syms.extra)
    add_dependencies(odef clang_rt.odef-${arch}-symbols
                          clang_rt.odef_cxx-${arch}-symbols)
  endif()
endforeach()
