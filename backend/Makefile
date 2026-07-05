.PHONY: build test clean cross

build:
	@zig build

test:
	@zig build test

clean:
	@rm -rf zig-out .zig-cache

cross:
	@zig build cross -Doptimize=ReleaseFast
