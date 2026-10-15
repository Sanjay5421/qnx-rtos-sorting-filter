COV_NAME := libstartup

COV_RULESET := freestanding

COV_DEPS := \
            lib/c \
            lib/gelf \
            lib/libfdt \
            lib/lzo2 \
            services/system \

COV_ROOT := $(realpath $(CURDIR)/../../..)
