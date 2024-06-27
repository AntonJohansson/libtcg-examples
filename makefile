build := $(abspath build)
prefix := $(abspath root)
libtcg := $(abspath submodules/libtcg)
qemu_n_jobs := 8
CC := clang

dump-ir: src/dump-ir.c src/cmdline.c src/loadelf.c src/common.c src/analyze-reg-src.c
	${CC} $^ -I${prefix}/include -L${prefix}/lib -ltcg-loader -g -o $@

dump-cfg: src/dump-cfg.c src/loadelf.c
	${CC} $^ -I${prefix}/include -ldl -g -o $@

libtcg: ${build} ${prefix}
	cd ${build} && PATH=/home/aj/git/system/llvm/versions/14/bin:/usr/bin CC=clang ${libtcg}/configure \
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
