.PHONY: build test clean cross run install-frontend

build: install-frontend
	@$(MAKE) -s -C backend build
	@cd frontend && npx vite build --logLevel silent

test:
	@$(MAKE) -s -C backend test
	@cd frontend && npx jest --no-cache

clean:
	@$(MAKE) -s -C backend clean
	@rm -rf frontend/dist frontend/node_modules

cross:
	@$(MAKE) -s -C backend cross

run: build
	@cd frontend && node electron/dev.cjs

install-frontend:
	@cd frontend && npm install --silent 2>/dev/null || true
