all: translate-structs cmake

clean:
	rm -rf out

cmake:
	mkdir -p out/static
	cp include/* out/static/
	# CMAKE_BUILD_TYPE is needed for single-config generators (e.g. Makefiles)
	cmake -B out/build -DCMAKE_BUILD_TYPE=Release -DCMAKE_ARCHIVE_OUTPUT_DIRECTORY=../static
	cmake --build out/build --config Release