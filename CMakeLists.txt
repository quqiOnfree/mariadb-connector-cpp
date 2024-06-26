# ************************************************************************************
#   Copyright (C) 2013,2023 MariaDB Corporation AB
#
#   This library is free software; you can redistribute it and/or
#   modify it under the terms of the GNU Library General Public
#   License as published by the Free Software Foundation; either
#   version 2.1 of the License, or (at your option) any later version.
#
#   This library is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
#   Library General Public License for more details.
#
#   You should have received a copy of the GNU Library General Public
#   License along with this library; if not see <http://www.gnu.org/licenses>
#   or write to the Free Software Foundation, Inc.,
#   51 Franklin St., Fifth Floor, Boston, MA 02110, USA
# *************************************************************************************/

IF(APPLE)
  # productbuild packaging stuff 
  CMAKE_MINIMUM_REQUIRED(VERSION 3.23)
ELSE()
  CMAKE_MINIMUM_REQUIRED(VERSION 3.1)
ENDIF()
CMAKE_POLICY(SET CMP0048 NEW)

PROJECT(mariadb_connector_cpp
        VERSION 1.0.4
#        DESCRIPTION "MariaDB Connector/C++"
        LANGUAGES CXX C)

IF(CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 4.8)
  MESSAGE(FATAL_ERROR "GCC version >=4.8 is required")
ENDIF()

SET(MACPP_VERSION_QUALITY "ga") #Empty also means GA
SET(MACPP_VERSION "1.00.0004")
SET(MARIADB_DEFAULT_PLUGINS_SUBDIR "plugin")

# For C/C
IF(CMAKE_VERSION VERSION_LESS "3.1")
  IF(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    SET (CMAKE_C_FLAGS ${CMAKE_C_FLAGS} "-std=gnu99 ${CMAKE_C_FLAGS}")
  ENDIF()
ELSE()
  SET(CMAKE_C_STANDARD 99)
ENDIF()

SET(CMAKE_CXX_STANDARD 11)
SET(CMAKE_CXX_STANDARD_REQUIRED ON)

GET_PROPERTY(MAODBC_LANGUAGES GLOBAL PROPERTY ENABLED_LANGUAGES)
# We don't need RC for what we need MAODBC_LANGUAGES for
LIST(REMOVE_ITEM MAODBC_LANGUAGES "RC")

SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

SET(LIBRARY_NAME "mariadbcpp")
SET(STATIC_LIBRARY_NAME "${LIBRARY_NAME}-static")

CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in
               ${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h)
CONFIGURE_FILE(${CMAKE_CURRENT_SOURCE_DIR}/src/maconncpp.rc.in
               ${CMAKE_CURRENT_SOURCE_DIR}/src/maconncpp.rc)
INCLUDE(SearchLibrary)
INCLUDE(SetValueMacro)


SET (MACPP_SOURCES src/MariaDbDriver.cpp
                   src/DriverManager.cpp
                   src/UrlParser.cpp
                   src/MariaDbDatabaseMetaData.cpp
                   src/HostAddress.cpp
                   src/Consts.cpp
                   src/SQLString.cpp
                   src/MariaDbConnection.cpp
                   src/MariaDbStatement.cpp
                   src/MariaDBException.cpp
                   src/MariaDBWarning.cpp
                   src/Identifier.cpp
                   src/MariaDbSavepoint.cpp
                   src/SqlStates.cpp
                   src/Results.cpp

                   src/ColumnType.cpp

                   src/ColumnDefinition.cpp
                   src/protocol/MasterProtocol.cpp

                   src/protocol/capi/QueryProtocol.cpp
                   src/protocol/capi/ConnectProtocol.cpp
                   src/com/capi/ColumnDefinitionCapi.cpp

                   src/cache/CallableStatementCache.cpp
                   src/cache/CallableStatementCacheKey.cpp

                   src/util/Value.cpp
                   src/util/Utils.cpp
                   src/util/String.cpp

                   src/logger/NoLogger.cpp
                   src/logger/LoggerFactory.cpp
                   src/logger/ProtocolLoggingProxy.cpp

                   src/parameters/ParameterHolder.cpp

                   src/options/Options.cpp
                   src/options/DefaultOptions.cpp

                   src/pool/GlobalStateInfo.cpp
                   src/pool/Pools.cpp

                   src/failover/FailoverProxy.cpp

                   src/credential/CredentialPluginLoader.cpp

                   src/SelectResultSet.cpp
                   src/com/capi/SelectResultSetCapi.cpp
                   #src/com/SelectResultSetPacket.cpp

                   #src/com/ColumnDefinitionPacket.cpp

                   src/com/ColumnNameMap.cpp

                   src/io/StandardPacketInputStream.cpp

                   src/Charset.cpp
                   src/ClientSidePreparedStatement.cpp
                   src/BasePrepareStatement.cpp
                   src/MariaDbFunctionStatement.cpp
                   src/MariaDbProcedureStatement.cpp
                   src/ServerSidePreparedStatement.cpp

                   src/parameters/BigDecimalParameter.cpp
                   src/parameters/BooleanParameter.cpp
                   src/parameters/ByteArrayParameter.cpp
                   src/parameters/ByteParameter.cpp
                   src/parameters/DateParameter.cpp
                   src/parameters/DefaultParameter.cpp
                   src/parameters/DoubleParameter.cpp
                   src/parameters/FloatParameter.cpp
                   src/parameters/IntParameter.cpp
                   #src/parameters/LocalTimeParameter.cpp
                   src/parameters/LongParameter.cpp
                   src/parameters/ULongParameter.cpp
                   src/parameters/NullParameter.cpp
                   #src/parameters/OffsetTimeParameter.cpp
                   src/parameters/ParameterHolder.cpp
                   src/parameters/ReaderParameter.cpp
                   #src/parameters/SerializableParameter.cpp
                   src/parameters/ShortParameter.cpp
                   src/parameters/StreamParameter.cpp
                   src/parameters/TimeParameter.cpp
                   src/parameters/TimestampParameter.cpp
                   #src/parameters/ZonedDateTimeParameter.cpp
                   src/parameters/StringParameter.cpp
                   src/MariaDbPooledConnection.cpp
                   src/MariaDbParameterMetaData.cpp
                   src/MariaDbResultSetMetaData.cpp
                   src/CallParameter.cpp
                   src/CallableParameterMetaData.cpp
                   src/com/Packet.cpp
                   src/credential/Credential.cpp
                   src/util/LogQueryTool.cpp
                   src/util/ClientPrepareResult.cpp
                   src/util/ServerPrepareResult.cpp
                   src/util/ServerPrepareStatementCache.cpp
                   src/com/CmdInformationSingle.cpp
                   src/com/CmdInformationBatch.cpp
                   src/com/CmdInformationMultiple.cpp
                   src/com/RowProtocol.cpp
                   src/protocol/capi/BinRowProtocolCapi.cpp
                   src/protocol/capi/TextRowProtocolCapi.cpp
                   src/ExceptionFactory.cpp
                   src/StringImp.cpp
                   src/CArray.cpp
                   src/SimpleParameterMetaData.cpp
                   ####CPP####
                   )

IF(WIN32)
  SET(MACPP_SOURCES ${MACPP_SOURCES}
                   src/Dll.c

                   src/MariaDBConnCpp.h
                   src/MariaDbDriver.h
                   src/UrlParser.h
                   src/MariaDbDatabaseMetaData.h
                   src/HostAddress.h
                   src/Version.h
                   src/Consts.h
                   src/MariaDbConnection.h
                   src/MariaDbStatement.h
                   src/MariaDBWarning.h
                   src/Protocol.h
                   src/Identifier.h
                   src/MariaDbSavepoint.h
                   src/SqlStates.h
                   src/Results.h
                   src/ColumnDefinition.h
                   src/MariaDbServerCapabilities.h

                   src/protocol/MasterProtocol.h

                   src/protocol/capi/QueryProtocol.h
                   src/protocol/capi/ConnectProtocol.h
                   src/com/capi/ColumnDefinitionCapi.h

                   src/cache/CallableStatementCache.h
                   src/cache/CallableStatementCacheKey.h

                   src/util/Value.h
                   src/util/ClassField.h
                   src/util/Utils.h
                   src/util/StateChange.h
                   src/util/String.h

                   src/logger/NoLogger.h
                   src/logger/LoggerFactory.h
                   src/logger/Logger.h
                   src/logger/ProtocolLoggingProxy.h

                   src/parameters/ParameterHolder.h

                   src/options/Options.h
                   src/options/DefaultOptions.h
                   src/options/DriverPropertyInfo.h

                   src/io/PacketOutputStream.h
                   src/io/PacketInputStream.h
                   src/io/StandardPacketInputStream.h

                   src/credential/CredentialPlugin.h
                   src/credential/CredentialPluginLoader.h

                   src/pool/GlobalStateInfo.h
                   src/pool/Pools.h
                   src/pool/Pool.h

                   src/failover/FailoverProxy.h

                   src/Listener.h

                   src/com/CmdInformation.h

                   "include/conncpp.hpp"
                   "include/conncpp/Connection.hpp"
                   "include/conncpp/Statement.hpp"
                   "include/conncpp/ResultSet.hpp"
                   "include/conncpp/PreparedStatement.hpp"
                   "include/conncpp/ParameterMetaData.hpp"
                   "include/conncpp/ResultSetMetaData.hpp"
                   "include/conncpp/DatabaseMetaData.hpp"
                   "include/conncpp/CallableStatement.hpp"
                   "include/conncpp/SQLString.hpp"
                   "include/conncpp/Warning.hpp"
                   "include/conncpp/Exception.hpp"
                   "include/conncpp/jdbccompat.hpp"
                   "include/conncpp/Driver.hpp"
                   "include/conncpp/DriverManager.hpp"
                   "include/conncpp/Types.hpp"
                   "include/conncpp/buildconf.hpp"
                   "include/conncpp/CArray.hpp"

                   src/SelectResultSet.h
                   src/com/capi/SelectResultSetCapi.h
                   src/com/SelectResultSetPacket.h
                   src/ColumnType.h
                   src/com/ColumnDefinitionPacket.h
                   src/com/ColumnNameMap.h
                   src/Charset.h
                   src/ClientSidePreparedStatement.h
                   src/BasePrepareStatement.h
                   src/MariaDbFunctionStatement.h
                   src/MariaDbProcedureStatement.h
                   src/ServerSidePreparedStatement.h

                   src/parameters/BigDecimalParameter.h
                   src/parameters/BooleanParameter.h
                   src/parameters/ByteArrayParameter.h
                   src/parameters/ByteParameter.h
                   src/parameters/DateParameter.h
                   src/parameters/DefaultParameter.h
                   src/parameters/DoubleParameter.h
                   src/parameters/FloatParameter.h
                   src/parameters/IntParameter.h
                   #src/parameters/LocalTimeParameter.h
                   src/parameters/LongParameter.h
                   src/parameters/ULongParameter.h
                   src/parameters/NullParameter.h
                   #src/parameters/OffsetTimeParameter.h
                   src/parameters/ReaderParameter.h
                   #src/parameters/SerializableParameter.h
                   src/parameters/ShortParameter.h
                   src/parameters/StreamParameter.h
                   src/parameters/TimeParameter.h
                   src/parameters/TimestampParameter.h
                   #src/parameters/ZonedDateTimeParameter.h
                   src/parameters/StringParameter.h

                   src/Parameters.h
                   src/MariaDbPooledConnection.h
                   src/MariaDbParameterMetaData.h
                   src/CallParameter.h
                   src/CallableParameterMetaData.h
                   src/MariaDbResultSetMetaData.h
                   src/com/Packet.h
                   src/util/ServerStatus.h
                   src/credential/Credential.h
                   src/util/LogQueryTool.h
                   src/PrepareResult.h
                   src/util/ClientPrepareResult.h
                   src/util/ServerPrepareResult.h
                   src/util/ServerPrepareStatementCache.h
                   src/com/CmdInformationSingle.h
                   src/com/CmdInformationBatch.h
                   src/com/CmdInformationMultiple.h
                   src/com/RowProtocol.h
                   src/protocol/capi/BinRowProtocolCapi.h
                   src/protocol/capi/TextRowProtocolCapi.h
                   src/ExceptionFactory.h
                   src/StringImp.h
                   src/CArrayImp.h
                   src/SimpleParameterMetaData.h
                   ####H####
                       )
  SOURCE_GROUP(Logging REGULAR_EXPRESSION "/logger/")
  SOURCE_GROUP(PublicAPI REGULAR_EXPRESSION "include/")
  SOURCE_GROUP(Parameters REGULAR_EXPRESSION "src/parameters")
  SOURCE_GROUP(Options REGULAR_EXPRESSION "src/options")
  SOURCE_GROUP(IO REGULAR_EXPRESSION "src/io")
  SOURCE_GROUP(Credential REGULAR_EXPRESSION "src/credential")
  SOURCE_GROUP(Pool REGULAR_EXPRESSION "src/pool")
  SOURCE_GROUP(Utils REGULAR_EXPRESSION "src/util")
  SOURCE_GROUP(Caches REGULAR_EXPRESSION "src/cache")
  SOURCE_GROUP(Failover REGULAR_EXPRESSION "src/failover")
  SOURCE_GROUP(Commands REGULAR_EXPRESSION "src/com/")
  SOURCE_GROUP(Protocol REGULAR_EXPRESSION "src/protocol/")
  SOURCE_GROUP(C_API REGULAR_EXPRESSION "/capi/")

  SET(PLATFORM_DEPENDENCIES ws2_32)
ELSE()
  SEARCH_LIBRARY(LIB_MATH floor m)
  MESSAGE(STATUS "Found math lib: ${LIB_MATH}")
  SET(PLATFORM_DEPENDENCIES ${LIB_MATH})
ENDIF()

INCLUDE(check_compiler_flag)
IF(WITH_ASAN)
  IF(MSVC)
    MA_SET_COMPILER_FLAG("-fsanitize=address" DEBUG RELWITHDEBINFO)
    SET(MAODBC_LINKER_FLAGS ${MAODBC_LINKER_FLAGS} /INCREMENTAL:NO)
    MA_SET_LINKER_FLAG("${MAODBC_LINKER_FLAGS}" DEBUG RELWITHDEBINFO)
  ELSE()
    MY_CHECK_AND_SET_COMPILER_FLAG("-U_FORTIFY_SOURCE" DEBUG RELWITHDEBINFO)
    MY_CHECK_AND_SET_COMPILER_FLAG("-fsanitize=address -fPIC" DEBUG RELWITHDEBINFO)

    SET(WITH_ASAN_OK 1)
    FOREACH(lang ${MAODBC_LANGUAGES})
      IF(NOT ${have_${lang}__fsanitize_address__fPIC})
        SET(WITH_ASAN_OK 0)
      ENDIF()
    ENDFOREACH()
    IF(WITH_ASAN_OK)
      OPTION(WITH_ASAN_SCOPE "Enable -fsanitize-address-use-after-scope" OFF)
      IF(WITH_ASAN_SCOPE)
        MY_CHECK_AND_SET_COMPILER_FLAG("-fsanitize=address -fsanitize-address-use-after-scope" DEBUG RELWITHDEBINFO)
      ENDIF()
    ELSE()
      MESSAGE(FATAL_ERROR "Do not know how to enable address sanitizer")
    ENDIF()
  ENDIF()
ENDIF()

IF (WITH_UBSAN)
  MY_CHECK_AND_SET_COMPILER_FLAG("-fsanitize=undefined -fno-sanitize=alignment -U_FORTIFY_SOURCE -DWITH_UBSAN" DEBUG RELWITHDEBINFO)
ENDIF()

IF (WITH_MSAN)
  MY_CHECK_AND_SET_COMPILER_FLAG("-fsanitize=memory -fsanitize-memory-track-origins -U_FORTIFY_SOURCE" DEBUG RELWITHDEBINFO)
ENDIF()

# This has to be before C/C's cmake run, or it will build with /MD
IF(WIN32)
  IF (MSVC)
    SET(CONFIG_TYPES "DEBUG" "RELEASE" "RELWITHDEBINFO" "MINSIZEREL")
    FOREACH(BUILD_TYPE ${CONFIG_TYPES})
      FOREACH(COMPILER ${MAODBC_LANGUAGES})
        SET(COMPILER_FLAGS "${CMAKE_${COMPILER}_FLAGS_${BUILD_TYPE}}")
        IF (NOT COMPILER_FLAGS STREQUAL "")
          IF(NOT WITH_ASAN)
            STRING(REPLACE "/MD" "/MT" COMPILER_FLAGS ${COMPILER_FLAGS})
            STRING(REPLACE "/Zi" "/ZI" COMPILER_FLAGS ${COMPILER_FLAGS})
          ENDIF(NOT WITH_ASAN)
          MESSAGE (STATUS "CMAKE_${COMPILER}_FLAGS_${BUILD_TYPE}= ${COMPILER_FLAGS}")
          SET(CMAKE_${COMPILER}_FLAGS_${BUILD_TYPE} ${COMPILER_FLAGS} CACHE
               STRING "Overwritten by mariadb-cpp" FORCE)
        ENDIF()
      ENDFOREACH()
    ENDFOREACH()
  ENDIF()
  ADD_DEFINITIONS(-D_CRT_SECURE_NO_WARNINGS -D_SCL_SECURE_NO_WARNINGS -DWIN32_LEAN_AND_MEAN)
  SET(INSTALL_PLUGINDIR "${MARIADB_DEFAULT_PLUGINS_SUBDIR}")
  SET(PLATFORM_DEPENDENCIES ${PLATFORM_DEPENDENCIES} version.lib)
ENDIF()

### Build options, initial settings and platform defaults
INCLUDE("options_defaults")

### Setting installation paths - should go before C/C subproject sets its own. We need to have control over those
INCLUDE("install")

IF(NOT MINGW AND WIN32)
  MESSAGE(STATUS "Generate MSI package: ${WITH_MSI}")
  MESSAGE(STATUS "Sign MSI package: ${WITH_SIGNCODE}")
ENDIF()

# By now we have everything needed by tests. If we need to build them only - firing config now and exit
# There is "normal" tests config below
IF(BUILD_TESTS_ONLY)
  IF(WIN32)
    LINK_DIRECTORIES("$ENV{ProgramFiles}/MariaDB/MariaDB C++ Connector 64-bit")
  ELSE()
    MESSAGE(STATUS "Configurig Tests: tcp://${TEST_UID}@${TEST_SERVER}:${TEST_PORT}/${TEST_SCHEMA} socket: ${TEST_SOCKET}")
    SET(DRIVER_LIB_LOCATION "${libmariadb_prefix}/${INSTALL_LIBDIR}")
  ENDIF()
  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include)
  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/include/conncpp)
  # At some point use of String.cpp from the driver was added to tests. Need to verify if it's still needed/a good idea.
  INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/src)
  ADD_SUBDIRECTORY(test)
  # Only means only
  RETURN()
ENDIF()

ADD_CUSTOM_TARGET(DEPENDENCIES_FOR_PACKAGE)
IF(NOT WITH_SSL)
  MESSAGE(STATUS "Setting default value for WITH_SSL for libmariadb build to ON")
  SET(CONC_WITH_SSL ON)
ENDIF()

### Including C/C subproject
find_package(unofficial-libmariadb CONFIG REQUIRED)
include_directories(${LIBRARY_NAME}_obj
    include/conncpp
    include
    src)

ADD_LIBRARY(${LIBRARY_NAME}_obj OBJECT ${MACPP_SOURCES})
TARGET_LINK_LIBRARIES(${LIBRARY_NAME}_obj PRIVATE unofficial::libmariadb)
IF(UNIX)
  SET_TARGET_PROPERTIES(${LIBRARY_NAME}_obj PROPERTIES COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_C_FLAGS}")
ENDIF()
SET(${LIBRARY_NAME}_OBJECTS $<TARGET_OBJECTS:${LIBRARY_NAME}_obj>)

IF(WIN32 AND NOT MINGW)
  ADD_LIBRARY(${LIBRARY_NAME} SHARED ${CMAKE_CURRENT_SOURCE_DIR}/src/maconncpp.rc ${${LIBRARY_NAME}_OBJECTS} ${CMAKE_CURRENT_SOURCE_DIR}/src/maconncpp.def)
  ADD_LIBRARY(${STATIC_LIBRARY_NAME} STATIC ${${LIBRARY_NAME}_OBJECTS} ${CMAKE_CURRENT_SOURCE_DIR}/src/maconncpp.rc)

  TARGET_COMPILE_DEFINITIONS(${LIBRARY_NAME}_obj PRIVATE "MARIADB_EXPORTED=__declspec(dllexport)")

  TARGET_COMPILE_DEFINITIONS(${LIBRARY_NAME} PRIVATE "MARIADB_EXPORTED=__declspec(dllexport)")
  TARGET_COMPILE_DEFINITIONS(${LIBRARY_NAME} INTERFACE "MARIADB_EXPORTED=__declspec(dllimport)")

  TARGET_COMPILE_DEFINITIONS(${STATIC_LIBRARY_NAME} PUBLIC MARIADB_STATIC_LINK)
ELSE()
  # Xcode doesn't support targets that have only object files,
  # so let's add an empty file to keep Xcode happy
  IF(CMAKE_GENERATOR MATCHES Xcode)
    FILE(WRITE ${CMAKE_BINARY_DIR}/empty.cpp "")
    SET(EMPTY_FILE ${CMAKE_BINARY_DIR}/empty.cpp)
  ENDIF()
  #  MESSAGE(STATUS "Version script: ${CMAKE_CURRENT_SOURCE_DIR}/src/maconncpp.def")
  ADD_LIBRARY(${LIBRARY_NAME} SHARED ${${LIBRARY_NAME}_OBJECTS} ${EMPTY_FILE})
  ADD_LIBRARY(${STATIC_LIBRARY_NAME} STATIC ${${LIBRARY_NAME}_OBJECTS} ${EMPTY_FILE})

  IF(APPLE)
    SET_TARGET_PROPERTIES(${LIBRARY_NAME} PROPERTIES LINK_FLAGS "-Wl"
                                     INSTALL_RPATH_USE_LINK_PATH 0
                                     BUILD_WITH_INSTALL_RPATH 1
                                     XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
                                     XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--timestamp -f"
                                     INSTALL_RPATH "${MAODBC_INSTALL_RPATH}")
    SET_TARGET_PROPERTIES(${STATIC_LIBRARY_NAME} PROPERTIES 
                                     INSTALL_RPATH_USE_LINK_PATH 0
                                     BUILD_WITH_INSTALL_RPATH 1
                                     XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
                                     XCODE_ATTRIBUTE_OTHER_CODE_SIGN_FLAGS "--timestamp -f"
                                     INSTALL_RPATH "${MAODBC_INSTALL_RPATH}")
    IF(WITH_SIGNCODE)
      SET_TARGET_PROPERTIES(${LIBRARY_NAME} PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application: ${DEVELOPER_ID}")
      SET_TARGET_PROPERTIES(${STATIC_LIBRARY_NAME} PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Developer ID Application: ${DEVELOPER_ID}")
    ELSE()
      SET_TARGET_PROPERTIES(${LIBRARY_NAME} PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
      SET_TARGET_PROPERTIES(${STATIC_LIBRARY_NAME} PROPERTIES XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")
    ENDIF()
  ELSE()
#    SET_TARGET_PROPERTIES(${LIBRARY_NAME} PROPERTIES LINK_FLAGS "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/src/maconncpp.def")
  ENDIF()
ENDIF()

TARGET_LINK_LIBRARIES(${LIBRARY_NAME} ${MARIADB_CLIENT_TARGET_NAME} ${PLATFORM_DEPENDENCIES} unofficial::libmariadb)
TARGET_LINK_LIBRARIES(${STATIC_LIBRARY_NAME} mariadbclient)
