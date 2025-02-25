#pragma once

#import "wasm.h"

// DEBUG
std::ostream& operator<<(std::ostream& os, const wasm_byte_vec_t &name) {
	for (size_t i = 0; i < name.size; ++i) {
		os << name.data[i];
	}
	return os;
}

// List of functions adapted from https://github.com/nodejs/uvwasi?tab=readme-ov-file#system-calls
#define WASIP1_FOREACH(MACRO_FN) \
	MACRO_FN(args_get);\
	MACRO_FN(args_sizes_get);\
	MACRO_FN(clock_res_get);\
	MACRO_FN(clock_time_get);\
	MACRO_FN(environ_get);\
	MACRO_FN(environ_sizes_get);\
	MACRO_FN(fd_advise);\
	MACRO_FN(fd_allocate);\
	MACRO_FN(fd_close);\
	MACRO_FN(fd_datasync);\
	MACRO_FN(fd_fdstat_get);\
	MACRO_FN(fd_fdstat_set_flags);\
	MACRO_FN(fd_fdstat_set_rights);\
	MACRO_FN(fd_filestat_get);\
	MACRO_FN(fd_filestat_set_size);\
	MACRO_FN(fd_filestat_set_times);\
	MACRO_FN(fd_pread);\
	MACRO_FN(fd_prestat_get);\
	MACRO_FN(fd_prestat_dir_name);\
	MACRO_FN(fd_pwrite);\
	MACRO_FN(fd_read);\
	MACRO_FN(fd_readdir);\
	MACRO_FN(fd_renumber);\
	MACRO_FN(fd_seek);\
	MACRO_FN(fd_sync);\
	MACRO_FN(fd_tell);\
	MACRO_FN(fd_write);\
	MACRO_FN(path_create_directory);\
	MACRO_FN(path_filestat_get);\
	MACRO_FN(path_filestat_set_times);\
	MACRO_FN(path_link);\
	MACRO_FN(path_open);\
	MACRO_FN(path_readlink);\
	MACRO_FN(path_remove_directory);\
	MACRO_FN(path_rename);\
	MACRO_FN(path_symlink);\
	MACRO_FN(path_unlink_file);\
	MACRO_FN(poll_oneoff);\
	MACRO_FN(proc_exit);\
	MACRO_FN(proc_raise);\
	MACRO_FN(random_get);\
	MACRO_FN(sched_yield);\
	MACRO_FN(sock_recv);\
	MACRO_FN(sock_send);\
	MACRO_FN(sock_shutdown);

struct WasiSandbox {
	WasiSandbox(wasm_store_t *store) : store(store) {}
	
	wasm_trap_t * fail(const char *message) {
		wasm_message_t wasmMessage;
		wasm_byte_vec_new(&wasmMessage, std::strlen(message) + 1, message);
		auto *trap = wasm_trap_new(store, &wasmMessage);
		wasm_byte_vec_delete(&wasmMessage);
		return trap;
	}

	wasm_extern_t * resolve(wasm_importtype_t *type) {
		auto *module = wasm_importtype_module(type);
		auto *name = wasm_importtype_name(type);
		auto *eType = wasm_importtype_type(type);
		
		const wasm_functype_t *funcType = wasm_externtype_as_functype_const(eType);
		if (funcType && nameEquals(module, "wasi_snapshot_preview1")) {
#define MATCH_NAME(fn_name) \
			if (nameEquals(name, #fn_name)) {\
				auto *func = wasm_func_new_with_env(store, funcType, wasip1_##fn_name, this, nullptr); \
				return wasm_func_as_extern(func); \
			}
			WASIP1_FOREACH(MATCH_NAME);
#undef MATCH_NAME
		}
		
		return nullptr;
	}

#define FAIL_NOT_IMPLEMENTED(fn_name) \
	static wasm_trap_t * wasip1_##fn_name(void *env, const wasm_val_vec_t* args, wasm_val_vec_t* results) { \
		LOG_EXPR(&wasip1_##fn_name); \
		auto *self = (WasiSandbox *)env; \
		LOG_EXPR(self); \
		return self->fail("WASI: " #fn_name "() not implemented"); \
	}
	WASIP1_FOREACH(FAIL_NOT_IMPLEMENTED);
#undef FAIL_NOT_IMPLEMENTED

	void fileRoot(const char *fileRoot) {
		fileRootScope = fileRoot;
	}

private:
	// non-owning reference
	wasm_store_t *store;
	std::string fileRootScope;

	static bool nameEquals(const wasm_name_t *name, const char *cName) {
		if (name->size != std::strlen(cName)) return false;
		for (size_t i = 0; i < name->size; ++i) {
			if (name->data[i] != cName[i]) return false;
		}
		return true;
	}
};

#undef WASIP1_FOREACH
