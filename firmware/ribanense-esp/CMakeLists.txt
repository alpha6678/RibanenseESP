cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../esp-sdk/components")

# PROJECT_VER alimenta o app_desc gravado no binario. Sem isto o IDF tenta
# git describe, nao acha .git no espelho C:\fw e grava "1" — o artefato deixa
# de dizer qual versao ele e, e o publish nao tem como conferir.
# O CMAKE_CONFIGURE_DEPENDS deste arquivo mora no componente board: declarar
# o mesmo caminho duas vezes faz o ninja acusar "multiple rules generate".
file(READ "${CMAKE_CURRENT_LIST_DIR}/version.json" _rbn_vj)
if(_rbn_vj MATCHES "\"version\"[ \t]*:[ \t]*\"([^\"]+)\"")
    set(PROJECT_VER "${CMAKE_MATCH_1}")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ribanense_esp)
