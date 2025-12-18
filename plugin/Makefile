all: bridge-plugin

CMAKE_PARAMS := -DCMAKE_BUILD_TYPE=Release -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=.. -DCMAKE_LIBRARY_OUTPUT_DIRECTORY=..

out/build: CMakeLists.txt
	cmake -B out/build $(CMAKE_PARAMS)

bridge-plugin: out/build
	cmake --build out/build --target wclap-bridge-plugin_clap --config Release

clean:
	rm -rf out

# Development

open-reaper:
	/Applications/REAPER.app/Contents/MacOS/REAPER REAPER/basics/basics.RPP

nodemon:
	nodemon --watch wclap-bridge --watch source -e h,hpp,cpp --exec "make bridge-plugin || exit 1"
