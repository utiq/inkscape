# SPDX-License-Identifier: GPL-2.0-or-later

include(${CMAKE_SOURCE_DIR}/CMakeScripts/UseGlibMarshal.cmake)

GLIB_MARSHAL(sp_marshal sp-marshal "${CMAKE_CURRENT_BINARY_DIR}/helper")

set(sp_marshal_SRC
	${CMAKE_CURRENT_BINARY_DIR}/sp-marshal.cpp
	${CMAKE_CURRENT_BINARY_DIR}/sp-marshal.h
)

set(helper_SRC
	choose-file.cpp
	geom.cpp
	geom-nodetype.cpp
	geom-pathstroke.cpp
	geom-pathvector_nodesatellites.cpp
	geom-nodesatellite.cpp
	gettext.cpp
	pixbuf-ops.cpp
	png-write.cpp
	save-image.cpp
	stock-items.cpp
	#units-test.cpp

	# we generate this file and it's .h counter-part
	${sp_marshal_SRC}


	# -------
	# Headers
	choose-file.h
	geom-curves.h
	geom-nodetype.h
	geom-pathstroke.h
	geom-pathvector_nodesatellites.h
	geom-nodesatellite.h
	geom.h
	gettext.h
	mathfns.h
	pixbuf-ops.h
	png-write.h
    save-image.h
	sigc-track-obj.h
	stock-items.h
)

set_source_files_properties(sp_marshal_SRC PROPERTIES GENERATED true)

# add_inkscape_lib(helper_LIB "${helper_SRC}")
add_inkscape_source("${helper_SRC}")
