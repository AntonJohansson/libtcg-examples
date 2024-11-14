build := $(abspath build)
prefix := $(abspath root)
libtcg := $(abspath submodules/libtcg)
qemu_n_jobs := 8
CC := clang

srcs := src/dump-ir.c \
	src/cmdline.c \
	src/loadelf.c \
	src/common.c  \
	src/analyze-reg-src.c \
	src/analyze-max-stack.c \
	src/graphviz.c \
	src/stack_alloc.c

cflags := -O2 \
	  -I${prefix}/include \
	  -L${prefix}/lib \
	  -ltcg-loader \
	  -lm -g \
	  -pedantic \
	  -Wextra \
	  -std=c11

dump-ir: ${srcs}
	${CC} $^ ${cflags} -o $@

libtcg: ${build} ${prefix}
	cd ${build} && ${libtcg}/configure \
	   --prefix=${prefix} \
	   --enable-libtcg \
	   --disable-werror \
	   --disable-kvm \
	   --disable-tools \
	   --disable-system \
	   --disable-libnfs \
	   --disable-vde \
	   --disable-gnutls \
	   --disable-cap-ng \
	   --disable-capstone \
	   -Dvhost_user=disabled \
	   -Dxkbcommon=disabled
	make -C ${build} install -j${qemu_n_jobs}

${build}:
	mkdir -p ${build}

${prefix}:
	mkdir -p ${prefix}
	mkdir -p ${prefix}/bin
	mkdir -p ${prefix}/lib
