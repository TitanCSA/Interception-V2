/* SPDX-License-Identifier: LGPL-3.0-or-later */
#ifndef INTERCEPTION_V2_CSDK_H
#define INTERCEPTION_V2_CSDK_H
#include <stdint.h>
#if !defined(_WIN32) || !defined(_WIN64)
#error This SDK requires Windows x64.
#endif
#ifdef __cplusplus
#define INTERCEPTION_V2_CSDK_NOEXCEPT noexcept
extern "C" {
#else
#define INTERCEPTION_V2_CSDK_NOEXCEPT
#endif
#if defined(INTERCEPTION_V2_CSDK_BUILD)
#define INTERCEPTION_V2_CSDK_API __declspec(dllexport)
#else
#define INTERCEPTION_V2_CSDK_API __declspec(dllimport)
#endif
/* open is inert. One active session per loaded product DLL.
   call accepts length-delimited UTF-8 JSON and returns an allocated UTF-8
   response, or NULL on allocation failure. Always free with this same DLL.
   Session/operation handles are scoped to the client that returned them. */
INTERCEPTION_V2_CSDK_API uint32_t interception_v2_json_open(uint64_t *session) INTERCEPTION_V2_CSDK_NOEXCEPT;
INTERCEPTION_V2_CSDK_API uint32_t interception_v2_json_close(uint64_t session) INTERCEPTION_V2_CSDK_NOEXCEPT;
INTERCEPTION_V2_CSDK_API char *interception_v2_json_call(uint64_t session,const char *request,uint32_t length) INTERCEPTION_V2_CSDK_NOEXCEPT;
INTERCEPTION_V2_CSDK_API void interception_v2_json_free(char *response) INTERCEPTION_V2_CSDK_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef INTERCEPTION_V2_CSDK_NOEXCEPT
#endif
