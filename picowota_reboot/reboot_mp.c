/**
 * Copyright (c) 2023 KisChang <feichang0609@gmail.com>
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "py/runtime.h"
#include "py/obj.h"

#include "picowota/reboot.h"

STATIC mp_obj_t mp_picowota_reboot(mp_obj_t arg_obj) {
	bool arg = mp_obj_is_true(arg_obj);
    picowota_reboot(arg);
	return mp_obj_new_int(1);
}

STATIC MP_DEFINE_CONST_FUN_OBJ_1(mp_picowota_reboot_obj, mp_picowota_reboot);

STATIC const mp_rom_map_elem_t example_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_picowota_reboot) },
    { MP_ROM_QSTR(MP_QSTR_reboot), MP_ROM_PTR(&mp_picowota_reboot_obj) },
};
STATIC MP_DEFINE_CONST_DICT(example_module_globals, example_module_globals_table);

// Define module object.
const mp_obj_module_t user_cmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&example_module_globals,
};

// Register the module to make it available in Python.
MP_REGISTER_MODULE(MP_QSTR_picowota_reboot, user_cmodule);
