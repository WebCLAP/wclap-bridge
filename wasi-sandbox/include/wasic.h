/** C API for WASI
 *
 * Originally based on Wasmtime's `wasi.h`. Prefix changed to `wasic_*` to avoid name-clash, and `wasic_instance_*` added for import resolution.
 */

#ifndef WASIC_H
#define WASIC_H

#include "wasm.h"
#include <stdint.h>

#ifndef WASIC_API_EXTERN
#ifdef _WIN32
#define WASIC_API_EXTERN __declspec(dllimport)
#else
#define WASIC_API_EXTERN
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define own

#define WASIC_DECLARE_OWN(name)                                                 \
  typedef struct wasic_##name##_t wasic_##name##_t;                              \
  WASIC_API_EXTERN void wasic_##name##_delete(own wasic_##name##_t *);

/**
 * \typedef wasic_config_t
 * \brief Convenience alias for #wasic_config_t
 *
 * \struct wasic_config_t
 * \brief TODO
 *
 * \fn void wasic_config_delete(wasic_config_t *);
 * \brief Deletes a configuration object.
 */
WASIC_DECLARE_OWN(config)

/**
 * \brief Creates a new empty configuration object.
 *
 * The caller is expected to deallocate the returned configuration
 */
WASIC_API_EXTERN own wasic_config_t *wasic_config_new();

/**
 * \brief Sets the argv list for this configuration object.
 *
 * By default WASI programs have an empty argv list, but this can be used to
 * explicitly specify what the argv list for the program is.
 *
 * The arguments are copied into the `config` object as part of this function
 * call, so the `argv` pointer only needs to stay alive for this function call.
 *
 * This function returns `true` if all arguments were registered successfully,
 * or `false` if an argument was not valid UTF-8.
 */
WASIC_API_EXTERN bool wasic_config_set_argv(wasic_config_t *config, size_t argc,
                                          const char *argv[]);

/**
 * \brief Indicates that the argv list should be inherited from this process's
 * argv list.
 */
WASIC_API_EXTERN void wasic_config_inherit_argv(wasic_config_t *config);

/**
 * \brief Sets the list of environment variables available to the WASI instance.
 *
 * By default WASI programs have a blank environment, but this can be used to
 * define some environment variables for them.
 *
 * It is required that the `names` and `values` lists both have `envc` entries.
 *
 * The env vars are copied into the `config` object as part of this function
 * call, so the `names` and `values` pointers only need to stay alive for this
 * function call.
 *
 * This function returns `true` if all environment variables were successfully
 * registered. This returns `false` if environment variables are not valid
 * UTF-8.
 */
WASIC_API_EXTERN bool wasic_config_set_env(wasic_config_t *config, size_t envc,
                                         const char *names[],
                                         const char *values[]);

/**
 * \brief Indicates that the entire environment of the calling process should be
 * inherited by this WASI configuration.
 */
WASIC_API_EXTERN void wasic_config_inherit_env(wasic_config_t *config);

/**
 * \brief Configures standard input to be taken from the specified file.
 *
 * By default WASI programs have no stdin, but this configures the specified
 * file to be used as stdin for this configuration.
 *
 * If the stdin location does not exist or it cannot be opened for reading then
 * `false` is returned. Otherwise `true` is returned.
 */
WASIC_API_EXTERN bool wasic_config_set_stdin_file(wasic_config_t *config,
                                                const char *path);

/**
 * \brief Configures standard input to be taken from the specified
 * #wasm_byte_vec_t.
 *
 * By default WASI programs have no stdin, but this configures the specified
 * bytes to be used as stdin for this configuration.
 *
 * This function takes ownership of the `binary` argument.
 */
WASIC_API_EXTERN void wasic_config_set_stdin_bytes(wasic_config_t *config,
                                                 wasm_byte_vec_t *binary);

/**
 * \brief Configures this process's own stdin stream to be used as stdin for
 * this WASI configuration.
 */
WASIC_API_EXTERN void wasic_config_inherit_stdin(wasic_config_t *config);

/**
 * \brief Configures standard output to be written to the specified file.
 *
 * By default WASI programs have no stdout, but this configures the specified
 * file to be used as stdout.
 *
 * If the stdout location could not be opened for writing then `false` is
 * returned. Otherwise `true` is returned.
 */
WASIC_API_EXTERN bool wasic_config_set_stdout_file(wasic_config_t *config,
                                                 const char *path);

/**
 * \brief Configures this process's own stdout stream to be used as stdout for
 * this WASI configuration.
 */
WASIC_API_EXTERN void wasic_config_inherit_stdout(wasic_config_t *config);

/**
 * \brief Configures standard output to be written to the specified file.
 *
 * By default WASI programs have no stderr, but this configures the specified
 * file to be used as stderr.
 *
 * If the stderr location could not be opened for writing then `false` is
 * returned. Otherwise `true` is returned.
 */
WASIC_API_EXTERN bool wasic_config_set_stderr_file(wasic_config_t *config,
                                                 const char *path);

/**
 * \brief Configures this process's own stderr stream to be used as stderr for
 * this WASI configuration.
 */
WASIC_API_EXTERN void wasic_config_inherit_stderr(wasic_config_t *config);

/**
 * \brief The permissions granted for a directory when preopening it.
 */
enum wasic_dir_perms_flags {
  /**
   * \brief This directory can be read, for example its entries can be iterated
   */
  WASIC_DIR_PERMS_READ = 1,

  /**
   * \brief This directory can be written to, for example new files can be
   * created within it.
   */
  WASIC_DIR_PERMS_WRITE = 2,
};

/**
 * \brief The permissions granted for directories when preopening them,
 * which is a bitmask with flag values from wasic_dir_perms_flags.
 */
typedef size_t wasic_dir_perms;

/**
 * \brief The permissions granted for files when preopening a directory.
 */
enum wasic_file_perms_flags {
  /**
   * \brief Files can be read.
   */
  WASIC_FILE_PERMS_READ = 1,

  /**
   * \brief Files can be written to.
   */
  WASIC_FILE_PERMS_WRITE = 2,
};

/**
 * \brief The max permissions granted a file within a preopened directory,
 * which is a bitmask with flag values from wasic_file_perms_flags.
 */
typedef size_t wasic_file_perms;

/**
 * \brief Configures a "preopened directory" to be available to WASI APIs.
 *
 * By default WASI programs do not have access to anything on the filesystem.
 * This API can be used to grant WASI programs access to a directory on the
 * filesystem, but only that directory (its whole contents but nothing above
 * it).
 *
 * The `host_path` argument here is a path name on the host filesystem, and
 * `guest_path` is the name by which it will be known in wasm.
 *
 * The `dir_perms` argument is the permissions that wasm will have to operate on
 * `guest_path`. This can be used, for example, to provide readonly access to a
 * directory. This argument is a bitmask with the following flag values:
 * - WASIC_DIR_PERMS_READ
 * - WASIC_DIR_PERMS_WRITE
 *
 * The `file_perms` argument is similar to `dir_perms` but corresponds to the
 * maximum set of permissions that can be used for any file in this directory.
 * This argument is a bitmask with the following flag values:
 * - WASIC_FILE_PERMS_READ
 * - WASIC_FILE_PERMS_WRITE
 */
WASIC_API_EXTERN bool wasic_config_preopen_dir(wasic_config_t *config,
                                             const char *host_path,
                                             const char *guest_path,
                                             wasic_dir_perms dir_perms,
                                             wasic_file_perms file_perms);

/**
 * \typedef wasic_instance_t
 * \brief Convenience alias for #wasic_instance_t
 *
 * \struct wasic_instance_t
 * \brief WASI instance which can be used to generate import values
 *
 * \fn void wasic_instance_delete(wasic_instance_t *);
 * \brief Deletes a WASI instance object.
 */
WASIC_DECLARE_OWN(instance)

/**
 * \brief Creates a new instance object using the provided config and store.
 *
 * The caller is expected to deallocate the returned instance.
 *
 * This function takes no ownership of the arguments, but the config and store
 * objects must remain valid until the instance is deleted.
 */
WASIC_API_EXTERN own wasic_instance_t *wasic_instance_new(const wasic_config_t *config, wasm_store_t *store);

/**
 * \brief Configures a WASI instance with the memory to use.
 *
 * Before this, WASI methods which require memory access will return an error.
 */
WASIC_API_EXTERN bool wasic_instance_link_memory(wasic_instance_t *instance, wasm_memory_t *memory);

/**
 * \brief returns a WASI import, or NULL if not supported by the config.
 *
 * The caller is expected to deallocate the returned import.
 *
 * The import is valid only for the lifetime of the supplied config and store.
 */
WASIC_API_EXTERN wasm_extern_t * wasic_instance_resolve(wasic_instance_t *instance, const wasm_importtype_t *type);

// Adapted from <errno.h>
enum WASIC_RESULT {
	WASIC_SUCCESS=0,
	WASIC_EPERM=1, // Operation not permitted
	WASIC_ENOENT=2, // No such file or directory
	WASIC_ESRCH=3, // No such process
	WASIC_EINTR=4, // Interrupted system call
	WASIC_EIO=5, // Input/output error
	WASIC_ENXIO=6, // No such device or address
	WASIC_E2BIG=7, // Argument list too long
	WASIC_ENOEXEC=8, // Exec format error
	WASIC_EBADF=9, // Bad file descriptor
	WASIC_ECHILD=10, // No child processes
	WASIC_EAGAIN=11, // Resource temporarily unavailable
	WASIC_ENOMEM=12, // Cannot allocate memory
	WASIC_EACCES=13, // Permission denied
	WASIC_EFAULT=14, // Bad address
	WASIC_ENOTBLK=15, // Block device required
	WASIC_EBUSY=16, // Device or resource busy
	WASIC_EEXIST=17, // File exists
	WASIC_EXDEV=18, // Invalid cross-device link
	WASIC_ENODEV=19, // No such device
	WASIC_ENOTDIR=20, // Not a directory
	WASIC_EISDIR=21, // Is a directory
	WASIC_EINVAL=22, // Invalid argument
	WASIC_ENFILE=23, // Too many open files in system
	WASIC_EMFILE=24, // Too many open files
	WASIC_ENOTTY=25, // Inappropriate ioctl for device
	WASIC_ETXTBSY=26, // Text file busy
	WASIC_EFBIG=27, // File too large
	WASIC_ENOSPC=28, // No space left on device
	WASIC_ESPIPE=29, // Illegal seek
	WASIC_EROFS=30, // Read-only file system
	WASIC_EMLINK=31, // Too many links
	WASIC_EPIPE=32, // Broken pipe
	WASIC_EDOM=33, // Numerical argument out of domain
	WASIC_ERANGE=34, // Numerical result out of range
	WASIC_EDEADLK=35, // Resource deadlock avoided
	WASIC_ENAMETOOLONG=36, // File name too long
	WASIC_ENOLCK=37, // No locks available
	WASIC_ENOSYS=38, // Function not implemented
	WASIC_ENOTEMPTY=39, // Directory not empty
	WASIC_ELOOP=40, // Too many levels of symbolic links
	WASIC_ENOMSG=42, // No message of desired type
	WASIC_EIDRM=43, // Identifier removed
	WASIC_ECHRNG=44, // Channel number out of range
	WASIC_EL2NSYNC=45, // Level 2 not synchronized
	WASIC_EL3HLT=46, // Level 3 halted
	WASIC_EL3RST=47, // Level 3 reset
	WASIC_ELNRNG=48, // Link number out of range
	WASIC_EUNATCH=49, // Protocol driver not attached
	WASIC_ENOCSI=50, // No CSI structure available
	WASIC_EL2HLT=51, // Level 2 halted
	WASIC_EBADE=52, // Invalid exchange
	WASIC_EBADR=53, // Invalid request descriptor
	WASIC_EXFULL=54, // Exchange full
	WASIC_ENOANO=55, // No anode
	WASIC_EBADRQC=56, // Invalid request code
	WASIC_EBADSLT=57, // Invalid slot
	WASIC_EBFONT=59, // Bad font file format
	WASIC_ENOSTR=60, // Device not a stream
	WASIC_ENODATA=61, // No data available
	WASIC_ETIME=62, // Timer expired
	WASIC_ENOSR=63, // Out of streams resources
	WASIC_ENONET=64, // Machine is not on the network
	WASIC_ENOPKG=65, // Package not installed
	WASIC_EREMOTE=66, // Object is remote
	WASIC_ENOLINK=67, // Link has been severed
	WASIC_EADV=68, // Advertise error
	WASIC_ESRMNT=69, // Srmount error
	WASIC_ECOMM=70, // Communication error on send
	WASIC_EPROTO=71, // Protocol error
	WASIC_EMULTIHOP=72, // Multihop attempted
	WASIC_EDOTDOT=73, // RFS specific error
	WASIC_EBADMSG=74, // Bad message
	WASIC_EOVERFLOW=75, // Value too large for defined data type
	WASIC_ENOTUNIQ=76, // Name not unique on network
	WASIC_EBADFD=77, // File descriptor in bad state
	WASIC_EREMCHG=78, // Remote address changed
	WASIC_ELIBACC=79, // Can not access a needed shared library
	WASIC_ELIBBAD=80, // Accessing a corrupted shared library
	WASIC_ELIBSCN=81, // .lib section in a.out corrupted
	WASIC_ELIBMAX=82, // Attempting to link in too many shared libraries
	WASIC_ELIBEXEC=83, // Cannot exec a shared library directly
	WASIC_EILSEQ=84, // Invalid or incomplete multibyte or wide character
	WASIC_ERESTART=85, // Interrupted system call should be restarted
	WASIC_ESTRPIPE=86, // Streams pipe error
	WASIC_EUSERS=87, // Too many users
	WASIC_ENOTSOCK=88, // Socket operation on non-socket
	WASIC_EDESTADDRREQ=89, // Destination address required
	WASIC_EMSGSIZE=90, // Message too long
	WASIC_EPROTOTYPE=91, // Protocol wrong type for socket
	WASIC_ENOPROTOOPT=92, // Protocol not available
	WASIC_EPROTONOSUPPORT=93, // Protocol not supported
	WASIC_ESOCKTNOSUPPORT=94, // Socket type not supported
	WASIC_EOPNOTSUPP=95, // Operation not supported
	WASIC_EPFNOSUPPORT=96, // Protocol family not supported
	WASIC_EAFNOSUPPORT=97, // Address family not supported by protocol
	WASIC_EADDRINUSE=98, // Address already in use
	WASIC_EADDRNOTAVAIL=99, // Cannot assign requested address
	WASIC_ENETDOWN=100, // Network is down
	WASIC_ENETUNREACH=101, // Network is unreachable
	WASIC_ENETRESET=102, // Network dropped connection on reset
	WASIC_ECONNABORTED=103, // Software caused connection abort
	WASIC_ECONNRESET=104, // Connection reset by peer
	WASIC_ENOBUFS=105, // No buffer space available
	WASIC_EISCONN=106, // Transport endpoint is already connected
	WASIC_ENOTCONN=107, // Transport endpoint is not connected
	WASIC_ESHUTDOWN=108, // Cannot send after transport endpoint shutdown
	WASIC_ETOOMANYREFS=109, // Too many references: cannot splice
	WASIC_ETIMEDOUT=110, // Connection timed out
	WASIC_ECONNREFUSED=111, // Connection refused
	WASIC_EHOSTDOWN=112, // Host is down
	WASIC_EHOSTUNREACH=113, // No route to host
	WASIC_EALREADY=114, // Operation already in progress
	WASIC_EINPROGRESS=115, // Operation now in progress
	WASIC_ESTALE=116, // Stale file handle
	WASIC_EUCLEAN=117, // Structure needs cleaning
	WASIC_ENOTNAM=118, // Not a XENIX named type file
	WASIC_ENAVAIL=119, // No XENIX semaphores available
	WASIC_EISNAM=120, // Is a named type file
	WASIC_EREMOTEIO=121, // Remote I/O error
	WASIC_EDQUOT=122, // Disk quota exceeded
	WASIC_ENOMEDIUM=123, // No medium found
	WASIC_EMEDIUMTYPE=124, // Wrong medium type
	WASIC_ECANCELED=125, // Operation canceled
	WASIC_ENOKEY=126, // Required key not available
	WASIC_EKEYEXPIRED=127, // Key has expired
	WASIC_EKEYREVOKED=128, // Key has been revoked
	WASIC_EKEYREJECTED=129, // Key was rejected by service
	WASIC_EOWNERDEAD=130, // Owner died
	WASIC_ENOTRECOVERABLE=131, // State not recoverable
	WASIC_ERFKILL=132, // Operation not possible due to RF-kill
	WASIC_EHWPOISON=133, // Memory page has hardware error
};

#undef own

#ifdef __cplusplus
} // extern "C"
#endif

#endif // #ifdef WASIC_H
