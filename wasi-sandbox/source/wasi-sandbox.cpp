#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;

#include "wasi.h"

#include <string>
#include <array>

// Helper functions
static bool nameEquals(const wasm_name_t *name, const char *cName) {
	if (name->size != std::strlen(cName)) return false;
	for (size_t i = 0; i < name->size; ++i) {
		if (name->data[i] != cName[i]) return false;
	}
	return true;
}
static char typeCode(wasm_valkind_t k) {
	if (k < 4) {
		return "iIfF"[k];
	} else if (k == WASM_EXTERNREF) {
		return 'X';
	} else if (k == WASM_FUNCREF) {
		return '$';
	}
	return '?';
}
static char typeCode(const wasm_valtype_t *t) {
	return typeCode(wasm_valtype_kind(t));
}
static std::string typeCode(const wasm_valtype_vec_t *typeList) {
	std::string result(typeList->size, '.');
	for (size_t i = 0; i < typeList->size; ++i) {
		result[i] = typeCode(typeList->data[i]);
	}
	return result;
}
static bool typeCodeEquals(const wasm_valtype_vec_t *typeList, const char *code) {
	size_t count = std::strlen(code);
	if (typeList->size != count) return false;
	for (size_t i = 0; i < count; ++i) {
		if (typeCode(typeList->data[i]) != code[i]) return false;
	}
	return true;
}

// DEBUG
std::ostream& operator<<(std::ostream& os, const wasm_byte_vec_t &name) {
	for (size_t i = 0; i < name.size; ++i) {
		os << name.data[i];
	}
	return os;
}

// List of functions adapted from https://wasix.org/docs/api-reference and https://github.com/nodejs/uvwasi?tab=readme-ov-file#system-calls
#define WASIP1_FOREACH(MACRO_FN) \
	MACRO_FN(args_get, ii, i);\
	MACRO_FN(args_sizes_get, ii, i);\
	MACRO_FN(clock_res_get, iI, i);\
	MACRO_FN(clock_time_get, iII, i);\
	MACRO_FN(environ_get, ii, i);\
	MACRO_FN(environ_sizes_get, ii, i);\
	MACRO_FN(fd_advise, iIIi, i);\
	MACRO_FN(fd_allocate, iII, i);\
	MACRO_FN(fd_close, i, i);\
	MACRO_FN(fd_datasync, i, i);\
	MACRO_FN(fd_fdstat_get, ii, i);\
	MACRO_FN(fd_fdstat_set_flags, ii, i);\
	MACRO_FN(fd_fdstat_set_rights, iII, i);\
	MACRO_FN(fd_filestat_get, ii, i);\
	MACRO_FN(fd_filestat_set_size, iI, i);\
	MACRO_FN(fd_filestat_set_times, iIIi, i);\
	MACRO_FN(fd_pread, iiiIi, i);\
	MACRO_FN(fd_prestat_get, ii, i);\
	MACRO_FN(fd_prestat_dir_name, iii, i);\
	MACRO_FN(fd_pwrite, iiiIi, i);\
	MACRO_FN(fd_read, iiii, i);\
	MACRO_FN(fd_readdir, iiiIi, i);\
	MACRO_FN(fd_renumber, ii, i);\
	MACRO_FN(fd_seek, iIii, i);\
	MACRO_FN(fd_sync, i, i);\
	MACRO_FN(fd_tell, iI, i);\
	MACRO_FN(fd_write, iiii, i);\
	MACRO_FN(path_create_directory, iii, i);\
	MACRO_FN(path_filestat_get, iiiii, i);\
	MACRO_FN(path_filestat_set_times, iiiiIIi, i);\
	MACRO_FN(path_link, iiiiiii, i);\
	MACRO_FN(path_open, iiiiiIIii, i);\
	MACRO_FN(path_readlink, iiiiii, i);\
	MACRO_FN(path_remove_directory, iii, i);\
	MACRO_FN(path_rename, iiiiii, i);\
	MACRO_FN(path_symlink, iiiii, i);\
	MACRO_FN(path_unlink_file, iii, i);\
	MACRO_FN(poll_oneoff, iiii, i);\
	MACRO_FN(proc_exit, i, i);\
	MACRO_FN(proc_raise, i, i);\
	MACRO_FN(random_get, ii, i);\
	MACRO_FN(sched_yield, , i);\
	MACRO_FN(sock_accept, iiii, i);\
	MACRO_FN(sock_recv, iiiiii, i);\
	MACRO_FN(sock_send, iiiii, i);\
	MACRO_FN(sock_shutdown, ii, i);
	
struct WasiSandboxConfig {
	std::vector<std::string> env, args;
};

struct WasiSandboxBase {
	wasm_store_t *store;

	WasiSandboxBase(wasm_store_t *store) : store(store) {}

	wasm_trap_t * fail(const char *message) {
		wasm_message_t wasmMessage;
		wasm_byte_vec_new(&wasmMessage, std::strlen(message) + 1, message);
		auto *trap = wasm_trap_new(store, &wasmMessage);
		wasm_byte_vec_delete(&wasmMessage);
		return trap;
	}

#define FAIL_NOT_IMPLEMENTED(fn_name, _p, _r) \
	wasm_trap_t * wasip1_##fn_name(const wasm_val_t *args, wasm_val_t *results) { \
		return fail("WASI: " #fn_name "() not implemented"); \
	}
	WASIP1_FOREACH(FAIL_NOT_IMPLEMENTED);
#undef FAIL_NOT_IMPLEMENTED
};

struct WasiSandbox : private WasiSandboxBase {
	WasiSandbox(const WasiSandboxConfig *config, wasm_store_t *store) : WasiSandboxBase(store), config(config) {}
	
	template<class T>
	bool validPointer(uint32_t wasmP) {
		if ((size_t)wasmP + sizeof(T) > wasm_memory_data_size(memory)) return false;
		return true;
	}
	template<class T>
	T & wasmValue(uint32_t wasmP) {
		return *(T *)(wasm_memory_data(memory) + wasmP);
	}
	
	wasm_trap_t * wasip1_environ_sizes_get(const wasm_val_t *args, wasm_val_t *results) {
		if (!memory) return fail("WASI not linked to memory");
		uint32_t countP = args[0].of.i32;
		uint32_t bufSizeP = args[1].of.i32;
		if (!validPointer<uint32_t>(countP)) return fail("memory out of bounds");
		if (!validPointer<uint32_t>(bufSizeP)) return fail("memory out of bounds");

		wasmValue<uint32_t>(countP) = config->env.size();
		size_t totalSize = 0;
LOG_EXPR(config);
LOG_EXPR(config->env.size());
		for (auto &v : config->env) {
LOG_EXPR(v);
			totalSize += v.size() + 1;
LOG_EXPR(totalSize);
		}
LOG_EXPR(bufSizeP);
return fail("Hello A");
		wasmValue<uint32_t>(bufSizeP) = totalSize;
return fail("Hello B");
		results[0].kind = WASM_I32;
		results[0].of.i32 = 0;


		return nullptr;
	}

#define FORWARD_TO_METHOD(fn_name, _p, _r) \
	static wasm_trap_t * static_wasip1_##fn_name(void *env, const wasm_val_vec_t* args, wasm_val_vec_t* results) { \
		auto *self = (WasiSandbox *)env; \
		return self->wasip1_##fn_name(args->data, results->data); \
	}
	WASIP1_FOREACH(FORWARD_TO_METHOD);
#undef FORWARD_TO_METHOD

	wasm_extern_t * resolve(const wasm_importtype_t *type) {
		auto *module = wasm_importtype_module(type);
		auto *name = wasm_importtype_name(type);
		auto *eType = wasm_importtype_type(type);
		
		const wasm_functype_t *funcType = wasm_externtype_as_functype_const(eType);
		if (funcType && nameEquals(module, "wasi_snapshot_preview1")) {
			const wasm_valtype_vec_t *params = wasm_functype_params(funcType);
			const wasm_valtype_vec_t *results = wasm_functype_results(funcType);
#define MATCH_NAME(fn_name, fn_paramsCode, fn_resultsCode) \
			if (nameEquals(name, #fn_name) && typeCodeEquals(params, #fn_paramsCode) && typeCodeEquals(results, #fn_resultsCode)) { \
				auto *func = wasm_func_new_with_env(store, funcType, &WasiSandbox::static_wasip1_##fn_name, this, nullptr); \
				return wasm_func_as_extern(func); \
			}
			WASIP1_FOREACH(MATCH_NAME);
#undef MATCH_NAME
			// TODO: somehow return details of why it didn't match
			//LOG_EXPR(*name) \
			//LOG_EXPR(typeCode(params))
			//LOG_EXPR(typeCode(results))
		}
		
		return nullptr;
	}
	
	bool hasMemory() const {
		return memory;
	}
	
	bool linkMemory(wasm_memory_t *m) {
		if (memory) return false;
		return memory = m;
	}
private:
	const WasiSandboxConfig *config;
	wasm_memory_t *memory;
};


#undef WASIP1_FOREACH

//---------- C API ----------

#import "wasi.h"

wasi_config_t * wasi_config_new() {
	auto *config = new WasiSandboxConfig();
	return (wasi_config_t *)config;
}

void wasi_config_delete() {
	auto *config = new WasiSandboxConfig();
	delete config;
}

wasi_instance_t * wasi_instance_new(const wasi_config_t *config, wasm_store_t *store) {
	auto *sandbox = new WasiSandbox((const WasiSandboxConfig *)config, store);
	return (wasi_instance_t *)sandbox;
}
void wasi_instance_delete(wasi_instance_t *instance) {
	auto *sandbox = (WasiSandbox *)instance;
	delete sandbox;
}

bool wasi_instance_link_memory(wasi_instance_t *instance, wasm_memory_t *memory) {
	if (!instance || !memory) return false;
	auto *sandbox = (WasiSandbox *)instance;
	return sandbox->linkMemory(memory);
}

wasm_extern_t * wasi_instance_resolve(wasi_instance_t *instance, const wasm_importtype_t *type) {
	if (!instance || !type) return nullptr;
	auto *sandbox = (WasiSandbox *)instance;
	return sandbox->resolve(type);
}
