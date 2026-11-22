#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <cyaml/cyaml.h>

#include <init/log.h>
#include <init/macros.h>
#include <init/system.h>

struct env_entry {
    char *key;
    char *value;
};

struct env_doc {
    struct env_entry *env;
    unsigned env_count;
};

static const cyaml_schema_field_t env_entry_fields[] = {
    CYAML_FIELD_STRING_PTR("key", CYAML_FLAG_POINTER, struct env_entry, key, 0, CYAML_UNLIMITED),
    CYAML_FIELD_STRING_PTR("value", CYAML_FLAG_POINTER, struct env_entry, value, 0, CYAML_UNLIMITED),
    CYAML_FIELD_END
};

static const cyaml_schema_value_t env_entry_schema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_DEFAULT, struct env_entry, env_entry_fields),
};

static const cyaml_schema_field_t env_doc_fields[] = {
    CYAML_FIELD_SEQUENCE(
        "env", CYAML_FLAG_OPTIONAL | CYAML_FLAG_POINTER,
        struct env_doc, env, &env_entry_schema, 0, CYAML_UNLIMITED),
    CYAML_FIELD_END
};

static const cyaml_schema_value_t env_doc_schema = {
    CYAML_VALUE_MAPPING(CYAML_FLAG_POINTER, struct env_doc, env_doc_fields),
};

static const cyaml_config_t env_cyaml_cfg = {
    .log_fn = cyaml_log,
    .mem_fn = cyaml_mem,
    .log_level = CYAML_LOG_WARNING,
    .flags = CYAML_CFG_DEFAULT,
};

void set_environment(void)
{
    struct env_doc *doc = NULL;

    cyaml_err_t err = cyaml_load_file(CONFIG_ENV, &env_cyaml_cfg, &env_doc_schema,
                                      (cyaml_data_t **)&doc, NULL);
    if (err != CYAML_OK) {
        log_message(LOG_WARN, "Failed to load %s: %s", CONFIG_ENV, cyaml_strerror(err));
        return;
    }

    if (doc && doc->env) {
        for (unsigned i = 0; i < doc->env_count; ++i) {
            const char *key = doc->env[i].key;
            const char *val = doc->env[i].value;

            if (key && *key && val) {
                if (setenv(key, val, 1) != 0) {
                    log_message(LOG_DEBUG, "Failed setting env %s: %s", key, strerror(errno));
                } else {
                    log_message(LOG_DEBUG, "Set env %s=%s", key, val);
                }
            }
        }
    }

    cyaml_free(&env_cyaml_cfg, &env_doc_schema, doc, 0);
}