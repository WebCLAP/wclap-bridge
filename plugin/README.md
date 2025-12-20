# WCLAP Bridge Plugin

This builds a CLAP plugin which scans/loads WCLAPs using the bridge library.  Where available, it provides plugin GUIs using the CLAP webview extension.

## TODO

* Search paths for non-MacOS systems
* `WCLAP_PATH` environment variable
* Cache the scanned results, and lazily load WCLAPs when we want a plugin from them (instead of loading them all eagerly)
