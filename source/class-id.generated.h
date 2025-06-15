#pragma once

namespace wclap {

enum class ClassId {
	clap_ambisonic_config,
	clap_audio_buffer,
	clap_audio_port_configuration_request,
	clap_audio_port_info,
	clap_audio_ports_config,
	clap_color,
	clap_context_menu_builder,
	clap_context_menu_check_entry,
	clap_context_menu_entry,
	clap_context_menu_item_title,
	clap_context_menu_submenu,
	clap_context_menu_target,
	clap_event_header,
	clap_event_midi,
	clap_event_midi2,
	clap_event_midi_sysex,
	clap_event_note,
	clap_event_note_expression,
	clap_event_param_gesture,
	clap_event_param_mod,
	clap_event_param_value,
	clap_event_transport,
	clap_gui_resize_hints,
	clap_host,
	clap_host_ambisonic,
	clap_host_audio_ports,
	clap_host_audio_ports_config,
	clap_host_context_menu,
	clap_host_event_registry,
	clap_host_gui,
	clap_host_latency,
	clap_host_log,
	clap_host_note_name,
	clap_host_note_ports,
	clap_host_params,
	clap_host_preset_load,
	clap_host_remote_controls,
	clap_host_state,
	clap_host_surround,
	clap_host_tail,
	clap_host_thread_check,
	clap_host_thread_pool,
	clap_host_timer_support,
	clap_host_track_info,
	clap_host_voice_info,
	clap_input_events,
	clap_istream,
	clap_note_name,
	clap_note_port_info,
	clap_ostream,
	clap_output_events,
	clap_param_info,
	clap_plugin,
	clap_plugin_ambisonic,
	clap_plugin_audio_ports,
	clap_plugin_audio_ports_activation,
	clap_plugin_audio_ports_config,
	clap_plugin_audio_ports_config_info,
	clap_plugin_configurable_audio_ports,
	clap_plugin_context_menu,
	clap_plugin_descriptor,
	clap_plugin_entry,
	clap_plugin_factory,
	clap_plugin_gui,
	clap_plugin_latency,
	clap_plugin_note_name,
	clap_plugin_note_ports,
	clap_plugin_param_indication,
	clap_plugin_params,
	clap_plugin_preset_load,
	clap_plugin_remote_controls,
	clap_plugin_render,
	clap_plugin_state,
	clap_plugin_state_context,
	clap_plugin_surround,
	clap_plugin_tail,
	clap_plugin_thread_pool,
	clap_plugin_timer_support,
	clap_plugin_track_info,
	clap_plugin_voice_info,
	clap_preset_discovery_factory,
	clap_preset_discovery_filetype,
	clap_preset_discovery_indexer,
	clap_preset_discovery_location,
	clap_preset_discovery_metadata_receiver,
	clap_preset_discovery_provider,
	clap_preset_discovery_provider_descriptor,
	clap_preset_discovery_soundpack,
	clap_process,
	clap_remote_controls_page,
	clap_track_info,
	clap_universal_plugin_id,
	clap_version,
	clap_voice_info
};

template<class ClapStruct>
inline ClassId getClassId();

template<>
inline constexpr ClassId getClassId<clap_ambisonic_config>() {
	return ClassId::clap_ambisonic_config;
}

template<>
inline constexpr ClassId getClassId<clap_audio_buffer>() {
	return ClassId::clap_audio_buffer;
}

template<>
inline constexpr ClassId getClassId<clap_audio_port_configuration_request>() {
	return ClassId::clap_audio_port_configuration_request;
}

template<>
inline constexpr ClassId getClassId<clap_audio_port_info>() {
	return ClassId::clap_audio_port_info;
}

template<>
inline constexpr ClassId getClassId<clap_audio_ports_config>() {
	return ClassId::clap_audio_ports_config;
}

template<>
inline constexpr ClassId getClassId<clap_color>() {
	return ClassId::clap_color;
}

template<>
inline constexpr ClassId getClassId<clap_context_menu_builder>() {
	return ClassId::clap_context_menu_builder;
}

template<>
inline constexpr ClassId getClassId<clap_context_menu_check_entry>() {
	return ClassId::clap_context_menu_check_entry;
}

template<>
inline constexpr ClassId getClassId<clap_context_menu_entry>() {
	return ClassId::clap_context_menu_entry;
}

template<>
inline constexpr ClassId getClassId<clap_context_menu_item_title>() {
	return ClassId::clap_context_menu_item_title;
}

template<>
inline constexpr ClassId getClassId<clap_context_menu_submenu>() {
	return ClassId::clap_context_menu_submenu;
}

template<>
inline constexpr ClassId getClassId<clap_context_menu_target>() {
	return ClassId::clap_context_menu_target;
}

template<>
inline constexpr ClassId getClassId<clap_event_header>() {
	return ClassId::clap_event_header;
}

template<>
inline constexpr ClassId getClassId<clap_event_midi>() {
	return ClassId::clap_event_midi;
}

template<>
inline constexpr ClassId getClassId<clap_event_midi2>() {
	return ClassId::clap_event_midi2;
}

template<>
inline constexpr ClassId getClassId<clap_event_midi_sysex>() {
	return ClassId::clap_event_midi_sysex;
}

template<>
inline constexpr ClassId getClassId<clap_event_note>() {
	return ClassId::clap_event_note;
}

template<>
inline constexpr ClassId getClassId<clap_event_note_expression>() {
	return ClassId::clap_event_note_expression;
}

template<>
inline constexpr ClassId getClassId<clap_event_param_gesture>() {
	return ClassId::clap_event_param_gesture;
}

template<>
inline constexpr ClassId getClassId<clap_event_param_mod>() {
	return ClassId::clap_event_param_mod;
}

template<>
inline constexpr ClassId getClassId<clap_event_param_value>() {
	return ClassId::clap_event_param_value;
}

template<>
inline constexpr ClassId getClassId<clap_event_transport>() {
	return ClassId::clap_event_transport;
}

template<>
inline constexpr ClassId getClassId<clap_gui_resize_hints>() {
	return ClassId::clap_gui_resize_hints;
}

template<>
inline constexpr ClassId getClassId<clap_host>() {
	return ClassId::clap_host;
}

template<>
inline constexpr ClassId getClassId<clap_host_ambisonic>() {
	return ClassId::clap_host_ambisonic;
}

template<>
inline constexpr ClassId getClassId<clap_host_audio_ports>() {
	return ClassId::clap_host_audio_ports;
}

template<>
inline constexpr ClassId getClassId<clap_host_audio_ports_config>() {
	return ClassId::clap_host_audio_ports_config;
}

template<>
inline constexpr ClassId getClassId<clap_host_context_menu>() {
	return ClassId::clap_host_context_menu;
}

template<>
inline constexpr ClassId getClassId<clap_host_event_registry>() {
	return ClassId::clap_host_event_registry;
}

template<>
inline constexpr ClassId getClassId<clap_host_gui>() {
	return ClassId::clap_host_gui;
}

template<>
inline constexpr ClassId getClassId<clap_host_latency>() {
	return ClassId::clap_host_latency;
}

template<>
inline constexpr ClassId getClassId<clap_host_log>() {
	return ClassId::clap_host_log;
}

template<>
inline constexpr ClassId getClassId<clap_host_note_name>() {
	return ClassId::clap_host_note_name;
}

template<>
inline constexpr ClassId getClassId<clap_host_note_ports>() {
	return ClassId::clap_host_note_ports;
}

template<>
inline constexpr ClassId getClassId<clap_host_params>() {
	return ClassId::clap_host_params;
}

template<>
inline constexpr ClassId getClassId<clap_host_preset_load>() {
	return ClassId::clap_host_preset_load;
}

template<>
inline constexpr ClassId getClassId<clap_host_remote_controls>() {
	return ClassId::clap_host_remote_controls;
}

template<>
inline constexpr ClassId getClassId<clap_host_state>() {
	return ClassId::clap_host_state;
}

template<>
inline constexpr ClassId getClassId<clap_host_surround>() {
	return ClassId::clap_host_surround;
}

template<>
inline constexpr ClassId getClassId<clap_host_tail>() {
	return ClassId::clap_host_tail;
}

template<>
inline constexpr ClassId getClassId<clap_host_thread_check>() {
	return ClassId::clap_host_thread_check;
}

template<>
inline constexpr ClassId getClassId<clap_host_thread_pool>() {
	return ClassId::clap_host_thread_pool;
}

template<>
inline constexpr ClassId getClassId<clap_host_timer_support>() {
	return ClassId::clap_host_timer_support;
}

template<>
inline constexpr ClassId getClassId<clap_host_track_info>() {
	return ClassId::clap_host_track_info;
}

template<>
inline constexpr ClassId getClassId<clap_host_voice_info>() {
	return ClassId::clap_host_voice_info;
}

template<>
inline constexpr ClassId getClassId<clap_input_events>() {
	return ClassId::clap_input_events;
}

template<>
inline constexpr ClassId getClassId<clap_istream>() {
	return ClassId::clap_istream;
}

template<>
inline constexpr ClassId getClassId<clap_note_name>() {
	return ClassId::clap_note_name;
}

template<>
inline constexpr ClassId getClassId<clap_note_port_info>() {
	return ClassId::clap_note_port_info;
}

template<>
inline constexpr ClassId getClassId<clap_ostream>() {
	return ClassId::clap_ostream;
}

template<>
inline constexpr ClassId getClassId<clap_output_events>() {
	return ClassId::clap_output_events;
}

template<>
inline constexpr ClassId getClassId<clap_param_info>() {
	return ClassId::clap_param_info;
}

template<>
inline constexpr ClassId getClassId<clap_plugin>() {
	return ClassId::clap_plugin;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_ambisonic>() {
	return ClassId::clap_plugin_ambisonic;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_audio_ports>() {
	return ClassId::clap_plugin_audio_ports;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_audio_ports_activation>() {
	return ClassId::clap_plugin_audio_ports_activation;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_audio_ports_config>() {
	return ClassId::clap_plugin_audio_ports_config;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_audio_ports_config_info>() {
	return ClassId::clap_plugin_audio_ports_config_info;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_configurable_audio_ports>() {
	return ClassId::clap_plugin_configurable_audio_ports;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_context_menu>() {
	return ClassId::clap_plugin_context_menu;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_descriptor>() {
	return ClassId::clap_plugin_descriptor;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_entry>() {
	return ClassId::clap_plugin_entry;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_factory>() {
	return ClassId::clap_plugin_factory;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_gui>() {
	return ClassId::clap_plugin_gui;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_latency>() {
	return ClassId::clap_plugin_latency;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_note_name>() {
	return ClassId::clap_plugin_note_name;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_note_ports>() {
	return ClassId::clap_plugin_note_ports;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_param_indication>() {
	return ClassId::clap_plugin_param_indication;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_params>() {
	return ClassId::clap_plugin_params;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_preset_load>() {
	return ClassId::clap_plugin_preset_load;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_remote_controls>() {
	return ClassId::clap_plugin_remote_controls;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_render>() {
	return ClassId::clap_plugin_render;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_state>() {
	return ClassId::clap_plugin_state;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_state_context>() {
	return ClassId::clap_plugin_state_context;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_surround>() {
	return ClassId::clap_plugin_surround;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_tail>() {
	return ClassId::clap_plugin_tail;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_thread_pool>() {
	return ClassId::clap_plugin_thread_pool;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_timer_support>() {
	return ClassId::clap_plugin_timer_support;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_track_info>() {
	return ClassId::clap_plugin_track_info;
}

template<>
inline constexpr ClassId getClassId<clap_plugin_voice_info>() {
	return ClassId::clap_plugin_voice_info;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_factory>() {
	return ClassId::clap_preset_discovery_factory;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_filetype>() {
	return ClassId::clap_preset_discovery_filetype;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_indexer>() {
	return ClassId::clap_preset_discovery_indexer;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_location>() {
	return ClassId::clap_preset_discovery_location;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_metadata_receiver>() {
	return ClassId::clap_preset_discovery_metadata_receiver;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_provider>() {
	return ClassId::clap_preset_discovery_provider;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_provider_descriptor>() {
	return ClassId::clap_preset_discovery_provider_descriptor;
}

template<>
inline constexpr ClassId getClassId<clap_preset_discovery_soundpack>() {
	return ClassId::clap_preset_discovery_soundpack;
}

template<>
inline constexpr ClassId getClassId<clap_process>() {
	return ClassId::clap_process;
}

template<>
inline constexpr ClassId getClassId<clap_remote_controls_page>() {
	return ClassId::clap_remote_controls_page;
}

template<>
inline constexpr ClassId getClassId<clap_track_info>() {
	return ClassId::clap_track_info;
}

template<>
inline constexpr ClassId getClassId<clap_universal_plugin_id>() {
	return ClassId::clap_universal_plugin_id;
}

template<>
inline constexpr ClassId getClassId<clap_version>() {
	return ClassId::clap_version;
}

template<>
inline constexpr ClassId getClassId<clap_voice_info>() {
	return ClassId::clap_voice_info;
}

}
