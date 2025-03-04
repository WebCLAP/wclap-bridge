#include <iostream>
#define LOG_EXPR(expr) std::cout << #expr " = " << (expr) << std::endl;

#include "wasm.h"
#include "wasic.h"

#include <string>
#include <array>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>

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
	std::istream *cin;
	std::ostream *cout, *cerr;
	
	std::unique_ptr<std::ifstream> fin;
	std::unique_ptr<std::ofstream> fout, ferr;
	std::unique_ptr<std::istringstream> sin;
	
	WasiSandboxConfig() {
		// unopened - will be in error state, but won't crash
		fout = std::unique_ptr<std::ofstream>{new std::ofstream()};
		cout = fout.get();
		ferr = std::unique_ptr<std::ofstream>{new std::ofstream()};
		cerr = ferr.get();
		
		sin = std::unique_ptr<std::istringstream>{new std::istringstream("", std::ios_base::binary)};
		cin = sin.get();
	}
	
	void clearStdIn() {
		cin = nullptr;
		fin = nullptr;
		sin = nullptr;
	}
	void clearStdOut() {
		cout = nullptr;
		fout = nullptr;
	}
	void clearStdErr() {
		cerr = nullptr;
		ferr = nullptr;
	}
	
	struct DirMap {
		std::string nativePrefix, wasmPrefix;
		bool dirRead, dirWrite;
		bool fileRead, fileWrite;
	};
	std::vector<DirMap> dirMaps;
	bool addDirMap(const char *host_path, const char *guest_path, wasic_dir_perms dir_perms, wasic_file_perms file_perms) {
		dirMaps.emplace_back();
		DirMap &map = dirMaps.back();
		map.nativePrefix = host_path;
		if (!map.nativePrefix.size()) return false;
		if (map.nativePrefix.back() != '/') map.nativePrefix += "/";
		map.wasmPrefix = guest_path;
		if (!map.wasmPrefix.size() || map.wasmPrefix.back() != '/') map.wasmPrefix += "/";
		if (map.wasmPrefix[0] != '/') map.wasmPrefix = "/" + map.wasmPrefix;
		map.dirRead = dir_perms&WASIC_DIR_PERMS_READ;
		map.dirWrite = dir_perms&WASIC_DIR_PERMS_WRITE;
		map.fileRead = file_perms&WASIC_FILE_PERMS_READ;
		map.fileWrite = file_perms&WASIC_FILE_PERMS_WRITE;
		
		std::error_code error;
		auto canonical = std::filesystem::canonical(map.nativePrefix, error);
		LOG_EXPR(map.nativePrefix);
		LOG_EXPR(canonical);
		LOG_EXPR(!error); // this will fail if the file doesn't exist
		
		return true;
	}
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

	wasm_trap_t * wasip1_strlist_get(const std::vector<std::string> &list, const wasm_val_t *args, wasm_val_t *results) {
		if (!memory) return memFail();
		uint32_t indexP = args[0].of.i32;
		uint32_t bufP = args[1].of.i32;
		
		for (auto &item : config->env) {
			if (!validPointer<uint32_t>(indexP) || !validPointer<char>(bufP) || !validPointer<char>(bufP + item.size())) return boundsFail();
			wasmValue<uint32_t>(indexP) = config->env.size();
			for (size_t i = 0; i < item.size(); ++i) {
				wasmValue<char>(bufP + i) = item[i];
			}
			wasmValue<char>(bufP + item.size()) = 0;
			indexP += sizeof(uint32_t);
			bufP += item.size() + 1;
		}
		results[0].kind = WASM_I32;
		results[0].of.i32 = WASIC_SUCCESS;
		return nullptr;
	}

	wasm_trap_t * wasip1_strlist_sizes_get(const std::vector<std::string> &list, const wasm_val_t *args, wasm_val_t *results) {
		if (!memory) return memFail();
		uint32_t countP = args[0].of.i32;
		uint32_t bufSizeP = args[1].of.i32;
		if (!validPointer<uint32_t>(countP) || !validPointer<uint32_t>(bufSizeP)) return boundsFail();

		wasmValue<uint32_t>(countP) = config->env.size();
		size_t totalSize = 0;
		for (auto &v : config->env) {
			totalSize += v.size() + 1;
		}
		wasmValue<uint32_t>(bufSizeP) = totalSize;
		results[0].kind = WASM_I32;
		results[0].of.i32 = WASIC_SUCCESS;
		return nullptr;
	}

	wasm_trap_t * wasip1_args_sizes_get(const wasm_val_t *args, wasm_val_t *results) {
		return wasip1_strlist_sizes_get(config->args, args, results);
	}
	wasm_trap_t * wasip1_argss_get(const wasm_val_t *args, wasm_val_t *results) {
		return wasip1_strlist_get(config->args, args, results);
	}
	wasm_trap_t * wasip1_environ_sizes_get(const wasm_val_t *args, wasm_val_t *results) {
		return wasip1_strlist_sizes_get(config->env, args, results);
	}
	wasm_trap_t * wasip1_environ_get(const wasm_val_t *args, wasm_val_t *results) {
		return wasip1_strlist_get(config->env, args, results);
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

	wasm_trap_t * memFail() {
		return fail("WASI not linked to memory");
	}
	wasm_trap_t *boundsFail() {
		return fail("memory out of bounds");
	}
};


#undef WASIP1_FOREACH

//---------- C API ----------

wasic_config_t * wasic_config_new() {
	auto *config = new WasiSandboxConfig();
	return (wasic_config_t *)config;
}

void wasic_config_delete(wasic_config_t *) {
	auto *config = new WasiSandboxConfig();
	delete config;
}

bool wasic_config_set_argv(wasic_config_t *config, size_t argc, const char *argv[]) {
	if (!config) return false;
	auto *sandboxConfig = new WasiSandboxConfig();
	sandboxConfig->args.clear();
	for (size_t i = 0; i < argc; ++i) {
		sandboxConfig->args.emplace_back(argv[i]);
	}
	return true;
}

void wasic_config_inherit_argv(wasic_config_t *config) {
	if (!config) return;
	auto *sandboxConfig = new WasiSandboxConfig();
	sandboxConfig->args.clear();
}

bool wasic_config_set_env(wasic_config_t *config, size_t envc, const char *names[], const char *values[]) {
	if (!config) return false;
	auto *sandboxConfig = new WasiSandboxConfig();
	sandboxConfig->env.clear();
	for (size_t i = 0; i < envc; ++i) {
		std::string item = std::string(names[i]) + "=" + values[i];
		sandboxConfig->env.emplace_back(std::move(item));
	}
	return false;
}

void wasic_config_inherit_env(wasic_config_t *config) {
	if (!config) return;
	auto *sandboxConfig = new WasiSandboxConfig();
	sandboxConfig->env.clear();
}

bool wasic_config_set_stdin_file(wasic_config_t *config, const char *path) {
	if (!config || !path) return false;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	sandboxConfig->clearStdIn();
	sandboxConfig->fin = std::unique_ptr<std::ifstream>{new std::ifstream(path, std::ios_base::binary)};
	sandboxConfig->cin = sandboxConfig->fin.get();
	return sandboxConfig->fin->good();
}

void wasic_config_set_stdin_bytes(wasic_config_t *config, wasm_byte_vec_t *binary) {
	if (!config || !binary) return;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	sandboxConfig->clearStdIn();
	std::string inStr{binary->data, binary->size};
	sandboxConfig->sin = std::unique_ptr<std::istringstream>{new std::istringstream(std::move(inStr), std::ios_base::binary)};
	sandboxConfig->cin = sandboxConfig->sin.get();
}

void wasic_config_inherit_stdin(wasic_config_t *config) {
	if (!config) return;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	sandboxConfig->clearStdIn();
	sandboxConfig->cin = &std::cin;
}

bool wasic_config_set_stdout_file(wasic_config_t *config, const char *path) {
	if (!config || !path) return false;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	sandboxConfig->clearStdOut();
	sandboxConfig->fout = std::unique_ptr<std::ofstream>{new std::ofstream(path, std::ios_base::binary)};
	sandboxConfig->cout = sandboxConfig->fout.get();
	return sandboxConfig->fout->good();
}

void wasic_config_inherit_stdout(wasic_config_t *config) {
	if (!config) return;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	sandboxConfig->clearStdOut();
	sandboxConfig->cerr = &std::cout;
}

bool wasic_config_set_stderr_file(wasic_config_t *config, const char *path) {
	if (!config || !path) return false;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	sandboxConfig->clearStdErr();
	sandboxConfig->ferr = std::unique_ptr<std::ofstream>{new std::ofstream(path, std::ios_base::binary)};
	sandboxConfig->cerr = sandboxConfig->ferr.get();
	return sandboxConfig->ferr->good();
}

void wasic_config_inherit_stderr(wasic_config_t *config) {
	if (!config) return;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	sandboxConfig->clearStdErr();
	sandboxConfig->cerr = &std::cerr;
}

bool wasic_config_preopen_dir(wasic_config_t *config, const char *host_path, const char *guest_path, wasic_dir_perms dir_perms, wasic_file_perms file_perms) {
	if (!config) return false;
	auto *sandboxConfig = (WasiSandboxConfig *)config;
	return sandboxConfig->addDirMap(host_path, guest_path, dir_perms, file_perms);
}

wasic_instance_t * wasic_instance_new(const wasic_config_t *config, wasm_store_t *store) {
	if (!config || !store) return nullptr;
	auto *sandbox = new WasiSandbox((const WasiSandboxConfig *)config, store);
	return (wasic_instance_t *)sandbox;
}
void wasic_instance_delete(wasic_instance_t *instance) {
	if (!instance) return;
	auto *sandbox = (WasiSandbox *)instance;
	delete sandbox;
}

bool wasic_instance_link_memory(wasic_instance_t *instance, wasm_memory_t *memory) {
	if (!instance || !memory) return false;
	auto *sandbox = (WasiSandbox *)instance;
	return sandbox->linkMemory(memory);
}

wasm_extern_t * wasic_instance_resolve(wasic_instance_t *instance, const wasm_importtype_t *type) {
	if (!instance || !type) return nullptr;
	auto *sandbox = (WasiSandbox *)instance;
	return sandbox->resolve(type);
}
