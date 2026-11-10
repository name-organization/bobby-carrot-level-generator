PORT  ?= 8080
ASSETS_DIR = assets/v1
SDL = docker/sdl-release-2.32.10.zip

.PHONY: all run build-image

all: run

# dependency: library
$(SDL):
	wget -O docker/sdl-release-2.32.10.zip https://github.com/libsdl-org/SDL/archive/release-2.32.10.zip

build-image: $(ASSETS_DIR) $(SDL)
	tools/fetch_assets.sh
	docker compose build bobby

run: build-image
	docker compose up

clean:
	rm -rf $(ASSETS_DIR) $(SDL)
