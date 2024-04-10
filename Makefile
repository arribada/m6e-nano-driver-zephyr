# Makefile for building project documentation

# Set the documentation directory
DOC_DIR := docs

# Set the source directory for documentation files
DOC_SRC_DIR := $(DOC_DIR)

# Set the output directory for generated documentation
DOC_OUTPUT_DIR := $(DOC_DIR)/build

# Set the command to generate the documentation
DOC_GENERATOR := doxygen

# Default target
all: docs

# Target to generate the documentation
docs:
	@mkdir -p $(DOC_OUTPUT_DIR)
	$(DOC_GENERATOR) $(DOC_SRC_DIR)/doxygen.config

test:
	west twister --device-testing --device-serial /dev/ttyACM0 -p swan_r5  -T tests/integration --integration

# Target to clean the generated documentation
clean:
	rm -rf $(DOC_OUTPUT_DIR)

.PHONY: all docs clean
