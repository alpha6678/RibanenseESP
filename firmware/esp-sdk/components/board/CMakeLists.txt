set(_rbn_version_json "")
foreach(_p
    "${CMAKE_CURRENT_LIST_DIR}/../../../ribanense-esp/version.json"
    "${CMAKE_CURRENT_LIST_DIR}/../../version.json")
    if(EXISTS "${_p}")
        set(_rbn_version_json "${_p}")
        break()
    endif()
endforeach()

if(NOT _rbn_version_json)
    message(FATAL_ERROR "version.json nao encontrado (OS ou copia no esp-sdk).")
endif()

# file(READ) nao cria dependencia: sem isto o CMake nao reconfigura quando a
# versao muda e o build sai com o numero antigo dentro do header.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_rbn_version_json}")

file(READ "${_rbn_version_json}" _rbn_vj)

set(RIBANENSEESP_PRODUCT "RibanenseESP")
set(RIBANENSEESP_VERSION "0.0.0")
set(RIBANENSEESP_GITHUB_OWNER "alpha6678")
set(RIBANENSEESP_GITHUB_REPO "RibanenseESP")
set(RIBANENSEESP_LAN_SEED "rbn-lan")

if(_rbn_vj MATCHES "\"product\"[ \t]*:[ \t]*\"([^\"]+)\"")
    set(RIBANENSEESP_PRODUCT "${CMAKE_MATCH_1}")
endif()
if(_rbn_vj MATCHES "\"version\"[ \t]*:[ \t]*\"([^\"]+)\"")
    set(RIBANENSEESP_VERSION "${CMAKE_MATCH_1}")
endif()
if(_rbn_vj MATCHES "\"githubOwner\"[ \t]*:[ \t]*\"([^\"]+)\"")
    set(RIBANENSEESP_GITHUB_OWNER "${CMAKE_MATCH_1}")
endif()
if(_rbn_vj MATCHES "\"githubRepo\"[ \t]*:[ \t]*\"([^\"]+)\"")
    set(RIBANENSEESP_GITHUB_REPO "${CMAKE_MATCH_1}")
endif()
if(_rbn_vj MATCHES "\"lanSeed\"[ \t]*:[ \t]*\"([^\"]+)\"")
    set(RIBANENSEESP_LAN_SEED "${CMAKE_MATCH_1}")
endif()

set(_rbn_gen_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${_rbn_gen_dir}")
configure_file(
    "${CMAKE_CURRENT_LIST_DIR}/include/ribanense_esp_version.h.in"
    "${_rbn_gen_dir}/ribanense_esp_version.h"
    @ONLY
)

idf_component_register(SRCS "board.c"
                    INCLUDE_DIRS "include" "${_rbn_gen_dir}"
                    REQUIRES driver esp_lcd esp_common esp_hw_support
                    PRIV_REQUIRES mbedtls)
