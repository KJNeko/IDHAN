option(ATLAS_PROFILE_ENABLE "" OFF)

if (${ATLAS_PROFILE_ENABLE} STREQUAL "ON")
	option(TRACY_ENABLE "" ON)
	option(TRACY_ON_DEMAND "" ON) #Reduces memory usage if profile isn't attached and makes memory profiling possible.
	option(TRACY_NO_BROADCAST "" ON)
	option(TRACY_NO_VSYNC_CAPTURE "" ON)
	option(TRACY_NO_FRAME_IMAGE "" ON)
else ()
	option(TRACY_ENABLE "" OFF)
endif ()
