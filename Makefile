PORT  ?= 8080
IMAGE = bobby
ASSETS_DIR = assets/v1
SDL = docker/sdl-release-2.32.10.zip


.PHONY: all assets sdl build-image

all: run

# dependency: game assets
$(ASSETS_DIR):
	tools/fetch_assets.sh

# dependency: library
$(SDL):
	wget -P docker/ https://github.com/libsdl-org/SDL/archive/release-2.32.10.zip
	mv docker/release-2.32.10.zip $(SDL)

# TODO: pull image
run: $(ASSETS_DIR) $(SDL)
	docker compose up

clean:
	rm -rf $(ASSETS_DIR) $(SDL)
