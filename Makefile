CXX = clang++
LLVM_CXXFLAGS = $(shell llvm-config --cxxflags)
LLVM_LDFLAGS = $(shell llvm-config --ldflags)
LLVM_LIBS = $(shell llvm-config --system-libs --libs core bitwriter support)

CXXFLAGS = -Wall -Wextra -std=c++17 -stdlib=libc++ $(LLVM_CXXFLAGS) -fexceptions -D__STDCXX_EXCEPTIONS__ -w
LDFLAGS = $(LLVM_LDFLAGS) $(LLVM_LIBS) -lc++ -lc++abi -nodefaultlibs -lc -lm -lgcc_s -lgcc

BUILDDIR = build
SOURCES = main.cc
TARGET = $(BUILDDIR)/tlc

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(SOURCES) | $(BUILDDIR)
	@$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET) $(LDFLAGS)

.PHONY: clean
clean:
	rm -rf $(BUILDDIR)

.PHONY: all
all: $(TARGET)
