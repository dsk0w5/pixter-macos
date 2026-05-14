# =============================================================================
# Root Makefile — builds all tools or individual targets
#
# Usage:
#   make             Show this help message
#   make all         Build all tools
#   make clean       Clean all tools
#   make <tool>      Build a specific tool (e.g. make mytool)
#   make <tool>/clean  Clean a specific tool (e.g. make mytool/clean)
# =============================================================================

# Auto-discover subdirectories that contain a Makefile
TOOLS := $(patsubst %/Makefile,%,$(wildcard */Makefile))

# Default target: print help
.DEFAULT_GOAL := help

# ── Phony declarations ────────────────────────────────────────────────────────
.PHONY: help all clean $(TOOLS) $(addsuffix /clean,$(TOOLS))

# ── Help ──────────────────────────────────────────────────────────────────────
help:
	@echo ""
	@echo "  Root build system"
	@echo "  ════════════════════════════════════════════"
	@echo ""
	@echo "  Targets:"
	@echo "    make all           Build every tool listed below"
	@echo "    make clean         Clean every tool listed below"
	@echo "    make <tool>        Build one specific tool"
	@echo "    make <tool>/clean  Clean one specific tool"
	@echo ""
	@echo "  Discovered tools (directories with a Makefile):"
	@for tool in $(TOOLS); do \
		echo "    - $$tool"; \
	done
	@echo ""
	@echo "  Dependencies / requirements:"
	@echo "    Each tool directory must contain its own Makefile."
	@echo "    The sub-Makefile must support 'make' (build) and 'make clean'."
	@echo ""

# ── Build all ─────────────────────────────────────────────────────────────────
all: $(TOOLS)
	@echo ""
	@echo "  ✓ All tools built successfully."
	@echo ""

# ── Clean all ────────────────────────────────────────────────────────────────
clean: $(addsuffix /clean,$(TOOLS))
	@echo ""
	@echo "  ✓ All tools cleaned."
	@echo ""

# ── Per-tool build rule ───────────────────────────────────────────────────────
# Matches any discovered tool name and runs 'make' inside its directory.
$(TOOLS):
	@echo "  ┌─ Building: $@ ─────────────────────────"
	@$(MAKE) -C $@ --no-print-directory
	@echo "  └─ Done: $@"
	@echo ""

# ── Per-tool clean rule ───────────────────────────────────────────────────────
# Matches <tool>/clean and runs 'make clean' inside its directory.
$(addsuffix /clean,$(TOOLS)):
	$(eval TOOL := $(patsubst %/clean,%,$@))
	@echo "  ┌─ Cleaning: $(TOOL) ────────────────────"
	@$(MAKE) -C $(TOOL) clean --no-print-directory
	@echo "  └─ Done: $(TOOL)"
	@echo ""

# ── Guard against unknown targets ─────────────────────────────────────────────
# Catches anything that isn't a known tool or built-in target.
%:
	@echo ""
	@echo "  ✗ Unknown target: '$@'"
	@echo ""
	@echo "  Run 'make' to see available targets."
	@echo ""
	@exit 1
