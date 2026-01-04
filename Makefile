# WCLAP Bridge Build Makefile
# Cross-platform build for macOS, Windows, Linux
#
# Usage:
#   make                    # Build for current platform
#   make release            # Build optimized release
#   make debug              # Build with debug symbols
#   make clean              # Clean build artifacts
#   make install            # Install to standard CLAP location
#   make install-vst3       # Install VST3 to standard location
#
# For cross-platform builds on CI/cloud VMs:
#   On macOS:   make release  (builds universal x86_64 + arm64)
#   On Windows: make release
#   On Linux:   make release

# Detect platform
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
    PLATFORM := macos
    CLAP_INSTALL_DIR := $(HOME)/Library/Audio/Plug-Ins/CLAP
    VST3_INSTALL_DIR := $(HOME)/Library/Audio/Plug-Ins/VST3
else ifeq ($(UNAME),Linux)
    PLATFORM := linux
    CLAP_INSTALL_DIR := $(HOME)/.clap
    VST3_INSTALL_DIR := $(HOME)/.vst3
else ifeq ($(OS),Windows_NT)
    PLATFORM := windows
    CLAP_INSTALL_DIR := $(COMMONPROGRAMFILES)/CLAP
    VST3_INSTALL_DIR := $(COMMONPROGRAMFILES)/VST3
endif

# Build configuration
BUILD_TYPE ?= Release
BUILD_DIR := build
PLUGIN_BUILD_DIR := plugin/build
CMAKE_FLAGS := -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

# macOS: build universal binary by default
ifeq ($(PLATFORM),macos)
    CMAKE_FLAGS += -DCMAKE_OSX_DEPLOYMENT_TARGET=10.15
endif

# Windows specific
ifeq ($(PLATFORM),windows)
    # Use Ninja if available (faster), otherwise default
    CMAKE_GEN := -G "Ninja"
endif

.PHONY: all release debug clean install install-vst3 submodules library plugin test

all: release

# Initialize git submodules
submodules:
	@echo "==> Initializing submodules..."
	git submodule update --init --recursive

# Build the static library
library: submodules
	@echo "==> Building wclap-bridge library ($(BUILD_TYPE))..."
	cmake -B $(BUILD_DIR) $(CMAKE_FLAGS) $(CMAKE_GEN)
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) --parallel

# Build the bridge plugin (CLAP + VST3)
plugin: library
	@echo "==> Building bridge plugin..."
	cd plugin && cmake -B build $(CMAKE_FLAGS) $(CMAKE_GEN)
	cd plugin && cmake --build build --config $(BUILD_TYPE) --parallel

release:
	$(MAKE) plugin BUILD_TYPE=Release

debug:
	$(MAKE) plugin BUILD_TYPE=Debug

# Build and run tests
test: submodules
	@echo "==> Building and running tests..."
	cmake -B $(BUILD_DIR) $(CMAKE_FLAGS) -DWCLAP_BRIDGE_BUILD_TESTS=ON $(CMAKE_GEN)
	cmake --build $(BUILD_DIR) --config $(BUILD_TYPE) --parallel
	cd $(BUILD_DIR) && ctest --output-on-failure

# Install CLAP plugin to standard location
install: release
	@echo "==> Installing CLAP to $(CLAP_INSTALL_DIR)..."
	@mkdir -p "$(CLAP_INSTALL_DIR)"
ifeq ($(PLATFORM),macos)
	@# Find the .clap bundle
	$(eval CLAP_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.clap" -type d | head -1))
	@if [ -z "$(CLAP_PLUGIN)" ]; then echo "Error: CLAP plugin not found"; exit 1; fi
	@echo "Found: $(CLAP_PLUGIN)"
	@echo "Removing quarantine attribute..."
	@xattr -rd com.apple.quarantine "$(CLAP_PLUGIN)" 2>/dev/null || true
	@echo "Signing plugin (ad-hoc)..."
	@codesign -s - -f --deep "$(CLAP_PLUGIN)"
	@cp -R "$(CLAP_PLUGIN)" "$(CLAP_INSTALL_DIR)/"
	@echo "Installed to $(CLAP_INSTALL_DIR)/"
else ifeq ($(PLATFORM),linux)
	$(eval CLAP_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.clap" -type f | head -1))
	@cp "$(CLAP_PLUGIN)" "$(CLAP_INSTALL_DIR)/"
	@echo "Installed to $(CLAP_INSTALL_DIR)/"
else ifeq ($(PLATFORM),windows)
	@echo "Windows install: Copy plugin/build/**/wclap-bridge-plugin.clap to $(CLAP_INSTALL_DIR)"
endif

# Install VST3 plugin to standard location
install-vst3: release
	@echo "==> Installing VST3 to $(VST3_INSTALL_DIR)..."
	@mkdir -p "$(VST3_INSTALL_DIR)"
ifeq ($(PLATFORM),macos)
	$(eval VST3_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.vst3" -type d | head -1))
	@if [ -z "$(VST3_PLUGIN)" ]; then echo "Error: VST3 plugin not found"; exit 1; fi
	@echo "Found: $(VST3_PLUGIN)"
	@echo "Removing quarantine attribute..."
	@xattr -rd com.apple.quarantine "$(VST3_PLUGIN)" 2>/dev/null || true
	@echo "Signing plugin (ad-hoc)..."
	@codesign -s - -f --deep "$(VST3_PLUGIN)"
	@cp -R "$(VST3_PLUGIN)" "$(VST3_INSTALL_DIR)/"
	@echo "Installed to $(VST3_INSTALL_DIR)/"
else ifeq ($(PLATFORM),linux)
	$(eval VST3_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.vst3" -type d | head -1))
	@cp -R "$(VST3_PLUGIN)" "$(VST3_INSTALL_DIR)/"
	@echo "Installed to $(VST3_INSTALL_DIR)/"
else ifeq ($(PLATFORM),windows)
	@echo "Windows install: Copy plugin/build/**/wclap-bridge-plugin.vst3 to $(VST3_INSTALL_DIR)"
endif

# Install both CLAP and VST3
install-all: install install-vst3

# Clean build artifacts
clean:
	@echo "==> Cleaning..."
	rm -rf $(BUILD_DIR) $(PLUGIN_BUILD_DIR) out

# Package for distribution
package: release
	@echo "==> Packaging for distribution..."
	@mkdir -p dist
ifeq ($(PLATFORM),macos)
	$(eval CLAP_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.clap" -type d | head -1))
	$(eval VST3_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.vst3" -type d | head -1))
	@cp -R "$(CLAP_PLUGIN)" dist/
	@cp -R "$(VST3_PLUGIN)" dist/
	@cd dist && zip -r wclap-bridge-macos-universal.zip *.clap *.vst3
else ifeq ($(PLATFORM),linux)
	$(eval CLAP_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.clap" -type f | head -1))
	$(eval VST3_PLUGIN := $(shell find $(PLUGIN_BUILD_DIR) -name "*.vst3" -type d | head -1))
	@cp "$(CLAP_PLUGIN)" dist/
	@cp -R "$(VST3_PLUGIN)" dist/
	@cd dist && tar czf wclap-bridge-linux-x86_64.tar.gz *
else ifeq ($(PLATFORM),windows)
	@echo "Windows packaging not implemented in Makefile"
endif
	@echo "Package created in dist/"

# Help
help:
	@echo "WCLAP Bridge Build System"
	@echo ""
	@echo "Targets:"
	@echo "  make              Build release for current platform"
	@echo "  make release      Build optimized release"
	@echo "  make debug        Build with debug symbols"
	@echo "  make test         Build and run unit tests"
	@echo "  make install      Install CLAP to standard folder"
	@echo "  make install-vst3 Install VST3 to standard folder"
	@echo "  make install-all  Install both CLAP and VST3"
	@echo "  make package      Create distributable package"
	@echo "  make clean        Remove build artifacts"
	@echo ""
	@echo "Platform: $(PLATFORM)"
	@echo "CLAP install dir: $(CLAP_INSTALL_DIR)"
	@echo "VST3 install dir: $(VST3_INSTALL_DIR)"
