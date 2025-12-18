# WCLAP Bridge Plugin

This builds a CLAP plugin which loads a WCLAP using the bridge library.

## Where does it look?

It looks for a WCLAP to open based on its own path, by replacing `.clap`/`.vst3`, and `CLAP`/`VST3` directories with `.wclap` and `WCLAP`.  You can therefore choose a WCLAP to bridge by renaming/symlinking as appropriate.

For example: on MacOS, if this plugin is in `~/Library/Audio/Plug-Ins/CLAP/my-plugin.clap`, it will attempt to open `~/Library/Audio/Plug-Ins/WCLAP/my-plugin.wclap`.
