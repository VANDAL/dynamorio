# from Deployment example
add_library(drsigil SHARED drsigil.c drsigil_instrument.c drsigil_ipc.c drsigil_parser.c)
find_package(DynamoRIO)
if (NOT DynamoRIO_FOUND)
  message(FATAL_ERROR "DynamoRIO package required to build")
endif(NOT DynamoRIO_FOUND)
configure_DynamoRIO_client(drsigil)

# IPC w/ Sigil2
include_directories(../../../..)

# Sigil2 Primitives
include_directories(../../../../..)

# target DynamoRIO extensions
use_DynamoRIO_extension(drsigil drmgr)
use_DynamoRIO_extension(drsigil drutil)
use_DynamoRIO_extension(drsigil drwrap)
use_DynamoRIO_extension(drsigil drreg)

install(TARGETS drsigil
	DESTINATION ${INSTALL_CLIENTS_LIB})
