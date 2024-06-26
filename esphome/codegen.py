# Base file for all codegen-related imports
# All integrations should have a line in the import section like this
#
# >>> import esphome.codegen as cg
#
# Integrations should specifically *NOT* import directly from the
# other helper modules (cpp_generator etc) directly if they don't
# want to break suddenly due to a rename (this file will get backports for features).

# pylint: disable=unused-import
from esphome.cpp_generator import (  # noqa
    Expression,
    RawExpression,
    RawStatement,
    TemplateArguments,
    StructInitializer,
    ArrayInitializer,
    safe_exp,
    Statement,
    LineComment,
    progmem_array,
    static_const_array,
    statement,
    variable,
    with_local_variable,
    new_variable,
    Pvariable,
    new_Pvariable,
    add,
    add_global,
    add_library,
    add_build_flag,
    add_define,
    add_platformio_option,
    get_variable,
    get_variable_with_full_id,
    process_lambda,
    is_template,
    templatable,
    MockObj,
    MockObjClass,
)
from esphome.cpp_helpers import (  # noqa
    gpio_pin_expression,
    register_component,
    build_registry_entry,
    build_registry_list,
    extract_registry_entry_config,
    register_parented,
    past_safe_mode,
)
from esphome.cpp_types import (  # noqa
    global_ns,
    void,
    nullptr,
    float_,
    double,
    bool_,
    int_,
    std_ns,
    std_shared_ptr,
    std_string,
    std_string_ref,
    std_vector,
    uint8,
    uint16,
    uint32,
    uint64,
    int16,
    int32,
    int64,
    size_t,
    const_char_ptr,
    NAN,
    esphome_ns,
    App,
    EntityBase,
    Component,
    ComponentPtr,
    PollingComponent,
    Application,
    optional,
    arduino_json_ns,
    JsonObject,
    JsonObjectConst,
    Controller,
    GPIOPin,
    InternalGPIOPin,
    gpio_Flags,
    EntityCategory,
    Parented,
    ESPTime,
)
