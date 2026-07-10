CC ?= gcc
AR ?= ar

BUILD_DIR := build
INCLUDE_DIR := include
SRC_DIR := src
DEMO_DIR := demo
REPORT_DIR := report

LIB_NAME := libsafe_shm.so
LIB_TARGET := $(BUILD_DIR)/$(LIB_NAME)
DEMO_TARGET := $(BUILD_DIR)/demo_safe_shm
REPORT_TEX := $(REPORT_DIR)/report.tex
REPORT_TARGET := $(BUILD_DIR)/report/report.pdf

LIBDIR ?= /lib
INCLUDEDIR ?= /usr/local/include

CPPFLAGS += -I$(INCLUDE_DIR)
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -g
PICFLAGS := -fPIC
LDFLAGS +=
LDLIBS += -pthread

.PHONY: all lib demo run install uninstall clean report

all: lib demo

lib: $(LIB_TARGET)

demo: $(DEMO_TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/safe_shm.o: $(SRC_DIR)/safe_shm.c $(INCLUDE_DIR)/safe_shm.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PICFLAGS) -c $< -o $@

$(LIB_TARGET): $(BUILD_DIR)/safe_shm.o
	$(CC) -shared -o $@ $^

$(DEMO_TARGET): $(DEMO_DIR)/main.c $(LIB_TARGET) $(INCLUDE_DIR)/safe_shm.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $< $(LIB_TARGET) -Wl,-rpath,'$$ORIGIN' $(LDLIBS) -o $@

run: demo
	./$(DEMO_TARGET)

install: $(LIB_TARGET) $(INCLUDE_DIR)/safe_shm.h
	install -d "$(DESTDIR)$(LIBDIR)"
	install -m 0755 "$(LIB_TARGET)" "$(DESTDIR)$(LIBDIR)/$(LIB_NAME)"
	install -d "$(DESTDIR)$(INCLUDEDIR)"
	install -m 0644 "$(INCLUDE_DIR)/safe_shm.h" "$(DESTDIR)$(INCLUDEDIR)/safe_shm.h"
	ldconfig 2>/dev/null || true

uninstall:
	rm -f "$(DESTDIR)$(LIBDIR)/$(LIB_NAME)"
	rm -f "$(DESTDIR)$(INCLUDEDIR)/safe_shm.h"
	ldconfig 2>/dev/null || true

report: $(REPORT_TARGET)

$(REPORT_TARGET): $(REPORT_TEX)
	mkdir -p $(BUILD_DIR)/report
	xelatex -interaction=nonstopmode -halt-on-error -output-directory=$(BUILD_DIR)/report $(REPORT_TEX)

clean:
	rm -rf $(BUILD_DIR)
