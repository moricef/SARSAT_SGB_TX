# SARSAT SGB Tools Makefile
# Builds test utilities for T.018 signal generation

CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu11
INCLUDES = -I../include
LIBS = -lm

# Directories
SRC_DIR = ../src
INC_DIR = ../include
BUILD_DIR = build
BIN_DIR = .

# Common object files (shared between tools)
COMMON_OBJS = $(BUILD_DIR)/prn_generator.o \
              $(BUILD_DIR)/oqpsk_modulator.o \
              $(BUILD_DIR)/rrc_filter.o

# Tools to build
TOOLS = generate_test_frame generate_test_from_hex

# Default target
all: directories $(TOOLS)

# Create build directory
directories:
	@mkdir -p $(BUILD_DIR)

# Build test frame generator
generate_test_frame: $(BUILD_DIR)/generate_test_frame.o $(COMMON_OBJS)
	@echo "Linking $@"
	@$(CC) $^ $(LIBS) -o $@
	@echo "✓ $@ built successfully"

# Build hex frame generator
generate_test_from_hex: $(BUILD_DIR)/generate_test_from_hex.o $(COMMON_OBJS)
	@echo "Linking $@"
	@$(CC) $^ $(LIBS) -o $@
	@echo "✓ $@ built successfully"

# Compile tool sources
$(BUILD_DIR)/generate_test_frame.o: generate_test_frame.c
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/generate_test_from_hex.o: generate_test_from_hex.c
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Compile common modules
$(BUILD_DIR)/prn_generator.o: $(SRC_DIR)/prn_generator.c $(INC_DIR)/prn_generator.h
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/oqpsk_modulator.o: $(SRC_DIR)/oqpsk_modulator.c $(INC_DIR)/oqpsk_modulator.h
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(BUILD_DIR)/rrc_filter.o: $(SRC_DIR)/rrc_filter.c $(INC_DIR)/rrc_filter.h
	@echo "Compiling $<"
	@$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

# Clean
clean:
	@echo "Cleaning tools build..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(TOOLS)
	@rm -f *.iq *.bin *.txt
	@echo "Clean complete"

# Run test frame generator
run: generate_test_frame
	@echo ""
	@echo "=========================================="
	@echo "Running Test Frame Generator"
	@echo "=========================================="
	@./generate_test_frame

# Run with different patterns
test-zeros: generate_test_frame
	@./generate_test_frame zeros

test-ones: generate_test_frame
	@./generate_test_frame ones

test-alt: generate_test_frame
	@./generate_test_frame alt

test-counter: generate_test_frame
	@./generate_test_frame counter

test-custom: generate_test_frame
	@./generate_test_frame custom

# Help
help:
	@echo "SARSAT SGB Tools Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build all tools"
	@echo "  clean        - Remove build artifacts and output files"
	@echo "  run          - Run test frame generator (default: custom message)"
	@echo "  test-zeros   - Generate test with all-zero message"
	@echo "  test-ones    - Generate test with all-one message"
	@echo "  test-alt     - Generate test with alternating 0/1 pattern"
	@echo "  test-counter - Generate test with binary counter"
	@echo "  test-custom  - Generate test with custom message"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Tools:"
	@echo "  generate_test_frame - Generate T.018 test signal with known message"
	@echo ""
	@echo "Output files:"
	@echo "  test_frame_known.iq         - IQ samples (complex float32)"
	@echo "  test_frame_message.bin      - Reference message (packed bytes)"
	@echo "  test_frame_message_bits.txt - Reference bits (ASCII)"
	@echo "  chips_after_spreading.bin   - Debug chips (int8 I/Q interleaved)"
	@echo ""
	@echo "Usage examples:"
	@echo "  make clean && make"
	@echo "  make run"
	@echo "  make test-custom"
	@echo "  ./generate_test_frame custom"
	@echo "  inspectrum test_frame_known.iq"

.PHONY: all clean run test-zeros test-ones test-alt test-counter test-custom help directories
