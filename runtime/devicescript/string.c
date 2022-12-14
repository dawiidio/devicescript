#include "devs_internal.h"
#include "devs_value.h"
#include <math.h>

bool devs_is_string(devs_ctx_t *ctx, value_t v) {
    switch (devs_handle_type(v)) {
    case DEVS_HANDLE_TYPE_GC_OBJECT:
        return devs_gc_tag(devs_handle_ptr_value(ctx, v)) == DEVS_GC_TAG_STRING;
    case DEVS_HANDLE_TYPE_IMG_BUFFERISH:
        return !devs_bufferish_is_buffer(v);
    default:
        return false;
    }
}

const char *devs_string_get_utf8(devs_ctx_t *ctx, value_t v, unsigned *size) {
    switch (devs_handle_type(v)) {
    case DEVS_HANDLE_TYPE_GC_OBJECT: {
        void *ptr = devs_handle_ptr_value(ctx, v);
        if (devs_gc_tag(ptr) == DEVS_GC_TAG_STRING) {
            devs_string_t *s = ptr;
            if (size)
                *size = s->length;
            return s->data;
        }
        return NULL;
    }
    case DEVS_HANDLE_TYPE_IMG_BUFFERISH:
        return devs_bufferish_is_buffer(v) ? NULL
                                           : devs_get_static_utf8(ctx, devs_handle_value(v), size);
    default:
        return NULL;
    }
}

static value_t builtin_string(unsigned idx) {
    return devs_value_from_handle(DEVS_HANDLE_TYPE_IMG_BUFFERISH,
                                  (DEVS_STRIDX_BUILTIN << DEVS_STRIDX__SHIFT) | idx);
}

value_t devs_string_vsprintf(devs_ctx_t *ctx, const char *format, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int len = jd_vsprintf(NULL, 0, format, ap);
    // len includes final NUL; devs_string_try_alloc() allocates the final NUL, but doesn't count it
    // in its len
    devs_string_t *s = devs_string_try_alloc(ctx->gc, len - 1);
    if (s == NULL) {
        devs_runtime_failure(ctx, 60144);
        // re-run vsprintf with non-NULL dst so it executes %-s (free)
        char tmp;
        jd_vsprintf(&tmp, 1, format, ap2);
        return devs_undefined;
    } else {
        jd_vsprintf(s->data, len, format, ap2);
        return devs_value_from_gc_obj(ctx, s);
    }
}

value_t devs_string_sprintf(devs_ctx_t *ctx, const char *format, ...) {
    va_list arg;
    va_start(arg, format);
    value_t r = devs_string_vsprintf(ctx, format, arg);
    va_end(arg);
    return r;
}

value_t devs_string_from_utf8(devs_ctx_t *ctx, const uint8_t *utf8, unsigned len) {
    devs_string_t *s = devs_string_try_alloc(ctx->gc, len);
    if (s == NULL) {
        devs_runtime_failure(ctx, 60145);
        return devs_undefined;
    } else {
        memcpy(s->data, utf8, len); // TODO validate utf8
        return devs_value_from_gc_obj(ctx, s);
    }
}

static value_t buffer_to_string(devs_ctx_t *ctx, value_t v) {
    unsigned sz;
    void *data = devs_buffer_data(ctx, v, &sz);
    unsigned maxbuf = 32;
    if (sz > maxbuf) {
        return devs_string_sprintf(ctx, "[Buffer %ub: %-s...]", sz, jd_to_hex_a(data, maxbuf));
    } else {
        return devs_string_sprintf(ctx, "[Buffer %ub: %-s]", sz, jd_to_hex_a(data, sz));
    }
}

value_t devs_value_to_string(devs_ctx_t *ctx, value_t v) {
    if (devs_is_string(ctx, v))
        return v;

    switch (devs_handle_type(v)) {
    case DEVS_HANDLE_TYPE_FLOAT64_OR_NULL: {
        if (devs_is_null(v))
            return builtin_string(DEVS_BUILTIN_STRING_NULL);
        char buf[64];
        double vv = devs_value_to_double(v);
        if (isnan(vv))
            return builtin_string(DEVS_BUILTIN_STRING_NAN);
        jd_print_double(buf, vv, 7);
        return devs_string_sprintf(ctx, "%s", buf);
    }
    case DEVS_HANDLE_TYPE_SPECIAL:
        switch (devs_handle_value(v)) {
        case DEVS_SPECIAL_FALSE:
            return builtin_string(DEVS_BUILTIN_STRING_FALSE);
        case DEVS_SPECIAL_TRUE:
            return builtin_string(DEVS_BUILTIN_STRING_TRUE);
        case DEVS_SPECIAL_PKT_BUFFER:
            return builtin_string(DEVS_BUILTIN_STRING_PACKET);
        default:
            JD_PANIC();
        }
    case DEVS_HANDLE_TYPE_FIBER:
        return devs_string_sprintf(ctx, "[Fiber: %x]", devs_handle_value(v));
    case DEVS_HANDLE_TYPE_FUNCTION:
        return devs_string_sprintf(ctx, "[Function: %s]",
                                   devs_img_fun_name(ctx->img, devs_handle_value(v)));
    case DEVS_HANDLE_TYPE_GC_OBJECT:
        switch (devs_gc_tag(devs_handle_ptr_value(ctx, v))) {
        case DEVS_GC_TAG_ARRAY:
            return builtin_string(DEVS_BUILTIN_STRING_ARRAY); // TODO stringify array?
        case DEVS_GC_TAG_BUFFER:
            return buffer_to_string(ctx, v);
        case DEVS_GC_TAG_MAP:
            return builtin_string(DEVS_BUILTIN_STRING_MAP);
        case DEVS_GC_TAG_STRING:
        default:
            JD_PANIC();
        }
    case DEVS_HANDLE_TYPE_IMG_BUFFERISH:
        JD_ASSERT(devs_bufferish_is_buffer(v));
        return buffer_to_string(ctx, v);
    case DEVS_HANDLE_TYPE_ROLE:
        return devs_string_sprintf(ctx, "[Role: %s]",
                                   devs_img_role_name(ctx->img, devs_handle_value(v)));
    default:
        JD_PANIC();
    }
}

static value_t devs_value_to_string_and_pin(devs_ctx_t *ctx, value_t a) {
    if (!devs_is_string(ctx, a)) {
        value_t tmp = devs_value_to_string(ctx, a);
        devs_value_pin(ctx, tmp);
        devs_value_unpin(ctx, a);
        return tmp;
    } else {
        return a;
    }
}

value_t devs_string_concat(devs_ctx_t *ctx, value_t a, value_t b) {
    devs_value_pin(ctx, a);
    devs_value_pin(ctx, b);

    a = devs_value_to_string_and_pin(ctx, a);
    b = devs_value_to_string_and_pin(ctx, b);

    const char *ap, *bp;
    unsigned alen, blen;
    ap = devs_string_get_utf8(ctx, a, &alen);
    bp = devs_string_get_utf8(ctx, b, &blen);

    value_t r;

    if (ap == NULL || bp == NULL) {
        // strange...
        devs_runtime_failure(ctx, 60141);
        r = devs_undefined;
    } else if (alen == 0) {
        r = b;
    } else if (blen == 0) {
        r = a;
    } else {
        devs_string_t *s = devs_string_try_alloc(ctx->gc, alen + blen);
        if (s == NULL) {
            devs_runtime_failure(ctx, 60142);
            r = devs_undefined;
        } else {
            memcpy(s->data, ap, alen);
            memcpy(s->data + alen, bp, blen);
            r = devs_value_from_gc_obj(ctx, s);
        }
    }

    devs_value_unpin(ctx, a);
    devs_value_unpin(ctx, b);

    return r;
}
