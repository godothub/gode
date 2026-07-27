#ifndef GODE_UTILS_NAPI_ERROR_UTILS_H
#define GODE_UTILS_NAPI_ERROR_UTILS_H

#include <napi.h>

#include <string>

namespace v8 {
class Isolate;
class TryCatch;
} // namespace v8

namespace gode {

std::string js_error_to_string(Napi::Value value);
std::string js_error_to_string(const Napi::Error &error);
void log_js_error(const std::string &context, const std::string &message);
void log_v8_exception(v8::Isolate *isolate, v8::TryCatch &try_catch, const std::string &context);
bool log_and_clear_pending_js_exception(Napi::Env env, const std::string &context);

} // namespace gode

#endif // GODE_UTILS_NAPI_ERROR_UTILS_H
