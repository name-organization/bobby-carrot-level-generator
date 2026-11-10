PORT  ?= 8080
ASSETS_DIR = assets/v1
IMAGE = bobby
SDL = docker/sdl-release-2.32.10.zip

.PHONY: all run build-image

all: run

# dependency: game assets (images, audio, default levels)
$(ASSETS_DIR):
	tools/fetch_assets.sh

# dependency: library
$(SDL):
	wget -O docker/sdl-release-2.32.10.zip https://github.com/libsdl-org/SDL/archive/release-2.32.10.zip

build-image: $(ASSETS_DIR) $(SDL)
	docker compose build $(IMAGE)

run: build-image
	docker compose up

clean:
	rm -rf $(ASSETS_DIR) $(SDL)
