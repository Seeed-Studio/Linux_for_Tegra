global-incdirs-y += include

# Include header files from $(NV_OPTEE_DIR)
ifneq ("$(wildcard $(NV_OPTEE_DIR))","")
global-incdirs_ext-y += $(NV_OPTEE_DIR)/lib/libutee/include
endif

srcs-y += abort.c
srcs-y += assert.c
srcs-y += tee_uuid_from_str.c
srcs-y += trace_ext.c

ifneq ($(sm),ldelf)
srcs-y += base64.c
srcs-y += tee_api.c
srcs-y += tee_api_arith_mpi.c
srcs-y += tee_api_objects.c
srcs-y += tee_api_operations.c
srcs-y += tee_api_panic.c
srcs-y += tee_api_property.c
srcs-y += tee_socket_pta.c
srcs-y += tee_system_pta.c
srcs-y += tee_tcpudp_socket.c
srcs-y += tcb.c
srcs-y += user_ta_entry.c
srcs-y += user_ta_entry_compat.c
endif #ifneq ($(sm),ldelf)

subdirs-y += arch/$(ARCH)
