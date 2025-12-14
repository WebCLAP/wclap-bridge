#[link(name = "wclap-bridge")]
unsafe extern "C" {

    pub fn wclap_global_init(timeLimitMs: ::std::os::raw::c_uint) -> bool;
    pub fn wclap_global_deinit();

    pub fn wclap_open(wclapDir: *const ::std::os::raw::c_char) -> *mut ::std::os::raw::c_void;
    pub fn wclap_open_with_dirs(
        wclapDir: *const ::std::os::raw::c_char,
        presetDir: *const ::std::os::raw::c_char,
        cacheDir: *const ::std::os::raw::c_char,
        varDir: *const ::std::os::raw::c_char,
    ) -> *mut ::std::os::raw::c_void;

    pub fn wclap_get_error(
        handle: *mut ::std::os::raw::c_void,
        buffer: *mut ::std::os::raw::c_char,
        bufferCapacity: usize
    ) -> bool;

    pub fn wclap_close(handle: *mut ::std::os::raw::c_void) -> bool;

    /* skip wclap_version() and wclap_bridge_version() for now */

    pub fn wclap_get_factory(
        handle: *mut ::std::os::raw::c_void,
        factory_id: *const ::std::os::raw::c_char,
    ) -> *const ::std::os::raw::c_void;

    pub fn wclap_set_strings(
        pluginIdPrefix: *const ::std::os::raw::c_char,
        pluginNamePrefix: *const ::std::os::raw::c_char,
        pluginNameSuffix: *const ::std::os::raw::c_char,
    );
}
