/**
 * Copyright (c) 2015 Runtime Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stddef.h>

#include "config/config.h"
#include "config_priv.h"

#ifdef SHELL_PRESENT
#include <string.h>

#include <shell/shell.h>
#include <console/console.h>

static struct shell_cmd shell_conf_cmd;

static int
shell_conf_command(int argc, char **argv)
{
    char *name = NULL;
    char *val = NULL;
    char *name_argv[CONF_MAX_DIR_DEPTH];
    char tmp_buf[16];
    int name_argc;
    int rc;
    struct conf_entry *ce;

    switch (argc) {
    case 1:
        break;
    case 2:
        name = argv[1];
        break;
    case 3:
        name = argv[1];
        val = argv[2];
        break;
    default:
        goto err;
    }

    rc = conf_parse_name(name, &name_argc, name_argv);
    if (rc) {
        goto err;
    }

    ce = conf_lookup(name_argc, name_argv);
    if (!ce) {
        console_printf("No such config variable\n");
        goto err;
    }

    if (!val) {
        val = conf_get_value(ce, tmp_buf, sizeof(tmp_buf));
        if (!val) {
            console_printf("Cannot display value\n");
            goto err;
        }
        console_printf("%s", val);
    } else {
        rc = conf_set_value(ce, val);
        if (rc) {
            console_printf("Failed to set\n");
            goto err;
        }
    }
    return 0;
err:
    console_printf("Invalid args\n");
    return 0;
}
#endif

int conf_module_init(void)
{
#ifdef SHELL_PRESENT
    shell_cmd_register(&shell_conf_cmd, "config", shell_conf_command);
#endif
#ifdef NEWTMGR_PRESENT
    conf_nmgr_register();
#endif
    return 0;
}
