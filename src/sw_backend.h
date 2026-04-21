#ifndef SW_BACKEND_H
#define SW_BACKEND_H

#include "sw_internal.h"

int sw_backend_init(sw_mgr* mgr);
void sw_backend_shutdown(sw_mgr* mgr);
int sw_backend_register_listener(sw_mgr* mgr, sw_listener* listener);
int sw_backend_register_connection(sw_mgr* mgr, sw_connection* connection);
int sw_backend_update_connection(sw_mgr* mgr, sw_connection* connection);
void sw_backend_unregister_listener(sw_mgr* mgr, sw_listener* listener);
void sw_backend_unregister_connection(sw_mgr* mgr, sw_connection* connection);
int sw_backend_poll(sw_mgr* mgr, i32 timeout_ms);

#endif
