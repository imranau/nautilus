cmd_src/cxx/built-in.o :=  ld -z max-page-size=0x1000 -melf_x86_64 -dp  -r -o src/cxx/built-in.o src/cxx/cxxglue.o src/cxx/cxxinit.o
