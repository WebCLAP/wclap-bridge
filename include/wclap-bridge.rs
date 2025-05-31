//pub const true_: u32 = 1;
//pub const false_: u32 = 0;

#[link(name = "wclap-bridge")]
unsafe extern "C" {

	pub fn wclap_global_init(validityCheckLevel: ::std::os::raw::c_uint) -> bool;
	pub fn wclap_global_deinit();
	
	pub fn wclap_open(wclapDir: *const ::std::os::raw::c_char) -> *mut ::std::os::raw::c_void;
	pub fn wclap_open_with_dirs(
		wclapDir: *const ::std::os::raw::c_char,
		presetDir: *const ::std::os::raw::c_char,
		cacheDir: *const ::std::os::raw::c_char,
		varDir: *const ::std::os::raw::c_char,
	) -> *mut ::std::os::raw::c_void;
	
	pub fn wclap_close(handle: *mut ::std::os::raw::c_void) -> bool;
	
	pub fn wclap_get_factory(
		handle: *mut ::std::os::raw::c_void,
		factory_id: *const ::std::os::raw::c_char,
	) -> *const ::std::os::raw::c_void;
	
	pub fn wclap_error() -> *const ::std::os::raw::c_char;
}
