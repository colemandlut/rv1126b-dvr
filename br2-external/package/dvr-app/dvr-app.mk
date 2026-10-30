################################################################################
#
# dvr-app
#
################################################################################

DVR_APP_VERSION = local
DVR_APP_SITE = $(BR2_EXTERNAL_DVR_PATH)/../app
DVR_APP_SITE_METHOD = local
DVR_APP_LICENSE = Proprietary
DVR_APP_DEPENDENCIES = json-for-modern-cpp
# 系统里有完整的 C++ 运行库，不需要静态链接
DVR_APP_CONF_OPTS = -DDVR_STATIC=OFF -DDVR_TESTS=OFF

$(eval $(cmake-package))
