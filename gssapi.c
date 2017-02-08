#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

#include "emacs-module.h"
#include "gssapi/gssapi.h"
#include "gssapi/gssapi_krb5.h"

int plugin_is_GPL_compatible;

#if 0
static void message(emacs_env *env, char *fmt, ...)
{
    int size = 0;
    char *p = NULL;
    va_list ap;

    va_start(ap, fmt);
    size = vsnprintf(p, size, fmt, ap);
    va_end(ap);

    size++;
    
    p = malloc(size);
    if(p == NULL) {
        // Out of memory, simply don't print anything
        return;
    }

    va_start(ap, fmt);
    size = vsnprintf(p, size, fmt, ap);
    va_end(ap);
    if(size < 0) {
        free(p);
        return;
    }

    emacs_value Qmessage = env->intern(env, "message");
    emacs_value args[] = { env->make_string(env, p, size) };
    env->funcall(env, Qmessage, 1, args);

    free(p);
}
#endif

static void bind_function(emacs_env *env, char *name, emacs_value Sfun)
{
    emacs_value Qfset = env->intern(env, "fset");
    emacs_value Qsym = env->intern(env, name);
    emacs_value args[] = { Qsym, Sfun };
    env->funcall(env, Qfset, 2, args);
}

static void provide_module(emacs_env *env, const char *feature)
{
  emacs_value Qfeat = env->intern (env, feature);
  emacs_value Qprovide = env->intern (env, "provide");
  emacs_value args[] = { Qfeat };
  env->funcall (env, Qprovide, 1, args);
}

static void throw_error(emacs_env *env, char *message) {
    emacs_value error_sym = env->intern(env, "error");
    emacs_value message_value = env->make_string(env, message, strlen(message));
    env->funcall(env, error_sym, 1, &message_value);
}

static emacs_value xcar(emacs_env *env, emacs_value obj) {
    emacs_value Qcar = env->intern(env, "car");
    emacs_value args[] = { obj };
    return env->funcall(env, Qcar, 1, args);
}

static emacs_value xcdr(emacs_env *env, emacs_value obj) {
    emacs_value Qcdr = env->intern(env, "cdr");
    emacs_value args[] = { obj };
    return env->funcall(env, Qcdr, 1, args);
}

static emacs_value make_array(emacs_env *env, void *ptr, size_t len)
{
    unsigned char *array_as_char = ptr;
    emacs_value Qmake_vector = env->intern(env, "make-vector");
    emacs_value args[] = { env->make_integer(env, len), env->make_integer(env, 0) };
    emacs_value array = env->funcall(env, Qmake_vector, 2, args);
    for(size_t i = 0 ; i < len ; i++) {
        env->vec_set(env, array, i, env->make_integer(env, array_as_char[i]));
    }
    return array;
}

static void release_name(void *name_ptr)
{
    gss_name_t name = name_ptr;
    OM_uint32 minor;
    OM_uint32 result = gss_release_name(&minor, &name);
    if(GSS_ERROR(result)) {
        abort();
    }
}

static void free_context(void *context_ptr)
{
    gss_ctx_id_t *context = context_ptr;
    gss_buffer_desc output;
    OM_uint32 minor;
    OM_uint32 result = gss_delete_sec_context(&minor, context, &output);
    if(GSS_ERROR(result)) {
        abort();
    }
}

static emacs_value Fgssapi_internal_import_name(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
    // Deal with unused variable warning
    (void)data;

    if(nargs != 2) {
        throw_error(env, "Wrong number of arguments");
        return env->intern(env, "nil");
    }
    emacs_value name = args[0];
    emacs_value type = args[1];

    gss_OID_desc *gss_type;
    if(env->eq(env, type, env->intern(env, ":user-name"))) {
        gss_type = GSS_C_NT_USER_NAME;
    }
    else if(env->eq(env, type, env->intern(env, ":machine-uid-name"))) {
        gss_type = GSS_C_NT_MACHINE_UID_NAME;
    }
    else if(env->eq(env, type, env->intern(env, ":string-uid-name"))) {
        gss_type = GSS_C_NT_STRING_UID_NAME;
    }
    else if(env->eq(env, type, env->intern(env, ":hostbased-service"))) {
        gss_type = GSS_C_NT_HOSTBASED_SERVICE;
    }
    else {
        throw_error(env, "illegal type");
        return env->intern(env, "nil");
    }

    ptrdiff_t size;
    env->copy_string_contents(env, name, NULL, &size);
    char *buf = malloc(size);
    if(!env->copy_string_contents(env, name, buf, &size)) {
        throw_error(env, "unable to copy string");
        return env->intern(env, "nil");
    }

    gss_name_t output_name;
    OM_uint32 minor;
    gss_buffer_desc name_buf;
    name_buf.value = buf;
    name_buf.length = strlen(buf);
    OM_uint32 result = gss_import_name(&minor, &name_buf, gss_type, &output_name);
    if(GSS_ERROR(result)) {
        throw_error(env, "gss error");
        return env->intern(env, "nil");
    }

    free(buf);

    return env->make_user_ptr(env, release_name, output_name);
}

static emacs_value Fgssapi_internal_name_to_string(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
    (void)data;

    if(nargs != 1) {
        throw_error(env, "Wrong number of arguments");
        return env->intern(env, "nil");
    }
    emacs_value name = args[0];

    gss_buffer_desc buffer;
    gss_OID type;

    OM_uint32 minor;
    OM_uint32 result = gss_display_name(&minor, env->get_user_ptr(env, name), &buffer, &type);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error reading name");
        return env->intern(env, "nil");
    }

    emacs_value ret = env->make_string(env, buffer.value, buffer.length);
    result = gss_release_buffer(&minor, &buffer);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error releasing buffer");
        return env->intern(env, "nil");
    }
    result = gss_release_oid(&minor, &type);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error releasing type");
        return env->intern(env, "nil");
    }

    return ret;
}

static OM_uint32 make_flags(emacs_env *env, emacs_value flags)
{
    emacs_value sym_deleg = env->intern(env, ":deleg");
    emacs_value sym_mutual = env->intern(env, ":mutual");
    emacs_value sym_replay = env->intern(env, ":replay");
    emacs_value sym_sequence = env->intern(env, ":sequence");
    emacs_value sym_conf = env->intern(env, ":conf");
    emacs_value sym_integ = env->intern(env, ":integ");
    emacs_value sym_anon = env->intern(env, ":anon");
    
    OM_uint32 result = 0;
    emacs_value curr = flags;
    while(env->is_not_nil(env, curr)) {
        emacs_value v = xcar(env, curr);
        if(env->eq(env, v, sym_deleg)) {
            result |= GSS_C_DELEG_FLAG;
        }
        else if(env->eq(env, v, sym_mutual)) {
            result |= GSS_C_MUTUAL_FLAG;
        }
        else if(env->eq(env, v, sym_replay)) {
            result |= GSS_C_REPLAY_FLAG;
        }
        else if(env->eq(env, v, sym_sequence)) {
            result |= GSS_C_SEQUENCE_FLAG;
        }
        else if(env->eq(env, v, sym_conf)) {
            result |= GSS_C_CONF_FLAG;
        }
        else if(env->eq(env, v, sym_integ)) {
            result |= GSS_C_INTEG_FLAG;
        }
        else if(env->eq(env, v, sym_anon)) {
            result |= GSS_C_ANON_FLAG;
        }
        curr = xcdr(env, curr);
    }
    return result;
}

static emacs_value lisp_push(emacs_env *env, emacs_value list, emacs_value v)
{
   emacs_value sym_cons = env->intern(env, "cons");
   emacs_value args[] = { v, list };
   return env->funcall(env, sym_cons, 2, args);
}

static emacs_value parse_flags(emacs_env *env, OM_uint32 flags)
{
    emacs_value res = env->intern(env, "nil");
    if(flags & GSS_C_DELEG_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":deleg"));
    }
    if(flags & GSS_C_MUTUAL_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":mutual"));
    }
    if(flags & GSS_C_REPLAY_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":replay"));
    }
    if(flags & GSS_C_SEQUENCE_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":sequence"));
    }
    if(flags & GSS_C_CONF_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":conf"));
    }
    if(flags & GSS_C_INTEG_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":integ"));
    }
    if(flags & GSS_C_ANON_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":anon"));
    }
    if(flags & GSS_C_PROT_READY_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":prot-ready"));
    }
    if(flags & GSS_C_TRANS_FLAG) {
        res = lisp_push(env, res, env->intern(env, ":trans"));
    }
    return res;
}

static gss_ctx_id_t make_context_ref(emacs_env *env, emacs_value context)
{
    if(env->is_not_nil(env, context)) {
        return env->get_user_ptr(env, context);
    }
    else {
        return GSS_C_NO_CONTEXT;
    }
}

static gss_buffer_desc make_input_token(emacs_env *env, emacs_value content)
{
    gss_buffer_desc input_token;
    if(env->is_not_nil(env, content)) {
        size_t input_token_length = env->vec_size(env, content);
        char *input_token_buf = malloc(input_token_length);
        for(size_t i = 0 ; i < input_token_length ; i++) {
            input_token_buf[i] = env->extract_integer(env, env->vec_get(env, content, i));
        }
        input_token.value = input_token_buf;
        input_token.length = input_token_length;
    }
    else {
        input_token.value = "";
        input_token.length = 0;
    }

    return input_token;
}

static void free_input_token(gss_buffer_desc *token)
{
    free(token->value);
}

static emacs_value Fgssapi_internal_init_sec_context(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
    (void)data;

    if(nargs != 5) {
        throw_error(env, "Wrong number of arguments");
        return env->intern(env, "nil");
    }
    emacs_value target = args[0];
    emacs_value flags = args[1];
    emacs_value context = args[2];
    emacs_value time_req = args[3];
    emacs_value content = args[4];

    gss_ctx_id_t context_handle = make_context_ref(env, context);
    gss_buffer_desc input_token = make_input_token(env, content);
    gss_OID actual_mech_type;
    gss_buffer_desc output_token;
    OM_uint32 ret_flags;
    OM_uint32 time_rec;

    OM_uint32 minor;
    OM_uint32 result = gss_init_sec_context(&minor, NULL, &context_handle, env->get_user_ptr(env, target),
                                            GSS_C_NO_OID, make_flags(env, flags),
                                            env->extract_integer(env, time_req), GSS_C_NO_CHANNEL_BINDINGS,
                                            &input_token, &actual_mech_type, &output_token, &ret_flags, &time_rec);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error from init_sec_context");
        return env->intern(env, "nil");
    }

    emacs_value sym_nil = env->intern(env, "nil");
    emacs_value sym_t = env->intern(env, "t");
    emacs_value result_list[] = { result & GSS_S_CONTINUE_NEEDED ? sym_t : sym_nil,
                                  context_handle == NULL ? sym_nil : env->make_user_ptr(env, free_context, context_handle),
                                  output_token.length == 0 ? sym_nil : make_array(env, output_token.value, output_token.length),
                                  parse_flags(env, ret_flags) };

    result = gss_release_buffer(&minor, &output_token);
    if(GSS_ERROR(result)) {
        abort();
    }

    return env->funcall(env, env->intern(env, "list"), 4, result_list);
}

static emacs_value Fgssapi_internal_accept_sec_context(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
    (void)data;

    if(nargs != 2) {
        throw_error(env, "Wrong number of arguments");
        return env->intern(env, "nil");
    }
    emacs_value content = args[0];
    emacs_value context = args[1];

    gss_ctx_id_t context_handle = make_context_ref(env, context);
    gss_buffer_desc input_token = make_input_token(env, content);
    gss_name_t src_name;
    gss_buffer_desc output_token;
    OM_uint32 ret_flags;
    OM_uint32 time_rec;
    gss_cred_id_t output_cred_handle;

    OM_uint32 minor;
    OM_uint32 result = gss_accept_sec_context(&minor, &context_handle, NULL, &input_token, GSS_C_NO_CHANNEL_BINDINGS,
                                              &src_name, NULL, &output_token, &ret_flags, &time_rec, &output_cred_handle);
    free_input_token(&input_token);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error when calling gss_accept_sec_context");
        return env->intern(env, "nil");
    }

    emacs_value sym_nil = env->intern(env, "nil");
    emacs_value sym_t = env->intern(env, "t");
    emacs_value result_list[] = { result & GSS_S_CONTINUE_NEEDED ? sym_t : sym_nil,
                                  context_handle == NULL ? sym_nil : env->make_user_ptr(env, free_context, context_handle),
                                  env->make_user_ptr(env, release_name, src_name),
                                  output_token.length == 0 ? sym_nil : make_array(env, output_token.value, output_token.length),
                                  parse_flags(env, ret_flags),
                                  env->make_integer(env, time_rec),
                                  sym_nil };

    result = gss_release_buffer(&minor, &output_token);
    if(GSS_ERROR(result)) {
        abort();
    }

    return env->funcall(env, env->intern(env, "list"), 7, result_list);
}

static emacs_value Fregister_acceptor_identity(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
    (void)data;

    if(nargs != 1) {
        throw_error(env, "Wrong number of arguments");
        return env->intern(env, "nil");
    }
    emacs_value filename = args[0];

    ptrdiff_t size;
    env->copy_string_contents(env, filename, NULL, &size);
    char *buf = malloc(size);
    if(!env->copy_string_contents(env, filename, buf, &size)) {
        throw_error(env, "unable to copy string");
        return env->intern(env, "nil");
    }
    
    OM_uint32 result = gsskrb5_register_acceptor_identity(buf);
    free(buf);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error loading file");
    }

    return env->intern(env, "nil");
}

static emacs_value Fwrap(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
    (void)data;

    if(nargs != 3) {
        throw_error(env, "Wrong number of arguments");
        return env->intern(env, "nil");
    }
    emacs_value context = args[0];
    emacs_value buffer = args[1];
    emacs_value conf = args[2];

    gss_buffer_desc buffer_desc = make_input_token(env, buffer);
    int conf_state;
    gss_buffer_desc output_desc;

    OM_uint32 minor;
    OM_uint32 result = gss_wrap(&minor,
                                env->get_user_ptr(env, context),
                                env->is_not_nil(env, conf) ? 1 : 0,
                                GSS_C_QOP_DEFAULT,
                                &buffer_desc,
                                &conf_state,
                                &output_desc);
    free_input_token(&buffer_desc);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error calling gss_wrap");
        return env->intern(env, "nil");
    }

    emacs_value ret = make_array(env, output_desc.value, output_desc.length);

    result = gss_release_buffer(&minor, &output_desc);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error releasing buffer");
        return env->intern(env, "nil");
    }

    emacs_value Qlist = env->intern(env, "list");
    emacs_value result_list[] = { ret, conf_state ? env->intern(env, "t") : env->intern(env, "nil") };
    return env->funcall(env, Qlist, 2, result_list);
}

static emacs_value Funwrap(emacs_env *env, ptrdiff_t nargs, emacs_value *args, void *data)
{
    (void)data;

    if(nargs != 2) {
        throw_error(env, "Wrong number of arguments");
        return env->intern(env, "nil");
    }
    emacs_value context = args[0];
    emacs_value buffer = args[1];

    gss_buffer_desc buffer_desc = make_input_token(env, buffer);
    int conf_state;
    gss_qop_t qop_state;
    gss_buffer_desc output_desc;

    OM_uint32 minor;
    OM_uint32 result = gss_unwrap(&minor,
                                  env->get_user_ptr(env, context),
                                  &buffer_desc,
                                  &output_desc,
                                  &conf_state,
                                  &qop_state);
    free_input_token(&buffer_desc);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error calling gss_wrap");
        return env->intern(env, "nil");
    }

    emacs_value ret = make_array(env, output_desc.value, output_desc.length);

    result = gss_release_buffer(&minor, &output_desc);
    if(GSS_ERROR(result)) {
        throw_error(env, "Error releasing buffer");
        return env->intern(env, "nil");
    }

    emacs_value Qlist = env->intern(env, "list");
    emacs_value result_list[] = { ret, conf_state ? env->intern(env, "t") : env->intern(env, "nil") };
    return env->funcall(env, Qlist, 2, result_list);
}

int emacs_module_init(struct emacs_runtime *ert)
{
    emacs_env *env = ert->get_environment(ert);

    emacs_value make_name_fn = env->make_function(env, 2, 2, Fgssapi_internal_import_name, "integrate gss_import_name", NULL);
    bind_function(env, "gss--internal-import-name", make_name_fn);

    emacs_value name_to_string_fn = env->make_function(env, 1, 1, Fgssapi_internal_name_to_string, "integrate gss_display_name", NULL);
    bind_function(env, "gss--internal-name-to-string", name_to_string_fn);

    emacs_value init_sec_context_fn = env->make_function(env, 5, 5, Fgssapi_internal_init_sec_context, "integrate gss_init_sec_context", NULL);
    bind_function(env, "gss--internal-init-sec-context", init_sec_context_fn);

    emacs_value accept_sec_context_fn = env->make_function(env, 2, 2, Fgssapi_internal_accept_sec_context, "integrate gss_accept_sec_context", NULL);
    bind_function(env, "gss--internal-accept-sec-context", accept_sec_context_fn);

    emacs_value register_acceptor_identity_fn = env->make_function(env, 1, 1, Fregister_acceptor_identity, "integrate krb5_gss_register_acceptor_identity", NULL);
    bind_function(env, "gss--internal-krb5-register-acceptor-identity", register_acceptor_identity_fn);

    emacs_value wrap_fn = env->make_function(env, 3, 3, Fwrap, "integrates gss_wrap", NULL);
    bind_function(env, "gss--internal-wrap", wrap_fn);

    emacs_value unwrap_fn = env->make_function(env, 2, 2, Funwrap, "integrates gss_unwrap", NULL);
    bind_function(env, "gss--internal-unwrap", unwrap_fn);

    provide_module(env, "emacs-gssapi");

    return 0;
}
