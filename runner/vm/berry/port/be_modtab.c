/* ChargeXcel module table for Berry. Replaces upstream default/be_modtab.c.
 *
 * Only the modules berry_conf.h enables are declared here, so a script's
 * `import os` fails at the module lookup rather than at some later guard.
 * The `dlm` module is NOT here on purpose: DlmScriptVm.cpp builds it at
 * runtime with be_newmodule(), so the Berry port never references anything
 * outside itself and can be compiled and tested on its own.
 */
#include "berry.h"

be_extern_native_module(string);
be_extern_native_module(json);
be_extern_native_module(math);
be_extern_native_module(undefined);

BERRY_LOCAL const bntvmodule_t* const be_module_table[] = {
#if BE_USE_STRING_MODULE
    &be_native_module(string),
#endif
#if BE_USE_JSON_MODULE
    &be_native_module(json),
#endif
#if BE_USE_MATH_MODULE
    &be_native_module(math),
#endif
    &be_native_module(undefined),
    NULL /* do not remove */
};

BERRY_LOCAL bclass_array be_class_table = {
    NULL, /* do not remove */
};
