#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void *shl_vm_t;
typedef void *shl_value_t;

typedef shl_value_t (*shl_native_fn)(shl_vm_t vm, int arg_count,
                                     shl_value_t *args);

typedef struct {
  void (*register_function)(shl_vm_t vm, const char *name, int expected_args,
                            shl_native_fn func);

  shl_value_t (*make_string)(shl_vm_t vm, const char *str);
  shl_value_t (*make_number)(shl_vm_t vm, double val);
  shl_value_t (*make_bool)(shl_vm_t vm, int val);
  shl_value_t (*make_null)(shl_vm_t vm);

  double (*get_number)(shl_value_t val);
  const char* (*get_string)(shl_value_t val);
  int (*get_bool)(shl_value_t val);
  int (*is_number)(shl_value_t val);
  int (*is_string)(shl_value_t val);
  int (*is_bool)(shl_value_t val);
  int (*is_null)(shl_value_t val);
  void (*release_value)(shl_vm_t vm, shl_value_t val);
} shlcppAPI;

typedef shlcppAPI ShellLiteAPI;

// Every plugin must implement this entry point:
// EXPORT void shlcpp_plugin_init(shlcppAPI* api, shl_vm_t vm);

#ifdef __cplusplus
}
#endif
